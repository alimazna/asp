#pragma once
// RSH-0018 - Research sandbox.
//
// An isolated execution context for experiments. Anything created here is
// discarded when the sandbox is closed and can never mutate live backend
// state or acquire execution authority.

#include "foundation/EntityId.h"
#include "foundation/Timestamp.h"

#include <map>
#include <string>
#include <vector>

namespace aura {

struct SandboxLimits {
    std::size_t maxOperations = 100000;
    std::int64_t maxWallClockMillis = 300000;   // 5 minutes
};

struct SandboxResult {
    bool ok = false;
    std::string output;
    std::string reason;
    std::int64_t elapsedMillis = 0;
};

class Sandbox {
public:
    explicit Sandbox(SandboxLimits limits = {}) : limits_(limits) {}

    // Open an isolated sandbox. Fails if one is already open.
    bool open(const EntityId& experimentId, Timestamp now);

    bool isOpen() const noexcept { return open_; }
    const EntityId& experimentId() const noexcept { return experimentId_; }

    // Record an operation; returns false if a limit would be exceeded.
    bool noteOperation();

    bool withinLimits(Timestamp now) const;

    // Discard everything. Always safe to call.
    void close();

    // Scratch state confined to the sandbox; never persisted to the live store.
    void set(const std::string& key, const std::string& value);
    bool get(const std::string& key, std::string& out) const;

    const SandboxLimits& limits() const noexcept { return limits_; }

private:
    SandboxLimits limits_;
    bool open_ = false;
    EntityId experimentId_;
    Timestamp openedAt_;
    std::size_t operations_ = 0;
    std::map<std::string, std::string> scratch_;
};

}  // namespace aura
