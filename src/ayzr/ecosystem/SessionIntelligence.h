#pragma once
// AYZR-MKT-001 — Session Intelligence (IANA timezone + trading sessions).
//
// Gold is an OTC instrument: it has no single central exchange bell. This module
// therefore models *reference trading sessions* (Tokyo/London/New York/Sydney and
// their overlap), each expressed in its own IANA timezone, and converts to/from
// UTC with full DST correctness by reading the system tzdata (TZif) files.
//
// No session hour is hard-coded: the open/close hours are a *declared* property
// of each session definition, and the UTC instants are computed through the
// timezone database, so a DST transition (which differs per country) is handled
// by the tzdata, not by our arithmetic.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace aura {
namespace ayzr {
namespace eco {

/// A wall-clock local time in a named IANA zone.
struct LocalTime {
    int year = 1970;
    int month = 1;      // 1..12
    int day = 1;        // 1..31
    int hour = 0;       // 0..23
    int minute = 0;     // 0..59
    int second = 0;     // 0..59
    int weekday = 0;    // 0=Sunday .. 6=Saturday (for the local date)
};

/// Result of converting an instant to a zone's local time. When the zone is
/// unknown the caller gets `available=false` and must show "غير متاح", never a
/// guessed time.
struct ZoneResult {
    bool available = false;
    LocalTime local;
    int utcOffsetSeconds = 0;   // zone offset at that instant
    bool inDst = false;         // true when a DST offset is in force
    std::string zoneId;
    std::string error;          // populated when !available
};

/// IANA timezone backed by a parsed TZif file. Immutable after load.
class TimeZone {
public:
    /// Load a zone by IANA id (e.g. "Europe/London"). Returns a zone whose
    /// `valid()` is false on failure — never throws, never guesses.
    static TimeZone load(const std::string& zoneId,
                         const std::string& tzRoot = "/usr/share/zoneinfo");

    bool valid() const noexcept { return valid_; }
    const std::string& id() const noexcept { return id_; }
    const std::string& error() const noexcept { return error_; }

    /// UTC instant -> local wall time in this zone.
    ZoneResult toLocal(std::int64_t utcSec) const;

    /// Local wall time -> UTC instant. `ambiguous` is set when a wall time falls
    /// in a DST "fall back" repeat; the earlier instant is returned.
    std::optional<std::int64_t> toUtc(const LocalTime& lt, bool* ambiguous = nullptr) const;

    /// The zone's UTC offset (seconds) at an instant.
    std::optional<int> offsetAt(std::int64_t utcSec) const;

private:
    struct Transition {
        std::int64_t at = 0;      // UTC instant of the transition
        int offset = 0;           // UTC offset after the transition
        bool dst = false;         // DST flag after the transition
    };
    std::vector<Transition> transitions_;
    // Type list, indexed by the transition's type index in the TZif file.
    std::vector<std::pair<int, bool>> types_;  // (offset, isDst)
    std::string id_;
    std::string error_;
    bool valid_ = false;
};

/// A named reference trading session, declared in its own zone.
struct SessionDef {
    std::string id;          // "tokyo", "london", "newyork", "sydney", "london_ny"
    std::string nameAr;      // Arabic display name
    std::string zoneId;      // IANA zone the hours are expressed in
    int openHour = 0;        // local hour in zoneId
    int openMinute = 0;
    int closeHour = 0;
    int closeMinute = 0;
    bool wrapsMidnight = false;   // e.g. Sydney 07:00 -> 16:00 does not; 22:00->07:00 does
};

/// The canonical session set. Hours are *reference* windows in each city's local
/// time, not an exchange bell. Documented in docs/ayzr/SESSIONS.md.
const std::vector<SessionDef>& canonicalSessions();

enum class SessionPhase {
    CLOSED,
    OPEN,
    UPCOMING,     // opens within the look-ahead window
    UNKNOWN,      // zone data unavailable
};

const char* toString(SessionPhase p) noexcept;
const char* sessionPhaseLabelAr(SessionPhase p) noexcept;

/// Live state of one session at a given instant.
struct SessionState {
    std::string id;
    std::string nameAr;
    std::string zoneId;
    SessionPhase phase = SessionPhase::UNKNOWN;
    std::optional<std::int64_t> nextOpenUtc;
    std::optional<std::int64_t> nextCloseUtc;
    std::optional<std::int64_t> secondsToChange;   // to next open or close
    ZoneResult localNow;                           // the zone's local time now
    bool inDst = false;
    std::string error;
};

/// The complete session picture at an instant, including the overlap.
struct SessionSnapshot {
    std::int64_t asOfUtc = 0;
    std::vector<SessionState> sessions;
    /// Ids of sessions currently open. Empty is a valid, honest answer.
    std::vector<std::string> openSessionIds;
    /// True when London and New York are both open (the highest-liquidity window).
    bool londonNewYorkOverlap = false;
    /// Zone data that could not be loaded, if any (never silently ignored).
    std::vector<std::string> unavailableZones;
};

/// Compute the full session snapshot. `lookAheadSec` controls how far ahead an
/// upcoming session is reported as UPCOMING (default 2 hours).
SessionSnapshot computeSessionSnapshot(
    std::int64_t nowUtcSec,
    const std::string& tzRoot = "/usr/share/zoneinfo",
    std::int64_t lookAheadSec = 7200);

/// Format a local time as "YYYY-MM-DD HH:MM:SS". Deterministic; no locale.
std::string formatLocal(const LocalTime& lt);

/// Format a duration in seconds as Arabic "س ن د" (hours/minutes) for the UI.
std::string formatCountdownAr(std::int64_t seconds);

}  // namespace eco
}  // namespace ayzr
}  // namespace aura
