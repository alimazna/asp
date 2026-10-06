#pragma once
// DEC-0005 - Deterministic market structure engine.

#include "data/BarNormalizer.h"
#include "features/FeatureSnapshot.h"
#include "structure/StructureSnapshot.h"

#include <cstddef>
#include <vector>

namespace aura {

struct StructureConfig {
    std::size_t swingLookback = 2;   // bars on each side of a swing pivot
    std::size_t minSwings = 2;
};

class StructureEngine {
public:
    explicit StructureEngine(StructureConfig config = {}) : config_(config) {}

    StructureSnapshot compute(const std::vector<Bar>& bars,
                              const FeatureSnapshot& features) const;

private:
    StructureConfig config_;
};

}  // namespace aura
