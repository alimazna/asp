// RSH-0018 - Research sandbox implementation.

#include "research/Sandbox.h"

namespace aura {

bool Sandbox::open(const EntityId& experimentId, Timestamp now) {
    if (open_) return false;
    if (experimentId.empty()) return false;
    open_ = true;
    experimentId_ = experimentId;
    openedAt_ = now.isUnknown() ? Timestamp::now() : now;
    operations_ = 0;
    scratch_.clear();
    return true;
}

bool Sandbox::noteOperation() {
    if (!open_) return false;
    if (operations_ >= limits_.maxOperations) return false;
    ++operations_;
    return true;
}

bool Sandbox::withinLimits(Timestamp now) const {
    if (!open_) return false;
    if (operations_ > limits_.maxOperations) return false;
    if (openedAt_.isUnknown()) return true;
    return now.epochMillis() - openedAt_.epochMillis() <= limits_.maxWallClockMillis;
}

void Sandbox::close() {
    open_ = false;
    experimentId_ = EntityId();
    scratch_.clear();
    operations_ = 0;
}

void Sandbox::set(const std::string& key, const std::string& value) {
    if (!open_) return;
    scratch_[key] = value;
}

bool Sandbox::get(const std::string& key, std::string& out) const {
    if (!open_) return false;
    auto it = scratch_.find(key);
    if (it == scratch_.end()) return false;
    out = it->second;
    return true;
}

}  // namespace aura
