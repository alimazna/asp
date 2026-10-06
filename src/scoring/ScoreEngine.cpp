// DEC-0015 - Score engine implementation.

#include "scoring/ScoreEngine.h"

#include <cmath>

namespace aura {

ScoreResult ScoreEngine::compute(const FeatureSnapshot& features,
                                 const StructureSnapshot& structure,
                                 const RegimeState& regime,
                                 const SignalCandidate& candidate) const {
    ScoreResult result;

    if (!features.valid || !candidate.valid) {
        result.quality = !features.valid ? features.quality : candidate.quality;
        return result;
    }

    if (candidate.direction == SignalDirection::NONE) {
        result.valid = true;
        result.quality = DataQualityState::VALID;
        result.score = 0.0;
        result.components.push_back("no directional signal");
        return result;
    }

    const double dirSign =
        candidate.direction == SignalDirection::LONG ? 1.0 : -1.0;

    // Direction agreement: structure bias and regime trend must not contradict.
    double directionAgreement = 0.0;
    if (structure.bias == StructureBias::BULLISH && dirSign > 0) directionAgreement += 1.0;
    if (structure.bias == StructureBias::BEARISH && dirSign < 0) directionAgreement += 1.0;
    if (regime.regime == RegimeType::TRENDING_UP && dirSign > 0) directionAgreement += 1.0;
    if (regime.regime == RegimeType::TRENDING_DOWN && dirSign < 0) directionAgreement += 1.0;
    result.breakdown.directionScore = directionAgreement / 2.0 * 30.0;

    // Structure strength magnitude.
    result.breakdown.structureScore =
        std::fabs(structure.structureScore) * 20.0;

    // Regime suitability: trending regimes score higher than ranging.
    double regimeBase = 0.0;
    switch (regime.regime) {
        case RegimeType::TRENDING_UP:
        case RegimeType::TRENDING_DOWN: regimeBase = 1.0; break;
        case RegimeType::RANGING:       regimeBase = 0.4; break;
        case RegimeType::QUIET:         regimeBase = 0.3; break;
        case RegimeType::VOLATILE:      regimeBase = 0.2; break;
        case RegimeType::UNKNOWN:       regimeBase = 0.0; break;
    }
    result.breakdown.regimeScore = regimeBase * 20.0;

    // Momentum alignment: RSI on the correct side of 50.
    const double rsiAlignment =
        (dirSign > 0) ? (features.rsi - 50.0) / 50.0 : (50.0 - features.rsi) / 50.0;
    result.breakdown.momentumScore =
        std::max(0.0, std::min(1.0, rsiAlignment)) * 20.0;

    // Raw strength contribution (0..10).
    const double strengthScore = std::max(0.0, std::min(1.0, candidate.rawStrength)) * 10.0;

    // Volatility penalty: excessive volatility reduces the score.
    result.breakdown.volatilityPenalty =
        std::max(0.0, features.volatility - 0.012) * 1000.0;
    if (result.breakdown.volatilityPenalty > 15.0) {
        result.breakdown.volatilityPenalty = 15.0;
    }

    double score = result.breakdown.directionScore + result.breakdown.structureScore +
                   result.breakdown.regimeScore + result.breakdown.momentumScore +
                   strengthScore - result.breakdown.volatilityPenalty;
    score = std::max(0.0, std::min(100.0, score));

    result.score = score;
    result.valid = true;
    result.quality = DataQualityState::VALID;
    result.components.push_back("direction=" +
                                std::to_string(result.breakdown.directionScore));
    result.components.push_back("structure=" +
                                std::to_string(result.breakdown.structureScore));
    result.components.push_back("regime=" +
                                std::to_string(result.breakdown.regimeScore));
    result.components.push_back("momentum=" +
                                std::to_string(result.breakdown.momentumScore));
    result.components.push_back("penalty=" +
                                std::to_string(result.breakdown.volatilityPenalty));
    return result;
}

}  // namespace aura
