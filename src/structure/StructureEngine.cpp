// DEC-0006 - Structure engine implementation.

#include "structure/StructureEngine.h"

namespace aura {

StructureSnapshot StructureEngine::compute(const std::vector<Bar>& bars,
                                           const FeatureSnapshot& features) const {
    StructureSnapshot snapshot;
    snapshot.timeframe = features.timeframe;
    snapshot.asOfBarOpenSec = features.asOfBarOpenSec;

    const std::size_t lookback = config_.swingLookback;
    if (bars.size() < 2 * lookback + 1) {
        snapshot.quality = DataQualityState::INCOMPLETE;
        snapshot.detail = "insufficient bars for swing detection";
        return snapshot;
    }

    // Identify alternating swing pivots. A bar is a swing high when its high
    // exceeds `lookback` bars on both sides; the mirror defines a swing low.
    std::vector<SwingPoint> swings;
    for (std::size_t i = lookback; i + lookback < bars.size(); ++i) {
        bool isHigh = true;
        bool isLow = true;
        for (std::size_t j = 1; j <= lookback; ++j) {
            if (bars[i].high <= bars[i - j].high || bars[i].high <= bars[i + j].high) {
                isHigh = false;
            }
            if (bars[i].low >= bars[i - j].low || bars[i].low >= bars[i + j].low) {
                isLow = false;
            }
        }
        if (isHigh) {
            swings.push_back({bars[i].openTimeSec, bars[i].high, true});
        } else if (isLow) {
            swings.push_back({bars[i].openTimeSec, bars[i].low, false});
        }
    }

    if (swings.size() < config_.minSwings) {
        snapshot.quality = DataQualityState::INCOMPLETE;
        snapshot.detail = "insufficient swing points";
        return snapshot;
    }

    snapshot.swings = swings;

    // Compare the last two swing highs and lows.
    double lastHigh = 0.0;
    double prevHigh = 0.0;
    double lastLow = 0.0;
    double prevLow = 0.0;
    int highs = 0;
    int lows = 0;
    for (auto it = swings.rbegin(); it != swings.rend(); ++it) {
        if (it->isHigh && highs < 2) {
            if (highs == 0) lastHigh = it->price;
            else prevHigh = it->price;
            ++highs;
        } else if (!it->isHigh && lows < 2) {
            if (lows == 0) lastLow = it->price;
            else prevLow = it->price;
            ++lows;
        }
        if (highs >= 2 && lows >= 2) break;
    }

    if (highs >= 2) {
        snapshot.higherHighs = lastHigh > prevHigh;
        snapshot.lowerHighs = lastHigh < prevHigh;
    }
    if (lows >= 2) {
        snapshot.higherLows = lastLow > prevLow;
        snapshot.lowerLows = lastLow < prevLow;
    }
    snapshot.lastSwingHigh = lastHigh;
    snapshot.lastSwingLow = lastLow;

    int bullishSignals = 0;
    int bearishSignals = 0;
    if (snapshot.higherHighs) ++bullishSignals;
    if (snapshot.higherLows) ++bullishSignals;
    if (snapshot.lowerHighs) ++bearishSignals;
    if (snapshot.lowerLows) ++bearishSignals;

    if (bullishSignals > bearishSignals) {
        snapshot.bias = StructureBias::BULLISH;
        snapshot.structureScore = 0.5 + 0.25 * static_cast<double>(bullishSignals - 1);
    } else if (bearishSignals > bullishSignals) {
        snapshot.bias = StructureBias::BEARISH;
        snapshot.structureScore = -0.5 - 0.25 * static_cast<double>(bearishSignals - 1);
    } else {
        snapshot.bias = StructureBias::RANGE;
        snapshot.structureScore = 0.0;
    }
    if (snapshot.structureScore > 1.0) snapshot.structureScore = 1.0;
    if (snapshot.structureScore < -1.0) snapshot.structureScore = -1.0;

    snapshot.valid = true;
    snapshot.quality = DataQualityState::VALID;
    return snapshot;
}

}  // namespace aura
