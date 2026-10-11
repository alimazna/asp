#include "autostart/AutoStart.h"

namespace astra {

AutoStartOutcome attemptAutoStart(bool suppressed, int port, bool& attempted,
                                  AutoStartDeps& deps) {
    // Probe the port FIRST, before the in-process guard. An already-running
    // backend (a second ASTRA launch, or a fast relaunch) is never duplicated,
    // and this is what makes a repeated call idempotent even without the flag.
    if (deps.backendListening && deps.backendListening()) {
        deps.log(QStringLiteral(
            "backend already listening on 127.0.0.1:%1; not spawning another")
                     .arg(port));
        return AutoStartOutcome::AlreadyListening;
    }

    // At most one auto-start per process lifetime. The previous code reached
    // this guard only through main(), so a second startup path (or a re-entrant
    // call) probed the port before the backend had bound it and spawned a
    // duplicate: the log showed two "attempting to spawn" lines 33s apart.
    if (attempted) {
        deps.log(QStringLiteral("auto-start already attempted; not spawning again"));
        return AutoStartOutcome::AlreadyAttempted;
    }
    attempted = true;

    if (suppressed) {
        deps.log(QStringLiteral("auto-start suppressed by AURA_NO_BACKEND"));
        return AutoStartOutcome::Suppressed;
    }

    // Cross-process guard: a second frontend launched while the first is still
    // baking in (the port is not yet answering) must not spawn its own backend.
    const QString lock = deps.lockPath ? deps.lockPath() : QString();
    if (!lock.isEmpty() && deps.tryLock && !deps.tryLock(lock)) {
        deps.log(QStringLiteral(
            "auto-start lock held by another ASTRA instance; not spawning"));
        return AutoStartOutcome::LockedByAnotherInstance;
    }

    const QString exe = deps.backendExePath ? deps.backendExePath() : QString();
    if (exe.isEmpty() || !deps.exeExists || !deps.exeExists(exe)) {
        deps.log(QStringLiteral("FAILED: backend host not found next to frontend: ")
                 + exe);
        return AutoStartOutcome::ExeMissing;
    }

    deps.log(QStringLiteral("attempting to spawn ") + exe + QStringLiteral(" ..."));
    if (!deps.spawn || !deps.spawn(exe, port)) {
        deps.log(QStringLiteral("FAILED: QProcess::startDetached returned false for ")
                 + exe);
        return AutoStartOutcome::SpawnFailed;
    }
    deps.log(QStringLiteral("spawn ok: ") + exe);
    return AutoStartOutcome::Spawned;
}

}  // namespace astra
