// AYZR-MKT-TEST-001 — Session Intelligence tests.
//
// The expected UTC offsets and local times below were independently produced
// with Python's zoneinfo (IANA tzdata), so this is a real cross-check of the
// C++ TZif reader, not a restatement of its own arithmetic.

#include "ayzr/ecosystem/SessionIntelligence.h"

#include <cstdio>
#include <string>

using namespace aura::ayzr::eco;

namespace {
int g_fail = 0;
int g_pass = 0;
void check(bool ok, const std::string& what) {
    if (ok) { ++g_pass; std::printf("[PASS] %s\n", what.c_str()); }
    else    { ++g_fail; std::printf("[FAIL] %s\n", what.c_str()); }
}
}  // namespace

int main() {
    // Epoch seconds for the probe instants (computed with Python zoneinfo/datetime).
    const std::int64_t t2024_07_15_12 = 1721044800;  // 2024-07-15T12:00:00Z
    const std::int64_t t2024_01_15_12 = 1705320000;  // 2024-01-15T12:00:00Z

    // --- 1. New York DST: -05:00 winter, -04:00 summer ---
    TimeZone ny = TimeZone::load("America/New_York");
    check(ny.valid(), "America/New_York loads");
    check(ny.offsetAt(t2024_01_15_12) == -18000, "NY winter offset is -05:00");
    check(ny.offsetAt(t2024_07_15_12) == -14400, "NY summer offset is -04:00");
    {
        auto l = ny.toLocal(t2024_07_15_12);
        check(l.available && l.local.hour == 8 && l.local.minute == 0,
              "NY local time at 12:00Z is 08:00");
        check(l.inDst, "NY summer instant is flagged DST");
        auto w = ny.toLocal(t2024_01_15_12);
        check(!w.inDst, "NY winter instant is not DST");
    }

    // --- 2. London DST: +00:00 winter, +01:00 summer ---
    TimeZone ldn = TimeZone::load("Europe/London");
    check(ldn.valid(), "Europe/London loads");
    check(ldn.offsetAt(t2024_01_15_12) == 0, "London winter offset is +00:00");
    check(ldn.offsetAt(t2024_07_15_12) == 3600, "London summer offset is +01:00");
    {
        auto l = ldn.toLocal(t2024_07_15_12);
        check(l.available && l.local.hour == 13, "London local at 12:00Z is 13:00");
    }

    // --- 3. Tokyo has no DST: +09:00 year round ---
    TimeZone tok = TimeZone::load("Asia/Tokyo");
    check(tok.valid(), "Asia/Tokyo loads");
    check(tok.offsetAt(t2024_01_15_12) == 32400, "Tokyo winter offset is +09:00");
    check(tok.offsetAt(t2024_07_15_12) == 32400, "Tokyo summer offset is +09:00");
    {
        auto l = tok.toLocal(t2024_07_15_12);
        check(l.available && l.local.hour == 21, "Tokyo local at 12:00Z is 21:00");
        check(!l.inDst, "Tokyo is never in DST");
    }

    // --- 4. Sydney is southern-hemisphere: DST in January, none in July ---
    TimeZone syd = TimeZone::load("Australia/Sydney");
    check(syd.valid(), "Australia/Sydney loads");
    check(syd.offsetAt(t2024_01_15_12) == 39600, "Sydney January offset is +11:00 (DST)");
    check(syd.offsetAt(t2024_07_15_12) == 36000, "Sydney July offset is +10:00 (no DST)");

    // --- 5. DST transition instants (US spring-forward 2024-03-10 07:00Z) ---
    check(ny.offsetAt(1710054000 - 1) == -18000, "NY just before spring-forward is -05:00");
    check(ny.offsetAt(1710054000) == -14400, "NY at spring-forward flips to -04:00");
    // EU spring-forward 2024-03-31 01:00Z
    check(ldn.offsetAt(1711846800 - 1) == 0, "London just before EU spring-forward is +00:00");
    check(ldn.offsetAt(1711846800) == 3600, "London at EU spring-forward flips to +01:00");

    // --- 6. Unknown zone is refused, never guessed ---
    TimeZone bad = TimeZone::load("Not/AZone");
    check(!bad.valid(), "an unknown zone id is invalid");
    check(!bad.toLocal(t2024_07_15_12).available, "an invalid zone yields unavailable local time");
    check(!TimeZone::load("../../etc/passwd").valid(), "path traversal in a zone id is refused");

    // --- 7. Session snapshot: London+NY overlap at 12:00Z in summer ---
    SessionSnapshot snap = computeSessionSnapshot(t2024_07_15_12);
    check(snap.unavailableZones.empty(), "all four session zones load");
    check(snap.sessions.size() == 4, "four sessions reported");
    bool londonOpen = false, nyOpen = false;
    for (const auto& s : snap.sessions) {
        if (s.id == "london" && s.phase == SessionPhase::OPEN) londonOpen = true;
        if (s.id == "newyork" && s.phase == SessionPhase::OPEN) nyOpen = true;
    }
    check(londonOpen, "London session is open at 13:00 local");
    check(nyOpen, "New York session is open at 08:00 local");
    check(snap.londonNewYorkOverlap, "London-New York overlap is detected");
    for (const auto& s : snap.sessions) {
        if (s.phase == SessionPhase::OPEN) {
            check(s.secondsToChange.has_value() && *s.secondsToChange > 0,
                  std::string(s.id) + " open session reports a positive countdown");
        }
    }

    // --- 8. Closed sessions still get a next-open instant ---
    for (const auto& s : snap.sessions) {
        if (s.phase != SessionPhase::OPEN) {
            check(s.nextOpenUtc.has_value(),
                  std::string(s.id) + " closed session reports a next open");
        }
    }

    // --- 9. Countdown formatting is Arabic and non-empty ---
    check(formatCountdownAr(3600).find("س") != std::string::npos,
          "hour countdown uses the Arabic hour unit");
    check(formatCountdownAr(120).find("د") != std::string::npos,
          "minute countdown uses the Arabic minute unit");
    check(formatCountdownAr(-5) == "0 د", "a negative countdown clamps to zero");

    std::printf("RESULT: %s (%d passed, %d failed)\n",
                g_fail == 0 ? "PASS" : "FAIL", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
