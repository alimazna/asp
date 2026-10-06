#pragma once
// FND-0010 - Frozen foundational contract: message envelope metadata.

#include "foundation/EntityId.h"
#include "foundation/ProtocolVersion.h"
#include "foundation/SchemaVersion.h"
#include "foundation/Timestamp.h"

#include <string>

namespace aura {

struct MessageMetadata {
    EntityId requestId;
    EntityId correlationId;
    ProtocolVersion protocolVersion;
    SchemaVersion schemaVersion;
    Timestamp eventTime;      // when the described event occurred (source)
    Timestamp receiveTime;    // when AURA received it
    Timestamp processTime;    // when AURA processed it
    std::string source;       // producing component/boundary identity

    bool hasValidTimeOrder() const noexcept {
        if (eventTime.isUnknown() || receiveTime.isUnknown()) return false;
        if (eventTime > receiveTime) return false;  // no future-dated events
        if (processTime.isKnown() && processTime < receiveTime) return false;
        return true;
    }
};

}  // namespace aura
