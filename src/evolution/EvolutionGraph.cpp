// EVO-0006 - Evolution graph implementation.

#include "evolution/EvolutionGraph.h"

#include <algorithm>

namespace aura {

void EvolutionGraph::addHypothesis(const Hypothesis& hypothesis) {
    if (!hypothesis.valid()) return;
    GraphNode& node = nodes_[hypothesis.hypothesisId];
    node.id = hypothesis.hypothesisId;
    node.kind = "hypothesis";
    node.label = hypothesis.statement;
}

void EvolutionGraph::addCandidate(const RegisteredCandidate& candidate) {
    if (!candidate.candidate.isValid()) return;
    GraphNode& node = nodes_[candidate.candidate.candidateId];
    node.id = candidate.candidate.candidateId;
    node.kind = "candidate";
    node.label = candidate.candidate.label;

    const EntityId& parent = candidate.candidate.originHypothesis;
    if (nodes_.find(parent) == nodes_.end()) return;
    node.parents.push_back(parent);
    nodes_[parent].children.push_back(candidate.candidate.candidateId);
}

void EvolutionGraph::addExperimentEdge(const EntityId& hypothesisId,
                                       const EntityId& experimentId) {
    if (nodes_.find(hypothesisId) == nodes_.end()) return;
    GraphNode& node = nodes_[experimentId];
    node.id = experimentId;
    node.kind = "experiment";
    node.parents.push_back(hypothesisId);
    nodes_[hypothesisId].children.push_back(experimentId);
}

bool EvolutionGraph::hasNode(const EntityId& id) const {
    return nodes_.find(id) != nodes_.end();
}

bool EvolutionGraph::getNode(const EntityId& id, GraphNode& out) const {
    auto it = nodes_.find(id);
    if (it == nodes_.end()) return false;
    out = it->second;
    return true;
}

std::vector<EntityId> EvolutionGraph::ancestors(const EntityId& id) const {
    std::vector<EntityId> result;
    std::vector<EntityId> frontier;
    auto it = nodes_.find(id);
    if (it == nodes_.end()) return result;
    frontier = it->second.parents;
    while (!frontier.empty()) {
        const EntityId current = frontier.back();
        frontier.pop_back();
        if (std::find(result.begin(), result.end(), current) != result.end()) continue;
        result.push_back(current);
        auto node = nodes_.find(current);
        if (node == nodes_.end()) continue;
        for (const auto& parent : node->second.parents) frontier.push_back(parent);
    }
    return result;
}

std::vector<EntityId> EvolutionGraph::descendants(const EntityId& id) const {
    std::vector<EntityId> result;
    std::vector<EntityId> frontier;
    auto it = nodes_.find(id);
    if (it == nodes_.end()) return result;
    frontier = it->second.children;
    while (!frontier.empty()) {
        const EntityId current = frontier.back();
        frontier.pop_back();
        if (std::find(result.begin(), result.end(), current) != result.end()) continue;
        result.push_back(current);
        auto node = nodes_.find(current);
        if (node == nodes_.end()) continue;
        for (const auto& child : node->second.children) frontier.push_back(child);
    }
    return result;
}

}  // namespace aura
