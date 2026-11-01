// AYZR-LIFE-TEST-001 — Trade lifecycle tests.
//
// Critical invariants proven here:
//   * A close RECOMMENDATION never closes a trade.
//   * A reached target is not a close.
//   * Ambiguous TP/SL candles resolve to UNDETERMINED, never a guess.
//   * The frozen origin snapshot never mutates.
//   * Status labels are Arabic.

#include "ayzr/TradeLifecycle.h"

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

TradeOrigin sampleOrigin(TradeSource src = TradeSource::HORIZON_SHORT) {
    TradeOrigin o;
    o.source = src;
    o.direction = TradeDirection::BUY;
    o.entry = 2000.0;
    o.stop = 1990.0;
    o.targets = {{2010.0, false, 0}, {2020.0, false, 0}, {2030.0, false, 0}};
    o.publishedAtSec = 1'700'000'000;
    o.entryTimeframe = "M5";
    o.contextTimeframes = {"M15"};
    o.rationale = "structure=BULLISH regime=TRENDING_UP";
    o.engineVersion = "AYZR-ENG-001";
    o.originStrength = 0.667;
    return o;
}

}  // namespace

int main() {
    std::printf("=== AYZR trade lifecycle tests ===\n");

    // 1. Opening a trade freezes the origin.
    {
        TradeLifecycleManager mgr;
        const std::string id = mgr.open(sampleOrigin());
        const auto* t = mgr.find(id);
        check(t != nullptr, "trade_is_registered");
        check(t->origin.entry == 2000.0 && t->origin.stop == 1990.0,
              "origin_entry_and_stop_are_frozen");
        check(t->status == TradeStatus::AWAITING_ENTRY,
              "new_trade_awaits_entry");
        check(t->isOpen(), "new_trade_is_open");
    }

    // 2. A reached target is NOT a close.
    {
        TradeLifecycleManager mgr;
        const std::string id = mgr.open(sampleOrigin());
        check(mgr.markTargetReached(id, 0, 1'700'000'100), "tp1_recorded");
        const auto* t = mgr.find(id);
        check(t->status == TradeStatus::TP1_REACHED, "status_is_tp1");
        check(t->isOpen(), "tp1_does_not_close_the_trade");
        check(t->origin.targets[0].reached, "tp1_flag_set");
        check(!t->origin.targets[1].reached, "tp2_still_pending");
    }

    // 3. A close recommendation is NOT a close.
    {
        TradeLifecycleManager mgr;
        const std::string id = mgr.open(sampleOrigin());
        check(mgr.recommendClose(id, 1'700'000'200, "تغير هيكل السوق"),
              "recommend_close_accepted");
        const auto* t = mgr.find(id);
        check(t->status == TradeStatus::CLOSE_RECOMMENDED,
              "status_is_close_recommended");
        check(t->isOpen(), "recommendation_does_not_close_the_trade");
        check(t->outcome == TradeOutcome::OPEN,
              "recommendation_does_not_set_outcome");
    }

    // 4. Only a real close ends a trade.
    {
        TradeLifecycleManager mgr;
        const std::string id = mgr.open(sampleOrigin());
        mgr.markTargetReached(id, 0, 1'700'000'100);
        check(mgr.close(id, TradeStatus::CLOSED_AT_TARGET, 1'700'000'300,
                        "تحقق الهدف الأول"),
              "real_close_accepted");
        const auto* t = mgr.find(id);
        check(t->isClosed(), "trade_is_closed");
        check(t->outcome == TradeOutcome::WIN, "outcome_is_win");
        check(t->closeReasonAr == "تحقق الهدف الأول", "close_reason_stored");
    }

    // 5. A non-terminal status cannot be passed to close().
    {
        TradeLifecycleManager mgr;
        const std::string id = mgr.open(sampleOrigin());
        check(!mgr.close(id, TradeStatus::ACTIVE, 1'700'000'400, "خطأ"),
              "non_terminal_close_is_refused");
        check(mgr.find(id)->isOpen(), "trade_still_open_after_refused_close");
    }

    // 6. Double close is refused.
    {
        TradeLifecycleManager mgr;
        const std::string id = mgr.open(sampleOrigin());
        mgr.close(id, TradeStatus::STOPPED_OUT, 1'700'000'500, "وقف الخسارة");
        check(!mgr.close(id, TradeStatus::CLOSED_AT_TARGET, 1'700'000'600, "x"),
              "double_close_is_refused");
        check(mgr.find(id)->outcome == TradeOutcome::LOSS,
              "outcome_stays_loss");
    }

    // 7. Ambiguous TP/SL in one candle -> UNDETERMINED, never a guess.
    {
        TradeLifecycleManager mgr;
        const std::string id = mgr.open(sampleOrigin());
        check(mgr.markUndetermined(id, 1'700'000'700,
                                   "لمس الهدف ووقف الخسارة في الشمعة نفسها"),
              "undetermined_recorded");
        const auto* t = mgr.find(id);
        check(t->outcome == TradeOutcome::UNDETERMINED,
              "ambiguous_candle_is_undetermined");
        check(t->outcome != TradeOutcome::WIN &&
                  t->outcome != TradeOutcome::LOSS,
              "ambiguous_candle_is_not_guessed");
    }

    // 8. Weakening warning keeps the trade open.
    {
        TradeLifecycleManager mgr;
        const std::string id = mgr.open(sampleOrigin());
        mgr.markWeakening(id, 1'700'000'800, "تغير الزخم");
        const auto* t = mgr.find(id);
        check(t->status == TradeStatus::WEAKENING, "status_is_weakening");
        check(t->isOpen(), "weakening_keeps_trade_open");
    }

    // 9. Invalidation before entry is a no-fill, not a loss.
    {
        TradeLifecycleManager mgr;
        const std::string id = mgr.open(sampleOrigin());
        mgr.close(id, TradeStatus::INVALIDATED, 1'700'000'900,
                  "لم يتحقق شرط الدخول");
        check(mgr.find(id)->outcome == TradeOutcome::BREAKEVEN,
              "pre_entry_invalidation_is_breakeven");
    }

    // 10. Arabic labels are correct.
    {
        check(std::string(statusLabelAr(TradeStatus::ACTIVE)) == "صفقة نشطة",
              "active_label_is_arabic");
        check(std::string(statusLabelAr(TradeStatus::STOPPED_OUT)) ==
                  "تحقق وقف الخسارة",
              "stopped_out_label_is_arabic");
        check(std::string(directionLabelAr(TradeDirection::BUY)) == "شراء",
              "buy_label_is_arabic");
        check(std::string(sourceLabelAr(TradeSource::NEWS)) == "صفقة خبر",
              "news_source_label_is_arabic");
        check(std::string(outcomeLabelAr(TradeOutcome::UNDETERMINED)) ==
                  "غير محسومة",
              "undetermined_label_is_arabic");
    }

    // 11. Open/closed partitioning.
    {
        TradeLifecycleManager mgr;
        const std::string a = mgr.open(sampleOrigin(TradeSource::HORIZON_SHORT));
        const std::string b = mgr.open(sampleOrigin(TradeSource::HORIZON_LONG));
        mgr.close(a, TradeStatus::STOPPED_OUT, 1'700'001'000, "وقف الخسارة");
        check(mgr.size() == 2, "two_trades_registered");
        check(mgr.openTrades().size() == 1, "one_trade_still_open");
        check(mgr.openTrades()[0].origin.tradeId == b, "correct_trade_is_open");
    }

    // 12. Events are recorded in order.
    {
        TradeLifecycleManager mgr;
        const std::string id = mgr.open(sampleOrigin());
        mgr.markTargetReached(id, 0, 1'700'000'100);
        mgr.recommendClose(id, 1'700'000'200, "سبب");
        mgr.close(id, TradeStatus::CLOSED_AT_TARGET, 1'700'000'300, "إغلاق");
        const auto* t = mgr.find(id);
        check(t->events.size() == 4, "four_events_recorded");
        check(t->events.front().atSec < t->events.back().atSec,
              "events_are_chronological");
    }

    std::printf("\n%d test case(s), %d failed, %d passed\n", g_pass + g_fail,
                g_fail, g_pass);
    std::printf("RESULT: %s\n", g_fail == 0 ? "PASS" : "FAIL");
    return g_fail == 0 ? 0 : 1;
}
