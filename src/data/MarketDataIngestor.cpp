// DAT-0005 - Market data ingestion implementation.

#include "data/MarketDataIngestor.h"

namespace aura {

IngestResult MarketDataIngestor::ingest(const IngestRequest& request,
                                        Timestamp now) {
    IngestResult result;

    if (!client_) {
        result.quality = DataQualityState::UNKNOWN;
        result.errorCode = "BRIDGE_UNAVAILABLE";
        result.errorMessage = "no bridge client configured";
        return result;
    }

    const auto bridgeResult =
        client_->candles(request.symbol, request.timeframe, request.count,
                         request.closedOnly);
    if (!bridgeResult.ok) {
        result.quality = DataQualityState::MISSING;
        result.errorCode = bridgeResult.error.code;
        result.errorMessage = bridgeResult.error.message;
        return result;
    }

    const std::string broker = client_->config().host.empty() ? "" : "";
    result.bars = normalizer_.normalizeAll(bridgeResult.value, request.timeframe,
                                           request.symbol, broker, now);
    if (result.bars.empty()) {
        result.quality = DataQualityState::MISSING;
        result.errorCode = "MARKET_DATA_MISSING";
        result.errorMessage = "bridge returned zero candles";
        return result;
    }

    // Per-bar validation.
    DataQualityState worst = DataQualityState::VALID;
    for (auto& bar : result.bars) {
        const BarValidation validation = validator_.validateBar(bar, now);
        bar.quality = validation.quality;
        if (!validation.ok) {
            result.issues.push_back(std::string(toString(validation.defect)) +
                                    ": " + validation.detail);
            // Rank: INVALID/UNKNOWN are worse than STALE/DEGRADED.
            if (validation.quality == DataQualityState::INVALID ||
                validation.quality == DataQualityState::UNKNOWN) {
                worst = DataQualityState::INVALID;
            } else if (worst == DataQualityState::VALID) {
                worst = validation.quality;
            }
        }
    }

    // Sequence validation.
    const SequenceValidation sequence = validator_.validateSequence(result.bars);
    if (!sequence.ok) {
        for (const auto& issue : sequence.issues) result.issues.push_back(issue);
        if (worst == DataQualityState::VALID) worst = sequence.quality;
    }

    result.ok = (worst == DataQualityState::VALID);
    result.quality = worst;
    if (!result.ok && result.errorCode.empty()) {
        result.errorCode = "MARKET_DATA_INVALID";
        result.errorMessage = "one or more bars failed validation";
    }
    return result;
}

}  // namespace aura
