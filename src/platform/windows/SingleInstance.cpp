// HOST-0012 - Cross-process single-instance guard implementation.

#include "platform/windows/SingleInstance.h"

#include <filesystem>

#ifdef _WIN32
#include <windows.h>
#else
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace aura {

#ifdef _WIN32

InstanceLock::~InstanceLock() { release(); }

bool InstanceLock::acquire(const std::string& lockFilePath, std::string& error) {
    if (held_) {
        error = "lock already held by this object";
        return false;
    }
    // Create the containing directory first. The lock lives under the
    // user-writable state root (%LOCALAPPDATA%\ASTRA\logs or an equivalent),
    // which does not exist on a fresh install or after the user clears it.
    // CreateFileA with OPEN_ALWAYS does not create parent directories, so
    // without this the very first launch of a clean install would fail to
    // acquire the lock and (wrongly) conclude another instance is running.
    {
        std::error_code ec;
        std::filesystem::path p(lockFilePath);
        if (p.has_parent_path())
            std::filesystem::create_directories(p.parent_path(), ec);
    }
    // Share mode 0 (exclusive): a second open by any process fails with
    // ERROR_SHARING_VIOLATION. The handle is released automatically when the
    // process exits, so a crashed holder never leaves a stale lock.
    HANDLE h = CreateFileA(lockFilePath.c_str(), GENERIC_READ | GENERIC_WRITE,
                           0, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_HIDDEN,
                           nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        const DWORD code = GetLastError();
        if (code == ERROR_SHARING_VIOLATION || code == ERROR_ACCESS_DENIED) {
            error = "another instance already holds " + lockFilePath;
        } else {
            error = "CreateFile failed with code " + std::to_string(code);
        }
        return false;
    }
    handle_ = h;
    path_ = lockFilePath;
    held_ = true;
    return true;
}

void InstanceLock::release() {
    if (handle_ != nullptr) {
        CloseHandle(static_cast<HANDLE>(handle_));
        handle_ = nullptr;
    }
    held_ = false;
}

#else

InstanceLock::~InstanceLock() { release(); }

bool InstanceLock::acquire(const std::string& lockFilePath, std::string& error) {
    if (held_) {
        error = "lock already held by this object";
        return false;
    }
    // See the Windows branch: the lock lives under a state root that may not
    // exist yet on a fresh install, and open() does not create parents.
    {
        std::error_code ec;
        std::filesystem::path p(lockFilePath);
        if (p.has_parent_path())
            std::filesystem::create_directories(p.parent_path(), ec);
    }
    const int fd = ::open(lockFilePath.c_str(), O_CREAT | O_RDWR, 0644);
    if (fd < 0) {
        error = "open failed: " + std::string(std::strerror(errno));
        return false;
    }
    if (::flock(fd, LOCK_EX | LOCK_NB) != 0) {
        ::close(fd);
        error = "another instance already holds " + lockFilePath;
        return false;
    }
    fd_ = fd;
    path_ = lockFilePath;
    held_ = true;
    return true;
}

void InstanceLock::release() {
    if (fd_ >= 0) {
        ::flock(fd_, LOCK_UN);
        ::close(fd_);
        fd_ = -1;
    }
    held_ = false;
}

#endif

}  // namespace aura
