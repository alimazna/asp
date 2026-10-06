// REC-0008 - Resource budget manager implementation.

#include "recovery/ResourceBudgetManager.h"

#include <algorithm>

namespace aura {

void ResourceBudgetManager::setLimit(ResourceKind kind, std::int64_t softLimit,
                                     std::int64_t hardLimit) {
    ResourceLimit limit;
    limit.softLimit = softLimit;
    limit.hardLimit = hardLimit;
    limits_[kind] = limit;
}

bool ResourceBudgetManager::reserve(ResourceKind kind, std::int64_t amount) {
    if (amount <= 0) return true;
    auto it = limits_.find(kind);
    if (it == limits_.end()) return false;   // no limit configured: deny
    const std::int64_t current = used_[kind];
    if (current + amount > it->second.hardLimit) return false;
    used_[kind] = current + amount;
    return true;
}

void ResourceBudgetManager::release(ResourceKind kind, std::int64_t amount) {
    if (amount <= 0) return;
    auto it = used_.find(kind);
    if (it == used_.end()) return;
    it->second = std::max<std::int64_t>(0, it->second - amount);
}

ResourceUsage ResourceBudgetManager::usage(ResourceKind kind) const {
    ResourceUsage usage;
    usage.used = used_.count(kind) ? used_.at(kind) : 0;
    auto it = limits_.find(kind);
    if (it != limits_.end()) {
        usage.softLimit = it->second.softLimit;
        usage.hardLimit = it->second.hardLimit;
        usage.softExceeded = usage.used > usage.softLimit;
        usage.hardExceeded = usage.used > usage.hardLimit;
    }
    return usage;
}

bool ResourceBudgetManager::withinSoftLimits() const {
    for (const auto& kv : limits_) {
        const std::int64_t used = used_.count(kv.first) ? used_.at(kv.first) : 0;
        if (used > kv.second.softLimit) return false;
    }
    return true;
}

std::map<std::string, std::string> ResourceBudgetManager::snapshot() const {
    std::map<std::string, std::string> result;
    for (const auto& kv : limits_) {
        const std::int64_t used = used_.count(kv.first) ? used_.at(kv.first) : 0;
        result[toString(kv.first)] =
            "used=" + std::to_string(used) +
            " soft=" + std::to_string(kv.second.softLimit) +
            " hard=" + std::to_string(kv.second.hardLimit);
    }
    return result;
}

}  // namespace aura
