#pragma once
#include <QString>
#include <functional>

namespace astra {

// Why an auto-start did or did not spawn the backend host.
enum class AutoStartOutcome {
    Spawned,                  // QProcess::startDetached returned true
    AlreadyListening,         // something already answers on the port
    AlreadyAttempted,         // this process already tried (in-process guard)
    LockedByAnotherInstance,  // another ASTRA frontend owns auto-start
    Suppressed,               // AURA_NO_BACKEND
    ExeMissing,               // aura_backend_host.exe not found next to the frontend
    SpawnFailed,              // QProcess::startDetached returned false
};

// All side effects are injectable so the decision path can be exercised without
// a real process, port, or lock file.
struct AutoStartDeps {
    std::function<bool()> backendListening;                    // probe the port
    std::function<bool(const QString& lockPath)> tryLock;      // cross-process lock
    std::function<bool(const QString& exePath)> exeExists;
    std::function<bool(const QString& exePath, int port)> spawn;
    std::function<void(const QString&)> log;
    std::function<QString()> backendExePath;
    std::function<QString()> lockPath;
};

// Runs the auto-start decision exactly once per process lifetime.
// `attempted` is the process-lifetime flag (owned by the caller so it persists
// across calls); `suppressed` models AURA_NO_BACKEND; `port` is the backend API
// port. The port probe runs BEFORE the guard so an already-running backend is
// never duplicated even if the guard was somehow reset.
AutoStartOutcome attemptAutoStart(bool suppressed, int port, bool& attempted,
                                  AutoStartDeps& deps);

}  // namespace astra
