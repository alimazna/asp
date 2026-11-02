// AYZR-NEWS-IMPACT-TEST-001 — news-to-open-trade impact linkage tests.
//
// The linkage is a pure function of (news direction, trade direction). These
// tests pin its contract:
//   * a matching direction is Supportive, an opposing one Contradicting;
//   * a non-directional news view (sideways/no_trade) is Neutral for every
//     trade — it cannot agree or disagree with a position;
//   * a trade on another instrument is Neutral for a gold-news view;
//   * an explanation is always present and NEVER claims an outcome (no
//     "will win" / "profitable" wording);
//   * the linkage never mutates the trade it reads.

#include "ayzr/NewsTradeImpact.h"

#include <cstdio>
#include <string>

using namespace aura::ayzr;

namespace {

int g_fail = 0;
int g_pass = 0;

void check(bool ok, const std::string& what) {
    if (ok) { ++g_pass; std::printf("[PASS] %s\n", what.c_str()); }
    else { ++g_fail; std::printf("[FAIL] %s\n", what.c_str()); }
}

TradeOrigin origin(TradeDirection dir, const std::string& symbol = "XAUUSD",
                   TradeSource src = TradeSource::HORIZON_SHORT) {
    TradeOrigin o;
    o.source = src;
    o.direction = dir;
    o.symbol = symbol;
    o.entry = 2650.0;
    o.stop = 2640.0;
    o.targets.push_back(TargetLevel{2665.0, false, 0});
    return o;
}

/// The explanation must not promise an outcome. Any of these phrasings would be
/// a fabricated forecast, which the system must never emit.
bool claimsOutcome(const std::string& s) {
    static const char* banned[] = {"سيربح", "ستربح", "مضمون", "ربح مؤكد",
                                   "ستخسر", "خسارة مؤكدة", "النتيجة"};
    for (const char* b : banned) {
        if (s.find(b) != std::string::npos) return true;
    }
    return false;
}

}  // namespace

int main() {
    // 1. Bullish news vs a BUY trade is Supportive.
    {
        TradeLifecycleManager mgr;
        const std::string id = mgr.open(origin(TradeDirection::BUY));
        const auto ti = computeTradeImpact(*mgr.find(id), "bullish");
        check(ti.relation == ImpactRelation::Supportive, "bullish_vs_buy_supportive");
        check(ti.relationLabelAr == std::string("متوافق مع الخبر"),
              "supportive_has_arabic_label");
        check(!ti.explanationAr.empty(), "supportive_has_explanation");
        check(!claimsOutcome(ti.explanationAr), "supportive_does_not_claim_outcome");
        check(ti.directionLabelAr == std::string("شراء"), "direction_label_is_arabic");
    }

    // 2. Bullish news vs a SELL trade is Contradicting.
    {
        TradeLifecycleManager mgr;
        const std::string id = mgr.open(origin(TradeDirection::SELL));
        const auto ti = computeTradeImpact(*mgr.find(id), "bullish");
        check(ti.relation == ImpactRelation::Contradicting,
              "bullish_vs_sell_contradicting");
        check(!claimsOutcome(ti.explanationAr),
              "contradicting_does_not_claim_outcome");
    }

    // 3. Bearish news vs a SELL trade is Supportive; vs a BUY trade Contradicting.
    {
        TradeLifecycleManager mgr;
        const std::string sell = mgr.open(origin(TradeDirection::SELL));
        check(computeTradeImpact(*mgr.find(sell), "bearish").relation ==
                  ImpactRelation::Supportive, "bearish_vs_sell_supportive");
        const std::string buy = mgr.open(origin(TradeDirection::BUY));
        check(computeTradeImpact(*mgr.find(buy), "bearish").relation ==
                  ImpactRelation::Contradicting, "bearish_vs_buy_contradicting");
    }

    // 4. A non-directional news view is Neutral for every trade. This is the
    //    ambiguity case: the news makes no claim, so it cannot agree.
    {
        TradeLifecycleManager mgr;
        const std::string buy = mgr.open(origin(TradeDirection::BUY));
        const std::string sell = mgr.open(origin(TradeDirection::SELL));
        for (const std::string& dir : {std::string("sideways"),
                                       std::string("no_trade"),
                                       std::string("")}) {
            check(computeTradeImpact(*mgr.find(buy), dir).relation ==
                      ImpactRelation::Neutral, "nondirectional_neutral_for_buy");
            check(computeTradeImpact(*mgr.find(sell), dir).relation ==
                      ImpactRelation::Neutral, "nondirectional_neutral_for_sell");
        }
    }

    // 5. A trade on another instrument is Neutral: gold news says nothing about
    //    another market.
    {
        TradeLifecycleManager mgr;
        const std::string id = mgr.open(origin(TradeDirection::BUY, "EURUSD"));
        const auto ti = computeTradeImpact(*mgr.find(id), "bullish");
        check(ti.relation == ImpactRelation::Neutral, "other_symbol_neutral");
        check(ti.explanationAr.find("XAUUSD") != std::string::npos,
              "other_symbol_explains_why");
    }

    // 6. computeOpenTradeImpacts returns one entry per OPEN trade, newest first
    //    (matching TradeLifecycleManager::openTrades), and skips closed ones.
    {
        TradeLifecycleManager mgr;
        const std::string a = mgr.open(origin(TradeDirection::BUY));
        const std::string b = mgr.open(origin(TradeDirection::SELL));
        const std::string c = mgr.open(origin(TradeDirection::BUY));
        mgr.close(b, TradeStatus::STOPPED_OUT, 1'700'000'500, "إغلاق");
        const auto impacts = computeOpenTradeImpacts(mgr, "bullish");
        check(impacts.size() == 2, "only_open_trades_linked");
        check(impacts[0].tradeId == c, "newest_open_trade_first");
        check(impacts[1].tradeId == a, "older_open_trade_second");
        check(impacts[0].relation == ImpactRelation::Supportive,
              "first_is_supportive");
        check(impacts[1].relation == ImpactRelation::Supportive,
              "third_is_supportive");
    }

    // 7. Reading the linkage does not mutate the trade: origin and status are
    //    unchanged after the comparison.
    {
        TradeLifecycleManager mgr;
        const std::string id = mgr.open(origin(TradeDirection::BUY));
        const TradeRecord before = *mgr.find(id);
        (void)computeOpenTradeImpacts(mgr, "bullish");
        const TradeRecord after = *mgr.find(id);
        check(before.origin.direction == after.origin.direction,
              "linkage_does_not_change_direction");
        check(before.origin.entry == after.origin.entry,
              "linkage_does_not_change_entry");
        check(before.status == after.status, "linkage_does_not_change_status");
        check(after.events.empty() == before.events.empty(),
              "linkage_records_no_event");
    }

    // 8. A NEWS-sourced trade is labelled as such.
    {
        TradeLifecycleManager mgr;
        const std::string id =
            mgr.open(origin(TradeDirection::BUY, "XAUUSD", TradeSource::NEWS));
        const auto ti = computeTradeImpact(*mgr.find(id), "bullish");
        check(ti.sourceLabelAr == std::string("صفقة خبر"),
              "news_trade_source_label_arabic");
    }

    std::printf("\n%d test case(s), %d failed, %d passed\n", g_pass + g_fail,
                g_fail, g_pass);
    std::printf("RESULT: %s\n", g_fail == 0 ? "PASS" : "FAIL");
    return g_fail == 0 ? 0 : 1;
}
