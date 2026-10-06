#pragma once
// DAT-0004 - Orchestrates bridge retrieval -> normalization -> validation.
//
// The ingestor never fabricates: a failed bridge read yields MISSING/UNKNOWN
// quality, and invalid bars are reported, not repaired.

#include "data/BarNormalizer.h"
#include "data/DataValidator.h"
#include "foundation/DataQualityState.h"
#include "foundation/Timestamp.h"
#include "mt5/PythonBridgeClient.h"

#include <memory>
#include <string>
#include <vector>

namespace aura {

struct IngestResult {
    bool ok = false;
    DataQualityState quality = DataQualityState::UNKNOWN;
    std::vector<Bar> bars;
    std::vector<std::string> issues;
    std::string errorCode;
    std::string errorMessage;
};

struct IngestRequest {
    std::string symbol = "XAUUSD";
    Timeframe timeframe = Timeframe::M15;
    int count = 500;
    bool closedOnly = true;
};

class MarketDataIngestor {
public:
    MarketDataIngestor(std::shared_ptr<IPythonBridgeClient> client,
                       BarNormalizer normalizer = {},
                       DataValidator validator = {})
        : client_(std::move(client)),
          normalizer_(normalizer),
          validator_(validator) {}

    // Retrieve, normalize, and validate. Bars whose individual validation
    // fails are retained in the result (so callers can audit them) but the
    // aggregate quality reflects the worst defect.
    IngestResult ingest(const IngestRequest& request, Timestamp now);

private:
    std::shared_ptr<IPythonBridgeClient> client_;
    BarNormalizer normalizer_;
    DataValidator validator_;
};

}  // namespace aura
