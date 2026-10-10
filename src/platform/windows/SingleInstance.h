#pragma once
// HOST-0012 - Cross-process single-instance guard.
//
// Advisory exclusive lock on a lock file. The frontend holds one before it
// auto-starts the backend; the backend holds one for its whole lifetime. The
// port probe alone is not enough to prevent duplicates: the backend binds its
// loopback API only after the bridge handshake (up to 15s), so two processes
// launched in quick succession both observe the port closed and each spawn a
// copy. This lock closes that window.

#include <string>

namespace aura {

class InstanceLock {
public:
    InstanceLock() = default;
    ~InstanceLock();
    InstanceLock(const InstanceLock&) = delete;
    InstanceLock& operator=(const InstanceLock&) = delete;

    // Acquires the lock. Returns false when another live instance holds it; the
    // caller should then log and NOT spawn/continue. `error` describes why.
    bool acquire(const std::string& lockFilePath, std::string& error);

    void release();

    bool held() const noexcept { return held_; }

private:
#ifdef _WIN32
    void* handle_ = nullptr;   // HANDLE, kept void* to keep windows.h out of the header.
#else
    int fd_ = -1;
#endif
    std::string path_;
    bool held_ = false;
};

}  // namespace aura
