// RES-0015 - Resilience runtime: dependency graph implementation.

#include "resilience/DependencyGraph.h"

#include <algorithm>
#include <functional>
#include <set>

namespace aura {

bool DependencyGraph::addDependency(const DependencyDescriptor& edge) {
    if (edge.dependent.empty() || edge.dependency.empty()) return false;
    if (edge.dependent == edge.dependency) return false;  // no self-edge
    std::lock_guard<std::mutex> lock(mutex_);
    auto& deps = forward_[edge.dependent];
    for (const auto& existing : deps) {
        if (existing.dependency == edge.dependency) {
            return false;  // duplicate edge is a no-op
        }
    }
    deps.push_back(edge);
    reverse_[edge.dependency].push_back(edge.dependent);
    return true;
}

bool DependencyGraph::hasEdge(const CapabilityId& dependent,
                              const CapabilityId& dependency) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = forward_.find(dependent);
    if (it == forward_.end()) return false;
    for (const auto& e : it->second) {
        if (e.dependency == dependency) return true;
    }
    return false;
}

std::vector<DependencyDescriptor> DependencyGraph::dependenciesOf(
    const CapabilityId& id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = forward_.find(id);
    if (it == forward_.end()) return {};
    return it->second;
}

std::vector<CapabilityId> DependencyGraph::directDependents(
    const CapabilityId& id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = reverse_.find(id);
    if (it == reverse_.end()) return {};
    return it->second;
}

std::vector<CapabilityId> DependencyGraph::allDependents(
    const CapabilityId& id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<CapabilityId> result;
    std::set<CapabilityId> visited;
    std::vector<CapabilityId> frontier{id};
    while (!frontier.empty()) {
        CapabilityId current = frontier.back();
        frontier.pop_back();
        auto it = reverse_.find(current);
        if (it == reverse_.end()) continue;
        for (const auto& dep : it->second) {
            if (visited.insert(dep).second) {
                result.push_back(dep);
                frontier.push_back(dep);
            }
        }
    }
    return result;
}

bool DependencyGraph::hasCycle() const {
    std::lock_guard<std::mutex> lock(mutex_);
    enum class Mark { WHITE, GRAY, BLACK };
    std::map<CapabilityId, Mark> marks;
    for (const auto& kv : forward_) marks[kv.first] = Mark::WHITE;
    for (const auto& kv : reverse_) marks[kv.first] = Mark::WHITE;

    std::function<bool(const CapabilityId&)> visit = [&](const CapabilityId& node) -> bool {
        marks[node] = Mark::GRAY;
        auto it = forward_.find(node);
        if (it != forward_.end()) {
            for (const auto& e : it->second) {
                Mark m = marks.count(e.dependency) ? marks[e.dependency] : Mark::WHITE;
                if (m == Mark::GRAY) return true;
                if (m == Mark::WHITE && visit(e.dependency)) return true;
            }
        }
        marks[node] = Mark::BLACK;
        return false;
    };

    for (const auto& kv : forward_) {
        if (marks[kv.first] == Mark::WHITE) {
            if (visit(kv.first)) return true;
        }
    }
    return false;
}

std::size_t DependencyGraph::edgeCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::size_t total = 0;
    for (const auto& kv : forward_) total += kv.second.size();
    return total;
}

std::vector<CapabilityId> DependencyGraph::nodes() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::set<CapabilityId> all;
    for (const auto& kv : forward_) {
        all.insert(kv.first);
        for (const auto& e : kv.second) all.insert(e.dependency);
    }
    for (const auto& kv : reverse_) {
        all.insert(kv.first);
        for (const auto& d : kv.second) all.insert(d);
    }
    return {all.begin(), all.end()};
}

}  // namespace aura
