#pragma once
// AYZR-ECO-001 — Standard Engine Contract.
//
// Every independent strategy engine (Trend Following, Mean Reversion, Breakout,
// Market Microstructure, Event Driven) and every existing horizon engine
// (SHORT/MEDIUM/LONG) publishes its findings through ONE struct: EngineResult.
// The Central Orchestrator consumes only this contract, so it never needs to
// know how a given engine computes its numbers.
//
// Honesty invariants (enforced by construction, tested in EngineContractTests):
//   * Absent data is `nullopt` / empty — never a plausible default.
//   * `confidenceMetadata` carries a probability ONLY when the engine has a
//     calibration record; ASTRA's 0..100 `score` is NOT a probability and is
//     never placed here.
//   * The original event is immutable once published; `eventId` is deterministic
//     so a replayed run yields a byte-identical id.

#include "foundation/DataQualityState.h"
#include "mt5/Mt5BridgeContract.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace aura {
namespace ayzr {
namespace eco {

/// Schema version of the contract itself. Bump only on a breaking change; the
/// orchestrator accepts any minor revision it knows and rejects unknown majors.
inline constexpr int kEngineContractSchemaVersion = 1;

/// Lifecycle of a single engine event, mirroring the Opportunity Lifecycle in
/// the spec. `WITHHELD` and `EXPIRED` are terminal and are still persisted.
enum class EventStatus {
    DETECTED,
    PENDING_CONFIRMATION,
    PUBLISHED,
    ACTIVE,
    TARGET_HIT,
    WARNING,
    CLOSE_RECOMMENDED,
    INVALIDATED,
    EXPIRED,
    CLOSED,
    WITHHELD,
};

const char* toString(EventStatus s) noexcept;
bool parseEventStatus(const std::string& text, EventStatus& out) noexcept;

/// Arabic UI label for an event status. ASCII id stays in toString().
const char* eventStatusLabelAr(EventStatus s) noexcept;

/// Direction a strategy proposes. Deliberately separate from ASTRA's
/// SignalDirection (LONG/SHORT/NONE) because the ecosystem adds the explicit
/// NO_TRADE state the spec requires.
enum class EngineDirection {
    BULLISH,
    BEARISH,
    SIDEWAYS,
    NO_TRADE,
};

const char* toString(EngineDirection d) noexcept;
bool parseEngineDirection(const std::string& text, EngineDirection& out) noexcept;
const char* engineDirectionLabelAr(EngineDirection d) noexcept;

/// Strategy family — the engine's own identity, distinct from its instance id.
enum class StrategyFamily {
    TREND_FOLLOWING,
    MEAN_REVERSION,
    BREAKOUT,
    MICROSTRUCTURE,
    EVENT_DRIVEN,
    HORIZON_SHORT,
    HORIZON_MEDIUM,
    HORIZON_LONG,
    ASTRA_CORE,
    UNKNOWN,
};

const char* toString(StrategyFamily f) noexcept;
const char* strategyFamilyLabelAr(StrategyFamily f) noexcept;

/// Health of an engine as observed by the orchestrator.
enum class EngineHealth {
    HEALTHY,
    DEGRADED,
    UNAVAILABLE,
    DISABLED,
    WAITING_FOR_DATA,
    STALE_OUTPUT,
};

const char* toString(EngineHealth h) noexcept;
const char* engineHealthLabelAr(EngineHealth h) noexcept;

/// A single price level with provenance. `source` says where it came from, so a
/// consumer can tell an ASTRA-computed level from an engine-derived one.
struct PriceLevel {
    double price = 0.0;
    /// "astra" | "engine_atr" | "structure" | "session_range" | ...
    std::string source;
    /// Free-form Arabic note shown in the UI when present.
    std::string noteAr;
};

/// Evidence item: a human-readable reason plus an optional machine code.
struct Evidence {
    std::string code;      // stable machine token, e.g. "ema_alignment"
    std::string textAr;    // Arabic explanation for the user
    /// Optional supporting value (e.g. the ATR used). nullopt when not numeric.
    std::optional<double> value;
};

/// Confidence metadata. A probability is emitted ONLY when a calibration
/// artifact exists; otherwise `probability` is nullopt and `calibrated` false.
struct ConfidenceMetadata {
    bool calibrated = false;
    std::optional<double> probability;      // [0,1], only if calibrated
    std::string calibrationRef;             // path/id of the calibration record
    /// The engine's own bounded 0..100 ranking score (NOT a probability).
    std::optional<double> score;
    std::string scoreKind;                  // e.g. "astra_rank_score"
};

/// The unified engine result. One per (engine, symbol, timeframe, event).
struct EngineResult {
    // --- identity ---
    std::string eventId;                    // deterministic, immutable
    std::optional<std::string> opportunityId;   // set once the orchestrator groups it
    std::string engineId;                   // instance id, e.g. "trend-m15"
    std::string engineVersion;              // semver-ish, e.g. "1.0.0"
    StrategyFamily strategyFamily = StrategyFamily::UNKNOWN;
    std::string eventType;                  // e.g. "signal", "health", "warning"

    // --- market coordinates ---
    std::string symbol = "XAUUSD";
    EngineDirection direction = EngineDirection::NO_TRADE;
    Timeframe timeframe = Timeframe::M15;

    // --- timing ---
    std::int64_t createdAt = 0;             // epoch seconds, UTC
    std::int64_t updatedAt = 0;
    std::optional<std::int64_t> expiresAt;  // nullopt = no expiry
    std::int64_t dataTimestamp = 0;         // open time of the bar that produced this

    // --- provenance ---
    std::string inputSnapshotReference;     // id of the frozen input snapshot
    std::string dataSource;                 // "biquote" | "dukascopy" | "mock"
    DataQualityState dataQuality = DataQualityState::UNKNOWN;

    // --- outcome ---
    EventStatus status = EventStatus::DETECTED;

    // --- levels (nullopt when the engine has no justified level) ---
    std::optional<PriceLevel> entry;
    std::optional<PriceLevel> stopLoss;
    std::vector<PriceLevel> takeProfitLevels;

    // --- explanation ---
    std::vector<std::string> reasonCodes;
    std::string explanationAr;
    std::vector<Evidence> supportingEvidence;
    std::vector<Evidence> contradictingEvidence;
    std::vector<std::string> riskFlags;

    // --- confidence (probability only when calibrated) ---
    ConfidenceMetadata confidence;

    /// Correlation id tying every event from one decision cycle together.
    std::string correlationId;

    /// True when this event came from the frozen ASTRA core. The orchestrator
    /// must never mutate such an event's numeric fields.
    bool immutableCoreOutput = false;
};

/// True when the result is safe to show as a live opportunity: it must have a
/// real direction, decision-grade data, and be within its validity window.
bool isPublishable(const EngineResult& r, std::int64_t nowSec) noexcept;

/// Deterministic event id from the identity tuple. Same inputs => same id, so a
/// replay reproduces ids byte-for-byte and duplicate detection is exact.
std::string makeEventId(const std::string& engineId, const std::string& symbol,
                        Timeframe tf, std::int64_t dataTimestamp) noexcept;

}  // namespace eco
}  // namespace ayzr
}  // namespace aura
