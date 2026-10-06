#pragma once
// RES-0028 - Resilience runtime: explicit pause/resume control.

#include "foundation/SystemMode.h"
#include "foundation/Timestamp.h"

#include <mutex>
#include <string>

namespace aura {

class PauseResumeManager {
public:
    PauseResumeManager() = default;

    bool pause(const std::string& reason);
    bool resume();
    bool isPaused() const noexcept;

    std::string pauseReason() const;
    SystemMode mode() const noexcept;

private:
    mutable std::mutex mutex_;
    bool paused_ = false;
    std::string reason_;
};

}  // namespace aura
