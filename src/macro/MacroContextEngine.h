#pragma once
// DEC-0020 - Macro / event boundary engine.
//
// Captures macro/event conditions that gate or suppress decisions. With no
// external calendar feed available, the context is UNKNOWN by default and
// callers must treat UNKNOWN as "not confirmed clear".

#include "foundation/DataQualityState.h"
#include "foundation/Timestamp.h"

#include <string>
#include <vector>

namespace aura {

struct MacroEvent {
    std::string name;
    Timestamp scheduledAt;
    std::int64_t impact = 0;      // 0 none, 1 low, 2 medium, 3 high
    std::int64_t windowBeforeMinutes = 30;
    std::int64_t windowAfterMinutes = 30;
};

struct MacroContext {
    bool inEventWindow = false;
    bool highImpactImminent = false;
    std::string activeEvent;
    std::vector<MacroEvent> upcoming;
    DataQualityState quality = DataQualityState::UNKNOWN;
    bool valid = false;
    std::string detail;
};

class MacroContextEngine {
public:
    MacroContextEngine() = default;

    void setEvents(const std::vector<MacroEvent>& events);

    MacroContext evaluate(Timestamp now) const;

private:
    std::vector<MacroEvent> events_;
};

}  // namespace aura
