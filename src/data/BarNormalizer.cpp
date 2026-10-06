// DAT-0007 - Bar normalization implementation.

#include "data/BarNormalizer.h"

namespace aura {

Bar BarNormalizer::normalize(const BridgeCandle& candle, Timeframe timeframe,
                             const std::string& symbol, const std::string& broker,
                             Timestamp receivedAt) const {
    Bar bar;
    bar.timeframe = timeframe;
    bar.openTimeSec = candle.openTime;
    bar.open = candle.open;
    bar.high = candle.high;
    bar.low = candle.low;
    bar.close = candle.close;
    bar.tickVolume = candle.tickVolume;
    bar.realVolume = candle.realVolume;
    bar.spread = candle.spread;
    bar.receivedAt = receivedAt;
    bar.sourceSymbol = symbol;
    bar.sourceBroker = broker;
    bar.quality = DataQualityState::UNKNOWN;
    return bar;
}

std::vector<Bar> BarNormalizer::normalizeAll(
    const std::vector<BridgeCandle>& candles, Timeframe timeframe,
    const std::string& symbol, const std::string& broker,
    Timestamp receivedAt) const {
    std::vector<Bar> bars;
    bars.reserve(candles.size());
    for (const auto& candle : candles) {
        bars.push_back(normalize(candle, timeframe, symbol, broker, receivedAt));
    }
    return bars;
}

}  // namespace aura
