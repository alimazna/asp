// DEC-0021 - Macro context implementation.

#include "macro/MacroContextEngine.h"

namespace aura {

void MacroContextEngine::setEvents(const std::vector<MacroEvent>& events) {
    events_ = events;
}

MacroContext MacroContextEngine::evaluate(Timestamp now) const {
    MacroContext context;
    context.upcoming = events_;

    // No feed configured means the context cannot be asserted clear.
    if (events_.empty() || now.isUnknown()) {
        context.quality = DataQualityState::UNKNOWN;
        context.valid = false;
        context.detail = "no macro feed available; event window unconfirmed";
        return context;
    }

    for (const auto& event : events_) {
        if (event.scheduledAt.isUnknown()) continue;
        const std::int64_t windowBefore = event.windowBeforeMinutes * 60 * 1000;
        const std::int64_t windowAfter = event.windowAfterMinutes * 60 * 1000;
        const std::int64_t delta = now.epochMillis() - event.scheduledAt.epochMillis();
        if (delta >= -windowBefore && delta <= windowAfter) {
            context.inEventWindow = true;
            context.activeEvent = event.name;
            if (event.impact >= 3) context.highImpactImminent = true;
        }
    }

    context.valid = true;
    context.quality = DataQualityState::VALID;
    context.detail = context.inEventWindow ? "within event window" : "clear of events";
    return context;
}

}  // namespace aura
