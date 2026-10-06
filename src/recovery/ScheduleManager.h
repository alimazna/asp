#pragma once
// REC-0001 - Operating-window / schedule manager.
//
// Defines when the backend may operate (trading sessions, maintenance windows,
// research-only windows) and answers "may we act now?" deterministically. The
// schedule never widens the research budget; it can only further restrict.

#include "foundation/Timestamp.h"
#include "research/ResearchBudget.h"

#include <string>
#include <vector>

namespace aura {

enum class WindowKind {
    TRADING,
    RESEARCH_ONLY,
    MAINTENANCE,
    CLOSED,
};

inline const char* toString(WindowKind k) noexcept {
    switch (k) {
        case WindowKind::TRADING:       return "TRADING";
        case WindowKind::RESEARCH_ONLY: return "RESEARCH_ONLY";
        case WindowKind::MAINTENANCE:   return "MAINTENANCE";
        case WindowKind::CLOSED:        return "CLOSED";
    }
    return "CLOSED";
}

struct OperatingWindow {
    WindowKind kind = WindowKind::CLOSED;
    std::string label;
    int startMinuteOfDay = 0;    // inclusive, UTC
    int endMinuteOfDay = 0;      // exclusive, UTC; may wrap past midnight
    bool monday = false;
    bool tuesday = false;
    bool wednesday = false;
    bool thursday = false;
    bool friday = false;
    bool saturday = false;
    bool sunday = false;
};

struct WindowState {
    WindowKind kind = WindowKind::CLOSED;
    std::string label;
    bool tradingAllowed = false;
    bool researchAllowed = false;
    std::string reason;
};

class ScheduleManager {
public:
    ScheduleManager() = default;

    void addWindow(const OperatingWindow& window);
    void clear();

    // Resolve the active window for a moment. When no window matches, the
    // schedule is CLOSED (deny by default).
    WindowState resolve(Timestamp now) const;

    // Research is additionally bounded by the supplied budget.
    bool researchAllowed(Timestamp now, const ResearchBudget& budget) const;

    std::size_t windowCount() const noexcept { return windows_.size(); }

private:
    static int minuteOfDay(Timestamp now);
    static bool dayMatches(const OperatingWindow& window, int weekday);
    static bool minuteInWindow(const OperatingWindow& window, int minute);

    std::vector<OperatingWindow> windows_;
};

}  // namespace aura
