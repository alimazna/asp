// AYZR-API-TEST-001 — JSON surface tests.
//
// Verifies the read surface produces well-formed JSON with the fields the
// Android app and web observatory depend on, and that Arabic labels survive.

#include "ayzr/AyzrJson.h"

#include <cstdio>
#include <string>

using namespace aura;
using namespace aura::ayzr;

namespace {

int g_fail = 0;
int g_pass = 0;

void check(bool ok, const std::string& what) {
    if (ok) { ++g_pass; std::printf("[PASS] %s\n", what.c_str()); }
    else { ++g_fail; std::printf("[FAIL] %s\n", what.c_str()); }
}

bool contains(const std::string& hay, const std::string& needle) {
    return hay.find(needle) != std::string::npos;
}

TradeOrigin origin(TradeSource s) {
    TradeOrigin o;
    o.source = s;
    o.direction = TradeDirection::SELL;
    o.entry = 2050.0;
    o.stop = 2060.0;
    o.targets = {{2040.0, false, 0}, {2030.0, false, 0}};
    o.publishedAtSec = 1'700'000'000;
    o.entryTimeframe = "H4";
    o.contextTimeframes = {"D1"};
    o.rationale = "structure=BEARISH";
    o.engineVersion = "AYZR-ENG-001";
    o.originStrength = 1.0;
    return o;
}

}  // namespace

int main() {
    std::printf("=== AYZR JSON surface tests ===\n");

    // 1. Engines JSON.
    {
        HorizonRegistry reg;
        const std::string j = enginesJson(reg);
        check(contains(j, "\"engines\":["), "engines_array_present");
        check(contains(j, "\"id\":\"SHORT\""), "short_present");
        check(contains(j, "\"id\":\"MEDIUM\""), "medium_present");
        check(contains(j, "\"id\":\"LONG\""), "long_present");
        check(contains(j, "\"label_ar\":\"قصير المدى\""),
              "arabic_label_short");
        check(contains(j, "\"entry_timeframe\":\"M5\""),
              "short_entry_timeframe");
        check(contains(j, "\"entry_timeframe\":\"H4\""), "long_entry_timeframe");
        check(contains(j, "\"busy\":false"), "idle_horizons_reported");
    }

    // 2. Trades JSON — empty.
    {
        TradeLifecycleManager mgr;
        const std::string j = tradesJson(mgr, "all");
        check(contains(j, "\"count\":0"), "empty_trades_count_zero");
    }

    // 3. Trades JSON — populated, open filter.
    {
        TradeLifecycleManager mgr;
        const std::string id = mgr.open(origin(TradeSource::HORIZON_LONG));
        const std::string j = tradesJson(mgr, "open");
        check(contains(j, "\"count\":1"), "one_open_trade");
        check(contains(j, "\"direction_label_ar\":\"بيع\""), "sell_label_arabic");
        check(contains(j, "\"source_label_ar\":\"بعيد المدى\""),
              "long_source_label_arabic");
        check(contains(j, "\"status_label_ar\":\"بانتظار الدخول\""),
              "awaiting_entry_label_arabic");
        check(contains(j, "\"is_open\":true"), "trade_reported_open");
        check(contains(j, id), "trade_id_in_json");

        // Close it and re-check the closed filter.
        mgr.close(id, TradeStatus::STOPPED_OUT, 1'700'001'000, "وقف");
        const std::string jc = tradesJson(mgr, "closed");
        check(contains(jc, "\"count\":1"), "closed_filter_finds_it");
        check(contains(jc, "\"outcome_label_ar\":\"خاسرة\""),
              "loss_outcome_arabic");
        check(contains(jc, "\"status_label_ar\":\"تحقق وقف الخسارة\""),
              "stopped_out_label_arabic");
        const std::string jo = tradesJson(mgr, "open");
        check(contains(jo, "\"count\":0"), "no_open_trades_after_close");
    }

    // 4. Targets serialization.
    {
        TradeLifecycleManager mgr;
        const std::string id = mgr.open(origin(TradeSource::NEWS));
        mgr.markTargetReached(id, 0, 1'700'000'500);
        const std::string j = tradeDetailJson(mgr, id);
        check(contains(j, "\"reached\":true"), "reached_target_flag");
        check(contains(j, "\"reached\":false"), "pending_target_flag");
        check(contains(j, "\"events\":["), "detail_includes_events");
        check(contains(j, "أصدر المحرك"), "origin_event_arabic");
    }

    // 5. Unknown id yields a not-found body, not a crash.
    {
        TradeLifecycleManager mgr;
        const std::string j = tradeDetailJson(mgr, "nope");
        check(contains(j, "\"error\":\"not_found\""), "unknown_id_not_found");
    }

    // 6. JSON is balanced (brace/bracket count).
    {
        TradeLifecycleManager mgr;
        mgr.open(origin(TradeSource::HORIZON_SHORT));
        HorizonRegistry reg;
        const std::string a = enginesJson(reg);
        const std::string b = tradesJson(mgr, "all");
        auto balanced = [](const std::string& s) {
            int depth = 0;
            for (char c : s) {
                if (c == '{' || c == '[') ++depth;
                if (c == '}' || c == ']') --depth;
            }
            return depth == 0;
        };
        check(balanced(a), "engines_json_balanced");
        check(balanced(b), "trades_json_balanced");
    }

    std::printf("\n%d test case(s), %d failed, %d passed\n", g_pass + g_fail,
                g_fail, g_pass);
    std::printf("RESULT: %s\n", g_fail == 0 ? "PASS" : "FAIL");
    return g_fail == 0 ? 0 : 1;
}
