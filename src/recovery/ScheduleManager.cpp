// REC-0002 - Schedule manager implementation.

#include "recovery/ScheduleManager.h"

#include <ctime>

namespace aura {

namespace {
constexpr int kMinutesPerDay = 24 * 60;
}

int ScheduleManager::minuteOfDay(Timestamp now) {
    const std::time_t seconds = static_cast<std::time_t>(now.epochMillis() / 1000);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &seconds);
#else
    gmtime_r(&seconds, &tm);
#endif
    return tm.tm_hour * 60 + tm.tm_min;
}

bool ScheduleManager::dayMatches(const OperatingWindow& window, int weekday) {
    // tm_wday: 0=Sunday .. 6=Saturday
    switch (weekday) {
        case 0: return window.sunday;
        case 1: return window.monday;
        case 2: return window.tuesday;
        case 3: return window.wednesday;
        case 4: return window.thursday;
        case 5: return window.friday;
        case 6: return window.saturday;
        default: return false;
    }
}

bool ScheduleManager::minuteInWindow(const OperatingWindow& window, int minute) {
    if (window.startMinuteOfDay == window.endMinuteOfDay) return false;
    if (window.startMinuteOfDay < window.endMinuteOfDay) {
        return minute >= window.startMinuteOfDay && minute < window.endMinuteOfDay;
    }
    // Wraps past midnight.
    return minute >= window.startMinuteOfDay || minute < window.endMinuteOfDay;
}

void ScheduleManager::addWindow(const OperatingWindow& window) {
    windows_.push_back(window);
}

void ScheduleManager::clear() { windows_.clear(); }

WindowState ScheduleManager::resolve(Timestamp now) const {
    WindowState state;
    state.kind = WindowKind::CLOSED;
    state.reason = "no operating window matches; schedule is closed by default";

    const std::time_t seconds = static_cast<std::time_t>(now.epochMillis() / 1000);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &seconds);
#else
    gmtime_r(&seconds, &tm);
#endif
    const int minute = minuteOfDay(now);

    // Precedence: MAINTENANCE > CLOSED-window > RESEARCH_ONLY > TRADING.
    // A maintenance window always wins over any other match.
    for (const auto& window : windows_) {
        if (!dayMatches(window, tm.tm_wday)) continue;
        if (!minuteInWindow(window, minute)) continue;
        if (window.kind == WindowKind::MAINTENANCE) {
            state.kind = WindowKind::MAINTENANCE;
            state.label = window.label;
            state.tradingAllowed = false;
            state.researchAllowed = false;
            state.reason = "maintenance window active";
            return state;
        }
    }

    for (const auto& window : windows_) {
        if (!dayMatches(window, tm.tm_wday)) continue;
        if (!minuteInWindow(window, minute)) continue;
        state.kind = window.kind;
        state.label = window.label;
        state.tradingAllowed = window.kind == WindowKind::TRADING;
        state.researchAllowed = window.kind == WindowKind::TRADING ||
                                window.kind == WindowKind::RESEARCH_ONLY;
        state.reason = std::string(toString(window.kind)) + " window active";
        return state;
    }
    return state;
}

bool ScheduleManager::researchAllowed(Timestamp now,
                                      const ResearchBudget& budget) const {
    const WindowState state = resolve(now);
    if (!state.researchAllowed) return false;
    return !budget.status(now).exhausted;
}

}  // namespace aura
