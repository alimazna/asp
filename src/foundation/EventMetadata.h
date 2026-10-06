#pragma once
// FND-0013 - Frozen foundational contract: event metadata envelope.

#include "foundation/EntityId.h"
#include "foundation/EventId.h"
#include "foundation/EventType.h"
#include "foundation/SchemaVersion.h"
#include "foundation/Timestamp.h"

#include <cstdint>
#include <string>

namespace aura {

struct EventMetadata {
    EventId eventId;
    EventType type = EventType::UNKNOWN;
    SchemaVersion schemaVersion;
    Timestamp eventTime;
    Timestamp receiveTime;
    std::uint64_t sequence = 0;   // monotonic per producer stream
    EntityId producer;
    std::string source;

    bool isWellFormed() const noexcept {
        return !eventId.empty() && type != EventType::UNKNOWN &&
               !producer.empty() && eventTime.isKnown();
    }
};

}  // namespace aura
