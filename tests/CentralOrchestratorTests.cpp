// AYZR-ORC-TEST-001 — Central Orchestrator tests.
//
// Proves the orchestrator's core promises: deterministic opportunity identity,
// exact deduplication, real conflict detection, quality-gated decisions, and an
// append-only ledger that records every step. No majority voting is tested for
// by showing a single high-weight engine can outrank two low-weight ones.

#include "ayzr/ecosystem/CentralOrchestrator.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace aura;
using namespace aura::ayzr::eco;

namespace {
int g_fail = 0;
int g_pass = 0;
void check(bool ok, const std::string& what) {
    if (ok) { ++g_pass; std::printf("[PASS] %s\n", what.c_str()); }
    else    { ++g_fail; std::printf("[FAIL] %s\n", what.c_str()); }
}
constexpr std::int64_t kT = 1700000000;

EngineResult ev(const std::string& engineId, StrategyFamily fam,
                EngineDirection dir, DataQualityState q = DataQualityState::VALID,
                EventStatus st = EventStatus::PENDING_CONFIRMATION,
                std::int64_t anchor = kT) {
    EngineResult e;
    e.engineId = engineId;
    e.strategyFamily = fam;
    e.symbol = "XAUUSD";
    e.timeframe = Timeframe::M15;
    e.dataTimestamp = anchor;
    e.direction = dir;
    e.dataQuality = q;
    e.status = st;
    e.createdAt = kT; e.updatedAt = kT;
    e.eventId = makeEventId(engineId, "XAUUSD", Timeframe::M15, anchor);
    if (dir != EngineDirection::NO_TRADE) {
        e.entry = PriceLevel{2000.0, "engine_last_close", ""};
        e.stopLoss = PriceLevel{1990.0, "engine_atr", ""};
        e.takeProfitLevels.push_back(PriceLevel{2020.0, "engine_atr", ""});
    }
    return e;
}
}  // namespace

