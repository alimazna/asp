#pragma once
// AYZR-NEWS-IMPACT-001 — News-to-open-trade impact linkage.
//
// When a news analysis yields a directional view, the owner wants to know what
// that view means for the positions that are ALREADY OPEN. This module answers
// exactly that question, deterministically, and nothing more:
//
//   * it compares the news direction against each open trade's direction and
//     reports alignment (supportive / contradicting / neutral);
//   * it NEVER claims a trade will win, and never claims a past trade was
//     profitable — alignment is not an outcome;
//   * it never mutates a trade, opens one, closes one, or touches ASTRA.
//
// The linkage is read-only and additive: it reads the news direction and the
// current open trades and returns a per-trade verdict. Because the rule is a
// pure function of two enums, it is fully testable without a server.

#include <string>
#include <vector>

#include "ayzr/TradeLifecycle.h"

namespace aura {
namespace ayzr {

/// How a news view relates to one open trade.
enum class ImpactRelation {
    /// The news view points the same way as the trade.
    Supportive,
    /// The news view points the opposite way from the trade.
    Contradicting,
    /// The news view carries no direction (sideways / no_trade) or the trade is
    /// not the instrument this linkage covers.
    Neutral,
};

inline const char* toString(ImpactRelation r) noexcept {
    switch (r) {
        case ImpactRelation::Supportive:   return "supportive";
        case ImpactRelation::Contradicting: return "contradicting";
        case ImpactRelation::Neutral:      return "neutral";
    }
    return "neutral";
}

inline const char* relationLabelAr(ImpactRelation r) noexcept {
    switch (r) {
        case ImpactRelation::Supportive:   return "متوافق مع الخبر";
        case ImpactRelation::Contradicting: return "متعارض مع الخبر";
        case ImpactRelation::Neutral:      return "محايد";
    }
    return "محايد";
}

/// One open trade's relationship to the news view.
struct TradeImpact {
    std::string tradeId;
    std::string sourceLabelAr;   // "قصير المدى" / "صفقة خبر" / ...
    std::string directionLabelAr; // "شراء" / "بيع"
    ImpactRelation relation = ImpactRelation::Neutral;
    std::string relationLabelAr;  // Arabic label for the relation
    std::string explanationAr;    // one honest sentence, no outcome claim
};

/// Map a news direction token to a trade direction, if it has one.
/// `bullish` -> BUY, `bearish` -> SELL; `sideways`/`no_trade`/anything else ->
/// no direction. The token set matches the backend's closed vocabulary.
inline bool newsDirectionToTradeDirection(const std::string& newsDirection,
                                          TradeDirection& out) {
    if (newsDirection == "bullish") { out = TradeDirection::BUY;  return true; }
    if (newsDirection == "bearish") { out = TradeDirection::SELL; return true; }
    return false;
}

/// Compute the impact of a news view on one open trade.
///
/// `symbol` is the trade's instrument. Only the project's supported instrument
/// (XAUUSD) is linked; anything else is Neutral, because a gold-news view says
/// nothing about another market.
inline TradeImpact computeTradeImpact(const TradeRecord& trade,
                                      const std::string& newsDirection,
                                      const std::string& symbol = "XAUUSD") {
    TradeImpact ti;
    ti.tradeId = trade.origin.tradeId;
    ti.sourceLabelAr = sourceLabelAr(trade.origin.source);
    ti.directionLabelAr = directionLabelAr(trade.origin.direction);
    ti.relationLabelAr = relationLabelAr(ImpactRelation::Neutral);
    ti.relation = ImpactRelation::Neutral;

    if (trade.origin.symbol != symbol) {
        ti.explanationAr =
            "لا ينطبق: الصفقة ليست على " + symbol + ".";
        return ti;
    }

    TradeDirection newsDir;
    if (!newsDirectionToTradeDirection(newsDirection, newsDir)) {
        // sideways / no_trade: the news makes no directional claim, so it
        // cannot agree or disagree with a position.
        ti.explanationAr =
            "الخبر لا يحمل اتجاهاً واضحاً، لذلك لا يؤيد الصفقة ولا يعارضها.";
        return ti;
    }

    if (newsDir == trade.origin.direction) {
        ti.relation = ImpactRelation::Supportive;
        ti.relationLabelAr = relationLabelAr(ImpactRelation::Supportive);
        ti.explanationAr =
            "اتجاه الخبر يتفق مع اتجاه الصفقة. هذا توافق في الاتجاه فقط، وليس "
            "توقعاً بالربح.";
    } else {
        ti.relation = ImpactRelation::Contradicting;
        ti.relationLabelAr = relationLabelAr(ImpactRelation::Contradicting);
        ti.explanationAr =
            "اتجاه الخبر يخالف اتجاه الصفقة. هذا تعارض في الاتجاه فقط، وليس "
            "توقعاً بالخسارة.";
    }
    return ti;
}

/// Compute the impact of a news view on every open trade.
inline std::vector<TradeImpact> computeOpenTradeImpacts(
    const TradeLifecycleManager& mgr, const std::string& newsDirection,
    const std::string& symbol = "XAUUSD") {
    std::vector<TradeImpact> out;
    for (const auto& t : mgr.openTrades()) {
        out.push_back(computeTradeImpact(t, newsDirection, symbol));
    }
    return out;
}

}  // namespace ayzr
}  // namespace aura
