// RES-0029 - Resilience runtime: pause/resume implementation.

#include "resilience/PauseResumeManager.h"

namespace aura {

bool PauseResumeManager::pause(const std::string& reason) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (paused_) return false;  // already paused
    paused_ = true;
    reason_ = reason;
    return true;
}

bool PauseResumeManager::resume() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!paused_) return false;
    paused_ = false;
    reason_.clear();
    return true;
}

bool PauseResumeManager::isPaused() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return paused_;
}

std::string PauseResumeManager::pauseReason() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return reason_;
}

SystemMode PauseResumeManager::mode() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return paused_ ? SystemMode::PAUSED : SystemMode::SHADOW;
}

}  // namespace aura