int main() {
    // ---- 1. Opportunity id is deterministic and identity-sensitive ----------
    const std::string idA = CentralOrchestrator::makeOpportunityId(
        "XAUUSD", Timeframe::M15, EngineDirection::BULLISH, kT);
    const std::string idB = CentralOrchestrator::makeOpportunityId(
        "XAUUSD", Timeframe::M15, EngineDirection::BULLISH, kT);
    check(idA == idB, "opportunity id is deterministic");
    check(idA != CentralOrchestrator::makeOpportunityId(
              "XAUUSD", Timeframe::H1, EngineDirection::BULLISH, kT),
          "different timeframe => different opportunity id");
    check(idA.rfind("op-", 0) == 0, "opportunity id carries the op- prefix");

    // ---- 2. Deduplication: same event id twice => one contribution ----------
    {
        DecisionLedger led;
        CentralOrchestrator orc(&led);
        std::vector<EngineResult> evs = {
            ev("trend-following-M15", StrategyFamily::TREND_FOLLOWING,
               EngineDirection::BULLISH),
            ev("trend-following-M15", StrategyFamily::TREND_FOLLOWING,
               EngineDirection::BULLISH),   // exact duplicate
        };
        auto cyc = orc.runCycle(evs, kT, "c1");
        check(cyc.opportunities.size() == 1, "duplicate events form one opportunity");
        if (!cyc.opportunities.empty())
            check(cyc.opportunities[0].contributingEvents.size() == 1,
                  "the duplicate collapses to a single contribution");
    }

    // ---- 3. Conflict detection ---------------------------------------------
    {
        DecisionLedger led;
        CentralOrchestrator orc(&led);
        std::vector<EngineResult> evs = {
            ev("trend-following-M15", StrategyFamily::TREND_FOLLOWING,
               EngineDirection::BULLISH),
            ev("mean-reversion-M15", StrategyFamily::MEAN_REVERSION,
               EngineDirection::BEARISH),
        };
        auto cyc = orc.runCycle(evs, kT, "c2");
        check(cyc.opportunities.size() == 1,
              "opposing events on one setup land in ONE opportunity");
        if (!cyc.opportunities.empty()) {
            check(cyc.opportunities[0].hasConflict, "conflict is detected");
            check(!cyc.opportunities[0].conflictNotesAr.empty(),
                  "conflict carries an Arabic note");
            check(cyc.opportunities[0].state == DecisionState::PUBLISH_WITH_CAUTION ||
                      cyc.opportunities[0].state == DecisionState::WATCH,
                  "a conflict downgrades the decision (never a clean PUBLISH)");
        }
        check(led.countOf(LedgerEventType::CONFLICT_DETECTED) == 1,
              "the conflict is recorded in the ledger");
    }

    // ---- 4. No majority voting: one ASTRA core outranks two weak engines ----
    {
        DecisionLedger led;
        CentralOrchestrator orc(&led);
        std::vector<EngineResult> evs = {
            ev("astra-core", StrategyFamily::ASTRA_CORE, EngineDirection::BULLISH),
            ev("micro-a", StrategyFamily::MICROSTRUCTURE, EngineDirection::BEARISH),
            ev("micro-b", StrategyFamily::MICROSTRUCTURE, EngineDirection::BEARISH),
        };
        auto cyc = orc.runCycle(evs, kT, "c3");
        check(!cyc.opportunities.empty(), "a cycle produces an opportunity");
        if (!cyc.opportunities.empty()) {
            // ASTRA weight 1.5 > 0.5+0.5=1.0, so BULLISH wins despite 1-vs-2.
            check(cyc.opportunities[0].direction == EngineDirection::BULLISH,
                  "weighted evidence beats simple majority");
            check(cyc.opportunities[0].astraCorePresent,
                  "the frozen ASTRA core contribution is flagged");
        }
    }

    // ---- 5. Data-quality gate forces WITHHOLD ------------------------------
    {
        DecisionLedger led;
        CentralOrchestrator orc(&led);
        std::vector<EngineResult> evs = {
            ev("astra-core", StrategyFamily::ASTRA_CORE, EngineDirection::BULLISH,
               DataQualityState::DEGRADED),
        };
        auto cyc = orc.runCycle(evs, kT, "c4");
        check(!cyc.opportunities.empty() &&
                  cyc.opportunities[0].state == DecisionState::WITHHOLD,
              "non-decision-grade data forces WITHHOLD");
        check(!cyc.dataQualityWarnings.empty(),
              "the cycle surfaces a data-quality warning");
    }

    // ---- 6. A clean strong agreement publishes, with levels & provenance ----
    {
        DecisionLedger led;
        CentralOrchestrator orc(&led);
        std::vector<EngineResult> evs = {
            ev("astra-core", StrategyFamily::ASTRA_CORE, EngineDirection::BULLISH),
            ev("trend-following-M15", StrategyFamily::TREND_FOLLOWING,
               EngineDirection::BULLISH),
        };
        auto cyc = orc.runCycle(evs, kT, "c5");
        check(!cyc.opportunities.empty() &&
                  cyc.opportunities[0].state == DecisionState::PUBLISH,
              "strong agreement publishes");
        if (!cyc.opportunities.empty()) {
            check(cyc.opportunities[0].entry.has_value() &&
                      cyc.opportunities[0].stopLoss.has_value(),
                  "a published opportunity carries entry and stop");
            check(!cyc.opportunities[0].levelSourceEngine.empty(),
                  "the level source engine is named (provenance kept)");
            check(cyc.opportunities[0].direction == EngineDirection::BULLISH,
                  "the published direction is the agreed one");
        }
    }

    // ---- 7. NO_TRADE-only cycle yields no opportunity ----------------------
    {
        DecisionLedger led;
        CentralOrchestrator orc(&led);
        std::vector<EngineResult> evs = {
            ev("trend-following-M15", StrategyFamily::TREND_FOLLOWING,
               EngineDirection::NO_TRADE, DataQualityState::VALID,
               EventStatus::WITHHELD),
        };
        auto cyc = orc.runCycle(evs, kT, "c6");
        check(cyc.opportunities.empty(), "a NO_TRADE-only cycle creates no opportunity");
    }

    // ---- 8. Ledger is append-only and grows monotonically ------------------
    {
        DecisionLedger led;
        CentralOrchestrator orc(&led);
        const std::size_t before = led.size();
        std::vector<EngineResult> evs = {
            ev("astra-core", StrategyFamily::ASTRA_CORE, EngineDirection::BULLISH)};
        orc.runCycle(evs, kT, "c7");
        const std::size_t after = led.size();
        check(after > before, "a cycle appends ledger rows");
        auto rows = led.recent(0);
        bool monotone = true;
        for (std::size_t i = 1; i < rows.size(); ++i)
            if (rows[i].seq <= rows[i - 1].seq) monotone = false;
        check(monotone, "ledger sequence numbers are strictly increasing");
        check(led.countOf(LedgerEventType::DECISION_PUBLISHED) == 1,
              "the publish decision is recorded once");
    }

    // ---- 9. Restart safety: a persistent ledger keeps its rows -------------
    {
        const std::string path = "/tmp/ayzr_ledger_test.sqlite";
        std::remove(path.c_str());
        {
            DecisionLedger led;
            check(led.open(path), "ledger opens a SQLite file");
            check(led.persistent(), "the opened ledger is persistent");
            CentralOrchestrator orc(&led);
            std::vector<EngineResult> evs = {
                ev("astra-core", StrategyFamily::ASTRA_CORE, EngineDirection::BULLISH)};
            orc.runCycle(evs, kT, "persist-1");
        }
        {
            DecisionLedger led2;
            check(led2.open(path), "ledger reopens the existing file");
            check(led2.size() > 0, "rows survive a close/reopen (no data loss)");
        }
        std::remove(path.c_str());
    }

    std::printf("RESULT: %s (%d passed, %d failed)\n",
                g_fail == 0 ? "PASS" : "FAIL", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
