// Offscreen regression for the duplicate-backend-spawn bug (Bug 2).
//
// The real log showed two "attempting to spawn" lines 33s apart. The guard was
// a single bool in one process, so it could not see a second frontend process,
// and the port probe alone cannot close the pre-bind race (the backend binds
// 8790 only after the bridge handshake). These tests drive the real
// attemptAutoStart() decision path and pin: at most one spawn per process, no
// duplicate of an already-listening backend, and a cross-process lock.

#include "autostart/AutoStart.h"
#include "platform/windows/SingleInstance.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QTemporaryDir>

#include <cstdio>
#include <string>
#include <vector>

using namespace astra;

namespace {

int g_failures = 0;

void check(bool ok, const char* name) {
    std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) ++g_failures;
}

int countContaining(const std::vector<QString>& logs, const QString& needle) {
    int n = 0;
    for (const QString& l : logs)
        if (l.contains(needle)) ++n;
    return n;
}

struct Harness {
    std::vector<QString> logs;
    int spawnCount = 0;
    bool listening = false;
    bool lockTaken = false;   // models a lock already held by another instance
    bool useRealLock = false; // use a real aura::InstanceLock on lockPath
    bool exeExists = true;
    bool spawnSucceeds = true;
    QString lockPath;
    aura::InstanceLock realLock;

    AutoStartDeps deps() {
        AutoStartDeps d;
        d.backendListening = [this]() { return listening; };
        d.tryLock = [this](const QString& p) {
            if (useRealLock) {
                std::string err;
                return realLock.acquire(p.toStdString(), err);
            }
            if (lockTaken) return false;
            lockTaken = true;
            return true;
        };
        d.exeExists = [this](const QString&) { return exeExists; };
        d.spawn = [this](const QString&, int) {
            ++spawnCount;
            return spawnSucceeds;
        };
        d.log = [this](const QString& s) { logs.push_back(s); };
        d.backendExePath = []() {
            return QStringLiteral("C:/Program Files/ASTRA/aura_backend_host.exe");
        };
        d.lockPath = [this]() { return lockPath; };
        return d;
    }
};

}  // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    QTemporaryDir tmp;
    if (!tmp.isValid()) {
        std::fprintf(stderr, "could not create temp dir\n");
        return 1;
    }

    // ── Two calls, one spawn: exactly one "attempting to spawn" and one ──────
    // "spawn ok" per process lifetime.
    {
        Harness h;
        h.lockPath = QDir(tmp.path()).filePath("a.lock");
        bool attempted = false;
        AutoStartDeps d = h.deps();

        const AutoStartOutcome first = attemptAutoStart(false, 8790, attempted, d);
        const AutoStartOutcome second = attemptAutoStart(false, 8790, attempted, d);

        check(first == AutoStartOutcome::Spawned, "first call spawns the backend");
        check(second == AutoStartOutcome::AlreadyAttempted,
              "second call is refused by the process-lifetime guard");
        check(h.spawnCount == 1, "exactly one spawn happened");
        check(countContaining(h.logs, "attempting to spawn") == 1,
              "exactly one 'attempting to spawn' line");
        check(countContaining(h.logs, "spawn ok") == 1,
              "exactly one 'spawn ok' line");
    }

    // ── An already-listening backend is never duplicated, and the guard is ──
    // NOT consumed by that check (a later call still spawns once).
    {
        Harness h;
        h.lockPath = QDir(tmp.path()).filePath("b.lock");
        h.listening = true;
        bool attempted = false;
        AutoStartDeps d = h.deps();

        const AutoStartOutcome o1 = attemptAutoStart(false, 8790, attempted, d);
        check(o1 == AutoStartOutcome::AlreadyListening,
              "an already-listening backend is not spawned again");
        check(!attempted, "the port-probe short-circuit does not consume the guard");

        h.listening = false;
        const AutoStartOutcome o2 = attemptAutoStart(false, 8790, attempted, d);
        check(o2 == AutoStartOutcome::Spawned, "a later call still spawns once");
        check(h.spawnCount == 1, "still exactly one spawn overall");
    }

    // ── Suppressed (AURA_NO_BACKEND) and missing-exe never spawn. ───────────
    {
        Harness h;
        h.lockPath = QDir(tmp.path()).filePath("c.lock");
        bool attempted = false;
        AutoStartDeps d = h.deps();
        check(attemptAutoStart(true, 8790, attempted, d) == AutoStartOutcome::Suppressed,
              "AURA_NO_BACKEND suppresses the spawn");
        check(h.spawnCount == 0, "suppressed: no spawn");
    }
    {
        Harness h;
        h.lockPath = QDir(tmp.path()).filePath("d.lock");
        h.exeExists = false;
        bool attempted = false;
        AutoStartDeps d = h.deps();
        check(attemptAutoStart(false, 8790, attempted, d) == AutoStartOutcome::ExeMissing,
              "a missing backend exe reports ExeMissing");
        check(h.spawnCount == 0, "missing exe: no spawn");
    }

    // ── A refused spawn is reported, not silently retried. ──────────────────
    {
        Harness h;
        h.lockPath = QDir(tmp.path()).filePath("e.lock");
        h.spawnSucceeds = false;
        bool attempted = false;
        AutoStartDeps d = h.deps();
        check(attemptAutoStart(false, 8790, attempted, d) == AutoStartOutcome::SpawnFailed,
              "a refused startDetached reports SpawnFailed");
        check(countContaining(h.logs, "spawn ok") == 0, "refused spawn: no 'spawn ok' line");
    }

    // ── Cross-process: a second frontend cannot acquire the held lock, so it ─
    // does not spawn a duplicate backend while the first is still baking in.
    {
        const QString lp = QDir(tmp.path()).filePath("f.lock");

        Harness h1;
        h1.useRealLock = true;
        h1.lockPath = lp;
        bool a1 = false;
        AutoStartDeps d1 = h1.deps();
        check(attemptAutoStart(false, 8790, a1, d1) == AutoStartOutcome::Spawned,
              "first frontend acquires the lock and spawns");

        Harness h2;
        h2.useRealLock = true;
        h2.lockPath = lp;
        bool a2 = false;
        AutoStartDeps d2 = h2.deps();
        check(attemptAutoStart(false, 8790, a2, d2)
                  == AutoStartOutcome::LockedByAnotherInstance,
              "second frontend is refused by the held cross-process lock");
        check(h2.spawnCount == 0, "second frontend spawns nothing");
    }

    // ── The lock directory does not exist yet (fresh install / cleared state). ─
    // The real failure: the frontend's lock lives under %LOCALAPPDATA%\ASTRA\logs,
    // which is absent on a clean machine. If acquire() cannot create that dir it
    // returns false and the frontend wrongly treats a missing directory as
    // "another instance is running", so it never spawns the backend and the UI
    // shows OFFLINE. The first launch on a clean box must still spawn.
    {
        const QString missing =
            QDir(tmp.path()).filePath("not-yet-created/deeper/g.lock");
        check(!QFileInfo::exists(QFileInfo(missing).absolutePath()),
              "precondition: the lock directory does not exist");

        Harness h;
        h.useRealLock = true;
        h.lockPath = missing;
        bool attempted = false;
        AutoStartDeps d = h.deps();
        check(attemptAutoStart(false, 8790, attempted, d) == AutoStartOutcome::Spawned,
              "a missing lock directory must not be mistaken for another instance");
        check(h.spawnCount == 1, "the first launch on a clean box still spawns");
    }

    if (g_failures == 0)
        std::printf("Auto-start tests: all passed\n");
    else
        std::fprintf(stderr, "Auto-start tests: %d failed\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
