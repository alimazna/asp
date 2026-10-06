#pragma once
// EVO-0005 - Evolution graph.
//
// Tracks lineage between hypotheses and candidates so a promoted strategy can
// always be traced back to the evidence that produced it.

#include "evolution/CandidateRegistry.h"
#include "research/HypothesisEngine.h"

#include <map>
#include <string>
#include <vector>

namespace aura {

struct GraphNode {
    EntityId id;
    std::string kind;          // "hypothesis" | "candidate" | "experiment"
    std::string label;
    std::vector<EntityId> parents;
    std::vector<EntityId> children;
};

class EvolutionGraph {
public:
    EvolutionGraph() = default;

    void addHypothesis(const Hypothesis& hypothesis);
    void addCandidate(const RegisteredCandidate& candidate);
    void addExperimentEdge(const EntityId& hypothesisId,
                           const EntityId& experimentId);

    bool hasNode(const EntityId& id) const;
    bool getNode(const EntityId& id, GraphNode& out) const;

    std::vector<EntityId> ancestors(const EntityId& id) const;
    std::vector<EntityId> descendants(const EntityId& id) const;

    std::size_t nodeCount() const noexcept { return nodes_.size(); }

private:
    std::map<EntityId, GraphNode> nodes_;
};

}  // namespace aura
