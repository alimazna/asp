#pragma once
// REC-0007 - Resource budget manager.
//
// Bounds the backend's use of machine resources (memory, disk, concurrent
// tasks). When a budget is exhausted, new work in that dimension is refused;
// the manager never silently over-commits and never disables safety checks to
// free capacity.

#include "foundation/Timestamp.h"
#include "research/ResearchBudget.h"

#include <cstdint>
#include <map>
#include <string>

namespace aura {

enum class ResourceKind {
    MEMORY_BYTES,
    DISK_BYTES,
    CONCURRENT_TASKS,
    CPU_MILLIS,
};

inline const char* toString(ResourceKind k) noexcept {
    switch (k) {
        case ResourceKind::MEMORY_BYTES:     return "MEMORY_BYTES";
        case ResourceKind::DISK_BYTES:       return "DISK_BYTES";
        case ResourceKind::CONCURRENT_TASKS: return "CONCURRENT_TASKS";
        case ResourceKind::CPU_MILLIS:       return "CPU_MILLIS";
    }
    return "UNKNOWN";
}

struct ResourceLimit {
    std::int64_t softLimit = 0;   // warn threshold
    std::int64_t hardLimit = 0;   // refuse threshold
};

struct ResourceUsage {
    std::int64_t used = 0;
    std::int64_t softLimit = 0;
    std::int64_t hardLimit = 0;
    bool softExceeded = false;
    bool hardExceeded = false;
};

class ResourceBudgetManager {
public:
    ResourceBudgetManager() = default;

    void setLimit(ResourceKind kind, std::int64_t softLimit,
                  std::int64_t hardLimit);

    // Reserve an amount. Refused (returns false) if it would breach the hard
    // limit; nothing is reserved on refusal.
    bool reserve(ResourceKind kind, std::int64_t amount);

    void release(ResourceKind kind, std::int64_t amount);

    ResourceUsage usage(ResourceKind kind) const;

    // True when every configured resource is within its soft limit.
    bool withinSoftLimits() const;

    std::map<std::string, std::string> snapshot() const;

private:
    std::map<ResourceKind, ResourceLimit> limits_;
    std::map<ResourceKind, std::int64_t> used_;
};

}  // namespace aura
