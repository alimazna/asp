// AYZR-ECO-TEST-CAL — Market closure contiguity tests.
//
// Real gold feeds stop for the daily rollover (22:00->01:00 UTC) and over the
// weekend (Fri 22:00 -> Mon 01:00 UTC). Those absences are expected, not
// corrupted data. This suite pins the predicate so it cannot silently widen to
// "every gap is fine", and proves a genuine mid-session gap is still rejected.

#include "ayzr/engines/StrategyEngine.h"

#include <cstdio>
#include <string>

using namespace aura;
using namespace aura::ayzr::eco;

namespace {
int g_fail = 0;
int g_pass = 0;
void check(bool ok, const std::string& what) {
    if (ok) { ++g_pass; std::printf("[PASS] %s\n", what.c_str()); }
    else    { ++g_fail; std::printf("[FAIL] %s\n", what.c_str()); }
}

// Build bars at a fixed step starting at `startUtc`, skipping the given gaps.
std::vector<Bar> makeBars(std::int64_t startUtc, std::int64_t step, int count) {
    std::vector<Bar> bars;
    for (int i = 0; i < count; ++i) {
        Bar b;
        b.openTimeUtc = startUtc + static_cast<std::int64_t>(i) * step;
        b.open = b.high = b.low = b.close = 2000.0 + i;
        bars.push_back(b);
    }
    return bars;
}

// Epoch second for a UTC calendar moment (handles the well-known anchors only).
std::int64_t utc(int y, int mo, int d, int h, int mi) {
    std::tm t{};
    t.tm_year = y - 1900; t.tm_mon = mo - 1; t.tm_mday = d;
    t.tm_hour = h; t.tm_min = mi; t.tm_sec = 0;
    return static_cast<std::int64_t>(timegm(&t));
}
}  // namespace

int main() {
    const std::int64_t step = 900;  // M15

    // 1. The daily rollover gap is a scheduled closure.
    //    2026-10-29 20:45 -> 22:00 UTC (75 min).
    {
        const std::int64_t from = utc(2026, 10, 29, 20, 45);
        const std::int64_t to   = utc(2026, 10, 29, 22, 0);
        check(isScheduledClosure(from, to), "daily 22:00 rollover is a closure");
    }
    // 2. The weekend gap is a scheduled closure.
    //    2026-10-30 20:45 (Fri) -> 2026-11-01 03:00 (Sun).
    {
        const std::int64_t from = utc(2026, 10, 30, 20, 45);
        const std::int64_t to   = utc(2026, 11, 1, 3, 0);
        check(isScheduledClosure(from, to), "Fri->Sun weekend gap is a closure");
    }
    // 3. A mid-session gap is NOT a closure.
    {
        const std::int64_t from = utc(2026, 10, 29, 10, 0);
        const std::int64_t to   = utc(2026, 10, 29, 12, 0);
        check(!isScheduledClosure(from, to), "a mid-session gap is not a closure");
    }
    // 4. A gap that starts at 22:00 but ends far beyond 01:00 is not a closure
    //    (guards against the predicate swallowing an arbitrary outage).
    {
        const std::int64_t from = utc(2026, 10, 27, 22, 0);   // Tue 22:00
        const std::int64_t to   = utc(2026, 10, 28, 6, 0);    // Wed 06:00
        check(!isScheduledClosure(from, to), "an 8h outage past 01:00 is not a closure");
    }

    // 5. A series spanning a weekend with the closure present is contiguous
    //    under the closure-aware check, and the gap is reported.
    {
        std::vector<Bar> bars;
        // Thu 22:00-01:00 daily gap, then Fri session, then weekend, then Mon.
        std::int64_t t = utc(2026, 10, 30, 20, 30);  // Fri 20:30
        for (int i = 0; i < 2; ++i) { Bar b; b.openTimeUtc = t + i * step; bars.push_back(b); }
        // next bar is Mon 2026-11-02 01:00 (after the weekend close)
        Bar b; b.openTimeUtc = utc(2026, 11, 2, 1, 0); bars.push_back(b);
        for (int i = 1; i < 4; ++i) { Bar c; c.openTimeUtc = b.openTimeUtc + i * step; bars.push_back(c); }
        std::vector<GapInfo> gaps;
        check(barsContiguousAllowingClosures(bars, Timeframe::M15, &gaps),
              "weekend-spanning series is contiguous when closures are allowed");
        check(gaps.size() == 1, "the weekend closure gap is recorded once");
        check(!barsContiguous(bars, Timeframe::M15),
              "the strict check still rejects the weekend gap (unchanged semantics)");
    }

    // 6. A genuine mid-session gap still breaks contiguity even with closures.
    {
        std::vector<Bar> bars = makeBars(utc(2026, 10, 29, 10, 0), step, 3);
        bars.push_back({bars.back().openTimeUtc + 4 * step, 0, 0, 0, 0, 0, 0, false});
        std::vector<GapInfo> gaps;
        check(!barsContiguousAllowingClosures(bars, Timeframe::M15, &gaps),
              "a mid-session gap still breaks contiguity");
        check(gaps.empty(), "a mid-session gap is not recorded as a closure");
    }

    // 7. Out-of-order and duplicate bars are always rejected.
    {
        std::vector<Bar> bars = makeBars(utc(2026, 10, 29, 10, 0), step, 3);
        std::vector<Bar> dup = bars; dup[1].openTimeUtc = dup[0].openTimeUtc;
        check(!barsContiguousAllowingClosures(dup, Timeframe::M15, nullptr),
              "a duplicate open time is rejected");
        std::vector<Bar> rev = bars;
        std::swap(rev[0], rev[2]);
        check(!barsContiguousAllowingClosures(rev, Timeframe::M15, nullptr),
              "out-of-order bars are rejected");
    }

    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
