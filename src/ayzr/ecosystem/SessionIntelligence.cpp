// AYZR-MKT-001 — Session Intelligence (implementation).
//
// TZif (RFC 8536) parser + session clock. C++17 ships no timezone database, so
// this reads the system tzdata directly and applies the transitions. It never
// approximates DST with a fixed rule: the transition table IS the source of
// truth, which is what makes cross-country DST differences correct.

#include "ayzr/ecosystem/SessionIntelligence.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>

namespace aura {
namespace ayzr {
namespace eco {
namespace {

// --- endian-safe readers -----------------------------------------------------
std::uint32_t rd32(const unsigned char* p) {
    return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) |
           (std::uint32_t(p[2]) << 8) | std::uint32_t(p[3]);
}
std::int32_t rdi32(const unsigned char* p) {
    return static_cast<std::int32_t>(rd32(p));
}
std::int64_t rdi64(const unsigned char* p) {
    std::uint64_t v = 0;
    for (int i = 0; i < 8; ++i) v = (v << 8) | p[i];
    return static_cast<std::int64_t>(v);
}

struct Header {
    int version = 0;
    std::uint32_t isutcnt = 0, isstdcnt = 0, leapcnt = 0;
    std::uint32_t timecnt = 0, typecnt = 0, charcnt = 0;
};

bool parseHeader(const unsigned char* p, std::size_t n, Header& h) {
    if (n < 44) return false;
    if (std::memcmp(p, "TZif", 4) != 0) return false;
    h.version = p[4] == '2' ? 2 : (p[4] == '3' ? 3 : (p[4] == '4' ? 4 : 1));
    h.isutcnt  = rd32(p + 20);
    h.isstdcnt = rd32(p + 24);
    h.leapcnt  = rd32(p + 28);
    h.timecnt  = rd32(p + 32);
    h.typecnt  = rd32(p + 36);
    h.charcnt  = rd32(p + 40);
    return true;
}

std::size_t dataBlockSize(const Header& h, int timeSize) {
    return std::size_t(h.timecnt) * (timeSize + 1) +
           std::size_t(h.typecnt) * 6 + h.charcnt +
           std::size_t(h.leapcnt) * (timeSize + 4) +
           h.isstdcnt + h.isutcnt;
}

// --- civil <-> epoch (no DST; pure calendar math) ----------------------------
bool isLeap(int y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }
int daysInMonth(int y, int m) {
    static const int d[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (m == 2 && isLeap(y)) return 29;
    return d[m - 1];
}
// Days since 1970-01-01 (Howard Hinnant's algorithm).
std::int64_t daysFromCivil(int y, unsigned m, unsigned d) {
    y -= m <= 2;
    const std::int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<std::int64_t>(doe) - 719468;
}
void civilFromDays(std::int64_t z, int& y, unsigned& m, unsigned& d) {
    z += 719468;
    const std::int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = static_cast<unsigned>(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    y = static_cast<int>(yoe) + static_cast<int>(era) * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    d = doy - (153 * mp + 2) / 5 + 1;
    m = mp + (mp < 10 ? 3 : -9);
    y += (m <= 2);
}

std::int64_t localToPseudoUtc(const LocalTime& lt) {
    std::int64_t days = daysFromCivil(lt.year, static_cast<unsigned>(lt.month),
                                      static_cast<unsigned>(lt.day));
    return days * 86400 + lt.hour * 3600 + lt.minute * 60 + lt.second;
}

LocalTime pseudoUtcToLocal(std::int64_t sec) {
    std::int64_t days = sec / 86400;
    std::int64_t rem = sec % 86400;
    if (rem < 0) { rem += 86400; --days; }
    LocalTime lt;
    unsigned m, d;
    civilFromDays(days, lt.year, m, d);
    lt.month = static_cast<int>(m);
    lt.day = static_cast<int>(d);
    lt.hour = static_cast<int>(rem / 3600);
    lt.minute = static_cast<int>((rem % 3600) / 60);
    lt.second = static_cast<int>(rem % 60);
    std::int64_t wd = (days + 4) % 7;   // 1970-01-01 was a Thursday
    if (wd < 0) wd += 7;
    lt.weekday = static_cast<int>(wd);
    return lt;
}

}  // namespace

TimeZone TimeZone::load(const std::string& zoneId, const std::string& tzRoot) {
    TimeZone z;
    z.id_ = zoneId;
    // Reject path traversal: an IANA id is a slash-separated name of
    // [A-Za-z0-9_+-] components, never "..".
    if (zoneId.empty() || zoneId.find("..") != std::string::npos ||
        zoneId.front() == '/') {
        z.error_ = "invalid zone id";
        return z;
    }
    std::ifstream f(tzRoot + "/" + zoneId, std::ios::binary);
    if (!f) { z.error_ = "zone file not found"; return z; }
    std::vector<unsigned char> buf((std::istreambuf_iterator<char>(f)),
                                   std::istreambuf_iterator<char>());
    Header h1{};
    if (!parseHeader(buf.data(), buf.size(), h1)) { z.error_ = "bad TZif header"; return z; }

    const unsigned char* p = buf.data();
    std::size_t off = 44;
    int timeSize = 4;
    Header h = h1;
    // A v2+ file has a second header + 64-bit data block after the v1 block.
    if (h1.version >= 2) {
        off += dataBlockSize(h1, 4);
        if (off + 44 > buf.size()) { z.error_ = "truncated TZif v2"; return z; }
        Header h2{};
        if (!parseHeader(buf.data() + off, buf.size() - off, h2)) {
            z.error_ = "bad TZif v2 header"; return z;
        }
        h = h2;
        off += 44;
        timeSize = 8;
    }
    if (h.typecnt == 0) { z.error_ = "no time types"; return z; }

    // Transition times.
    std::vector<std::int64_t> ttimes(h.timecnt);
    for (std::uint32_t i = 0; i < h.timecnt; ++i) {
        if (off + timeSize > buf.size()) { z.error_ = "truncated transitions"; return z; }
        ttimes[i] = timeSize == 8 ? rdi64(p + off) : rdi32(p + off);
        off += timeSize;
    }
    // Type indices.
    std::vector<unsigned char> tidx(h.timecnt);
    for (std::uint32_t i = 0; i < h.timecnt; ++i) {
        if (off >= buf.size()) { z.error_ = "truncated indices"; return z; }
        tidx[i] = p[off++];
    }
    // Types.
    z.types_.resize(h.typecnt);
    for (std::uint32_t i = 0; i < h.typecnt; ++i) {
        if (off + 6 > buf.size()) { z.error_ = "truncated types"; return z; }
        int off_sec = rdi32(p + off);
        bool dst = p[off + 4] != 0;
        z.types_[i] = {off_sec, dst};
        off += 6;
    }
    // Designations + leap + std/ut flags are skipped: we do not need them.
    for (std::uint32_t i = 0; i < h.timecnt; ++i) {
        unsigned char ti = tidx[i];
        if (ti >= z.types_.size()) { z.error_ = "bad type index"; return z; }
        z.transitions_.push_back({ttimes[i], z.types_[ti].first, z.types_[ti].second});
    }
    z.valid_ = true;
    return z;
}

std::optional<int> TimeZone::offsetAt(std::int64_t utcSec) const {
    if (!valid_) return std::nullopt;
    if (transitions_.empty()) {
        // No transitions: a single fixed type applies.
        return types_.empty() ? std::optional<int>{} : std::optional<int>(types_[0].first);
    }
    // Find the last transition with at <= utcSec.
    auto it = std::upper_bound(
        transitions_.begin(), transitions_.end(), utcSec,
        [](std::int64_t v, const Transition& t) { return v < t.at; });
    if (it == transitions_.begin()) {
        // Before the first transition: the pre-first type is unknown from the
        // table; report the first type's offset (tzdata guarantees a sane value).
        return transitions_.front().offset;
    }
    --it;
    return it->offset;
}

ZoneResult TimeZone::toLocal(std::int64_t utcSec) const {
    ZoneResult r;
    r.zoneId = id_;
    if (!valid_) { r.error = error_; return r; }
    auto off = offsetAt(utcSec);
    if (!off.has_value()) { r.error = "no offset available"; return r; }
    r.available = true;
    r.utcOffsetSeconds = *off;
    // DST flag from the governing transition.
    r.inDst = false;
    if (!transitions_.empty()) {
        auto it = std::upper_bound(
            transitions_.begin(), transitions_.end(), utcSec,
            [](std::int64_t v, const Transition& t) { return v < t.at; });
        if (it != transitions_.begin()) { --it; r.inDst = it->dst; }
    }
    r.local = pseudoUtcToLocal(utcSec + *off);
    return r;
}

std::optional<std::int64_t> TimeZone::toUtc(const LocalTime& lt, bool* ambiguous) const {
    if (ambiguous) *ambiguous = false;
    if (!valid_) return std::nullopt;
    const std::int64_t pseudo = localToPseudoUtc(lt);
    // Candidate offsets: every distinct offset in the type table.
    std::vector<int> cands;
    for (const auto& t : types_) {
        if (std::find(cands.begin(), cands.end(), t.first) == cands.end())
            cands.push_back(t.first);
    }
    std::vector<std::int64_t> hits;
    for (int o : cands) {
        std::int64_t utc = pseudo - o;
        auto back = offsetAt(utc);
        if (back.has_value() && *back == o) hits.push_back(utc);
    }
    if (hits.empty()) return std::nullopt;   // DST spring-forward gap
    std::sort(hits.begin(), hits.end());
    if (hits.size() > 1 && ambiguous) *ambiguous = true;
    return hits.front();   // earlier instant on a fall-back repeat
}

const std::vector<SessionDef>& canonicalSessions() {
    // Reference windows in each city's local time. Sydney wraps midnight.
    static const std::vector<SessionDef> k = {
        {"sydney",   "جلسة سيدني",      "Australia/Sydney", 7,  0, 16, 0, false},
        {"tokyo",    "جلسة طوكيو",      "Asia/Tokyo",       9,  0, 18, 0, false},
        {"london",   "جلسة لندن",       "Europe/London",    8,  0, 17, 0, false},
        {"newyork",  "جلسة نيويورك",    "America/New_York", 8,  0, 17, 0, false},
    };
    return k;
}

const char* toString(SessionPhase p) noexcept {
    switch (p) {
        case SessionPhase::CLOSED:  return "CLOSED";
        case SessionPhase::OPEN:    return "OPEN";
        case SessionPhase::UPCOMING:return "UPCOMING";
        case SessionPhase::UNKNOWN: return "UNKNOWN";
    }
    return "UNKNOWN";
}
const char* sessionPhaseLabelAr(SessionPhase p) noexcept {
    switch (p) {
        case SessionPhase::CLOSED:  return "مغلقة";
        case SessionPhase::OPEN:    return "مفتوحة";
        case SessionPhase::UPCOMING:return "قادمة";
        case SessionPhase::UNKNOWN: return "غير متاح";
    }
    return "غير متاح";
}

namespace {

// Is the local wall-clock time inside [open, close) for a session?
bool localInWindow(const LocalTime& lt, const SessionDef& s, int& minsNow,
                   int& minsOpen, int& minsClose) {
    minsNow = lt.hour * 60 + lt.minute;
    minsOpen = s.openHour * 60 + s.openMinute;
    minsClose = s.closeHour * 60 + s.closeMinute;
    if (!s.wrapsMidnight) {
        return minsNow >= minsOpen && minsNow < minsClose;
    }
    return minsNow >= minsOpen || minsNow < minsClose;
}

}  // namespace

SessionSnapshot computeSessionSnapshot(std::int64_t nowUtcSec,
                                       const std::string& tzRoot,
                                       std::int64_t lookAheadSec) {
    SessionSnapshot snap;
    snap.asOfUtc = nowUtcSec;
    bool londonOpen = false, nyOpen = false;
    for (const SessionDef& def : canonicalSessions()) {
        SessionState st;
        st.id = def.id;
        st.nameAr = def.nameAr;
        st.zoneId = def.zoneId;
        TimeZone tz = TimeZone::load(def.zoneId, tzRoot);
        if (!tz.valid()) {
            st.phase = SessionPhase::UNKNOWN;
            st.error = tz.error();
            snap.unavailableZones.push_back(def.zoneId + ": " + tz.error());
            snap.sessions.push_back(std::move(st));
            continue;
        }
        ZoneResult now = tz.toLocal(nowUtcSec);
        st.localNow = now;
        st.inDst = now.inDst;
        int minsNow = 0, minsOpen = 0, minsClose = 0;
        const bool open = localInWindow(now.local, def, minsNow, minsOpen, minsClose);
        if (open) {
            st.phase = SessionPhase::OPEN;
            if (def.id == "london") londonOpen = true;
            if (def.id == "newyork") nyOpen = true;
            snap.openSessionIds.push_back(def.id);
            // Seconds until close: build the local close time today.
            LocalTime closeLt = now.local;
            closeLt.hour = def.closeHour;
            closeLt.minute = def.closeMinute;
            closeLt.second = 0;
            if (def.wrapsMidnight && minsClose <= minsNow) {
                closeLt.day += 1;   // close is tomorrow
            }
            // Normalise an overflowed day via the calendar helpers.
            std::int64_t pseudo = localToPseudoUtc(closeLt);
            LocalTime norm = pseudoUtcToLocal(pseudo);
            closeLt = norm;
            bool amb = false;
            auto closeUtc = tz.toUtc(closeLt, &amb);
            if (closeUtc.has_value()) {
                st.nextCloseUtc = *closeUtc;
                st.secondsToChange = *closeUtc - nowUtcSec;
            }
        } else {
            // Compute the next open instant by scanning forward up to 48h.
            st.phase = SessionPhase::CLOSED;
            for (std::int64_t probe = 0; probe <= 48 * 3600; probe += 60) {
                std::int64_t t = nowUtcSec + probe;
                ZoneResult lz = tz.toLocal(t);
                if (!lz.available) break;
                int a = 0, b = 0, c = 0;
                if (localInWindow(lz.local, def, a, b, c)) {
                    st.nextOpenUtc = t;
                    st.secondsToChange = t - nowUtcSec;
                    if (probe <= lookAheadSec) st.phase = SessionPhase::UPCOMING;
                    break;
                }
            }
        }
        snap.sessions.push_back(std::move(st));
    }
    snap.londonNewYorkOverlap = londonOpen && nyOpen;
    return snap;
}

std::string formatLocal(const LocalTime& lt) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d",
                  lt.year, lt.month, lt.day, lt.hour, lt.minute, lt.second);
    return buf;
}

std::string formatCountdownAr(std::int64_t seconds) {
    if (seconds < 0) seconds = 0;
    std::int64_t h = seconds / 3600;
    std::int64_t m = (seconds % 3600) / 60;
    char buf[64];
    if (h > 0) std::snprintf(buf, sizeof(buf), "%lld س %lld د",
                             static_cast<long long>(h), static_cast<long long>(m));
    else       std::snprintf(buf, sizeof(buf), "%lld د",
                             static_cast<long long>(m));
    return buf;
}

}  // namespace eco
}  // namespace ayzr
}  // namespace aura
