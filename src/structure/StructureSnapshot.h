#pragma once
// DEC-0004 - Structure snapshot: deterministic swing/trend structure.

#include "features/FeatureSnapshot.h"
#include "foundation/DataQualityState.h"
#include "mt5/Mt5BridgeContract.h"

#include <cstdint>
#include <vector>

namespace aura {

enum class StructureBias {
    BULLISH,
    BEARISH,
    RANGE,
    UNKNOWN,
};

inline const char* toString(StructureBias bias) noexcept {
    switch (bias) {
        case StructureBias::BULLISH: return "BULLISH";
        case StructureBias::BEARISH: return "BEARISH";
        case StructureBias::RANGE:   return "RANGE";
        case StructureBias::UNKNOWN: return "UNKNOWN";
    }
    return "UNKNOWN";
}

struct SwingPoint {
    std::int64_t barOpenSec = 0;
    double price = 0.0;
    bool isHigh = false;
};

struct StructureSnapshot {
    Timeframe timeframe = Timeframe::M15;
    std::int64_t asOfBarOpenSec = 0;
    StructureBias bias = StructureBias::UNKNOWN;
    bool higherHighs = false;
    bool higherLows = false;
    bool lowerHighs = false;
    bool lowerLows = false;
    double lastSwingHigh = 0.0;
    double lastSwingLow = 0.0;
    double structureScore = 0.0;   // -1..1 (bearish..bullish)
    std::vector<SwingPoint> swings;
    DataQualityState quality = DataQualityState::UNKNOWN;
    bool valid = false;
    std::string detail;
};

}  // namespace aura
