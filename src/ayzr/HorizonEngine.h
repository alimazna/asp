#pragma once
// AYZR-ENG-001 — Multi-horizon engine layer.
//
// Wraps the existing ASTRA SignalEngine in THREE independent horizon
// profiles (SHORT / MEDIUM / LONG). This file does NOT re-implement any
// ASTRA math: each profile is a SignalConfig + EligibilityConfig + a
// timeframe policy, and every decision still flows through
// aura::SignalEngine::generate().
//
// Invariants (from the AYZR spec):
//   * ASTRA's own SignalEngine and its config are never mutated.
//   * Each horizon owns its own state, trade slot and lifecycle.
//   * At most ONE active trade per horizon (News opportunities are separate).
//   * A horizon that is busy stays silent; it never queues or replaces.

#include "eligibility/EligibilityEngine.h"
#include "features/FeatureSnapshot.h"
#include "regime/RegimeState.h"
#include "signals/SignalEngine.h"
#include "structure/StructureSnapshot.h"

#include <cstdint>
#include <string>
#include <vector>

namespace aura {
namespace ayzr {

/// Analysis horizon. Drives the timeframe set and the config profile.
enum class Horizon {
    SHORT,
    MEDIUM,
    LONG,
};

inline const char* toString(Horizon h) noexcept {
    switch (h) {
        case Horizon::SHORT:  return "SHORT";
        case Horizon::MEDIUM: return "MEDIUM";
        case Horizon::LONG:   return "LONG";
    }
    return "SHORT";
}

/// Arabic display name (UI-facing). ASCII id stays in `toString`.
inline const char* horizonLabelAr(Horizon h) noexcept {
    switch (h) {
        case Horizon::SHORT:  return "قصير المدى";
        case Horizon::MEDIUM: return "متوسط المدى";
        case Horizon::LONG:   return "بعيد المدى";
    }
    return "قصير المدى";
}

/// Per-horizon tuning. Every field is a real, documented difference — not a
/// renamed copy. The entry timeframe is the operational frame; the context
/// frames are the higher-timeframe confirmation set.
struct HorizonProfile {
    Horizon horizon = Horizon::SHORT;
    /// Operational timeframe the signal is generated on.
    Timeframe entryTimeframe = Timeframe::M5;
    /// Higher-timeframe context, ordered low -> high. Used for the trend gate.
    std::vector<Timeframe> contextTimeframes;
    /// ASTRA signal thresholds for this horizon.
    SignalConfig signal;
    /// Eligibility gates for this horizon.
    EligibilityConfig eligibility;
    /// Reject the signal unless the context trend agrees with the direction.
    bool requireContextAlignment = true;
    /// Minimum rawStrength (0..1) a candidate must reach to be published.
    double minStrength = 0.0;
};

/// The three canonical profiles. Documented rationale per horizon:
///
/// SHORT  — M5 entry, M15 context. Tighter stops (1.2x ATR), faster target
///          (2.0x ATR), higher RSI band (needs a real push), ranging allowed
///          because short-horizon setups live in ranges.
///
/// MEDIUM — M15 entry, H1 + H4 context. Balanced stops (1.6x ATR), wider
///          target (3.0x ATR), moderate RSI band, ranging allowed but a
///          context trend is required.
///
/// LONG   — H4 entry, D1 context. Wide stops (2.2x ATR), distant target
///          (4.5x ATR), narrower RSI band (does not need an extreme), ranging
///          NOT allowed (a long-horizon trade needs a trend), and the highest
///          strength bar so only strong setups publish.
HorizonProfile makeShortProfile();
HorizonProfile makeMediumProfile();
HorizonProfile makeLongProfile();
HorizonProfile profileFor(Horizon h);

/// A published horizon signal, carrying the frozen origin snapshot.
struct HorizonSignal {
    Horizon horizon = Horizon::SHORT;
    Timeframe timeframe = Timeframe::M15;
    std::int64_t asOfBarOpenSec = 0;
    SignalDirection direction = SignalDirection::NONE;
    double entry = 0.0;
    double stop = 0.0;
    double target = 0.0;
    double strength = 0.0;
    std::string rationale;
    DataQualityState quality = DataQualityState::UNKNOWN;
    bool valid = false;
};

/// One horizon's runtime. Owns its slot, its last decision and its counters.
/// No shared mutable state with the other horizons.
class HorizonEngine {
public:
    explicit HorizonEngine(HorizonProfile profile);

    Horizon horizon() const noexcept { return profile_.horizon; }
    const HorizonProfile& profile() const noexcept { return profile_; }

    /// True while this horizon holds an active trade. A busy horizon refuses
    /// to emit a new signal, regardless of what the other horizons are doing.
    bool isBusy() const noexcept { return busy_; }

    /// Mark the slot as occupied (a trade was opened).
    void occupy() noexcept { busy_ = true; }

    /// Release the slot. Only a real close event may call this.
    void release() noexcept { busy_ = false; }

    /// Generate a candidate on the entry timeframe. Returns a valid=false
    /// signal when the horizon is busy, the candidate is weak, or the context
    /// disagrees. Never fabricates a direction.
    HorizonSignal evaluate(const FeatureSnapshot& entryFeatures,
                           const StructureSnapshot& entryStructure,
                           const RegimeState& entryRegime,
                           const EligibilityState& entryEligibility,
                           const std::vector<RegimeState>& contextRegimes) const;

    /// Number of signals this horizon has published since construction.
    std::uint64_t publishedCount() const noexcept { return published_; }
    void notePublished() noexcept { ++published_; }

private:
    HorizonProfile profile_;
    bool busy_ = false;
    std::uint64_t published_ = 0;
};

/// Registry that owns all three horizons and enforces their independence.
/// This is the only place that knows the full set.
class HorizonRegistry {
public:
    HorizonRegistry();

    HorizonEngine& engine(Horizon h);
    const HorizonEngine& engine(Horizon h) const;

    /// Evaluate every idle horizon and return the signals that published.
    /// Busy horizons are skipped without affecting the others.
    std::vector<HorizonSignal> evaluateAll(
        const FeatureSnapshot& entryFeatures,
        const StructureSnapshot& entryStructure,
        const RegimeState& entryRegime,
        const EligibilityState& entryEligibility,
        const std::vector<RegimeState>& contextRegimes);

private:
    HorizonEngine short_;
    HorizonEngine medium_;
    HorizonEngine long_;
};

}  // namespace ayzr
}  // namespace aura
