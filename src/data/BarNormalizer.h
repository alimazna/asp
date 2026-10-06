#pragma once
// DAT-0006 - Normalizes raw bridge candles into AURA's canonical Bar.
//
// A Bar carries provenance (source symbol/broker) and the receive timestamp so
// downstream stages never have to reconstruct where a value came from.

#include "foundation/DataQualityState.h"
#include "foundation/Timestamp.h"
#include "mt5/Mt5BridgeContract.h"

#include <cstdint>
#include <string>
#include <vector>

namespace aura {

struct Bar {
    Timeframe timeframe = Timeframe::M15;
    std::int64_t openTimeSec = 0;   // source bar open time
    double open = 0.0;
    double high = 0.0;
    double low = 0.0;
    double close = 0.0;
    std::int64_t tickVolume = 0;
    std::int64_t realVolume = 0;
    std::int64_t spread = 0;

    Timestamp receivedAt;           // when AURA observed the bar
    DataQualityState quality = DataQualityState::UNKNOWN;
    std::string sourceSymbol;
    std::string sourceBroker;

    std::int64_t closeTimeSec() const noexcept {
        return openTimeSec + intervalMillis(timeframe) / 1000;
    }
};

class BarNormalizer {
public:
    // Convert one bridge candle. Quality is set to UNKNOWN until validated;
    // normalization never asserts validity it has not checked.
    Bar normalize(const BridgeCandle& candle, Timeframe timeframe,
                  const std::string& symbol, const std::string& broker,
                  Timestamp receivedAt) const;

    std::vector<Bar> normalizeAll(const std::vector<BridgeCandle>& candles,
                                  Timeframe timeframe, const std::string& symbol,
                                  const std::string& broker,
                                  Timestamp receivedAt) const;
};

}  // namespace aura
