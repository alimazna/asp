// HOST-0004 - Bundle locator implementation.

#include "platform/windows/BundleLocator.h"

#include <cstdlib>
#include <vector>

#ifdef _WIN32
#include <io.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace aura {

namespace {

bool fileExists(const std::string& path) {
    if (path.empty()) return false;
#ifdef _WIN32
    return _access(path.c_str(), 0) == 0;
#else
    return ::access(path.c_str(), F_OK) == 0;
#endif
}

}  // namespace

BundleLayout BundleLocator::locate(bool allowSystemFallback) const {
    BundleLayout layout;
    layout.bridgeScript = paths_.bridgeScript;

    // Preferred: a self-contained frozen bridge (bridge.exe). It needs no
    // interpreter at all, which is what the installer ships.
    const std::vector<std::string> frozenCandidates = {
        PathResolver::join(paths_.bridgeDir, "bridge.exe"),
        PathResolver::join(paths_.bridgeDir, "bridge"),
    };
#ifdef _WIN32
    const std::vector<std::string> frozenOrder = {frozenCandidates[0]};
#else
    const std::vector<std::string> frozenOrder = {frozenCandidates[1]};
#endif
    for (const auto& candidate : frozenOrder) {
        layout.searched.push_back(candidate);
        if (fileExists(candidate)) {
            layout.pythonSource = PythonRuntimeSource::BUNDLED;
            layout.pythonExecutable = candidate;
            layout.bridgeScript = candidate;
            layout.bridgeIsFrozen = true;
            layout.found = true;
            layout.error.clear();
            return layout;
        }
    }

    if (!fileExists(paths_.bridgeScript)) {
        layout.error = "bridge script not found at " + paths_.bridgeScript;
    }

#ifdef _WIN32
    const std::string pythonName = "python.exe";
#else
    const std::string pythonName = "python3";
#endif

    // 1. Bundled private runtime.
    const std::vector<std::string> bundledCandidates = {
        PathResolver::join(paths_.pythonRuntimeDir, pythonName),
        PathResolver::join(paths_.pythonRuntimeDir, "bin/python3"),
        PathResolver::join(paths_.pythonRuntimeDir, "python.exe"),
        PathResolver::join(paths_.resourceDir, "python-embed/python.exe"),
    };
    for (const auto& candidate : bundledCandidates) {
        layout.searched.push_back(candidate);
        if (fileExists(candidate)) {
            layout.pythonSource = PythonRuntimeSource::BUNDLED;
            layout.pythonExecutable = candidate;
            layout.found = true;
            layout.error.clear();
            return layout;
        }
    }

    // 2. Development-only system fallback.
    if (allowSystemFallback) {
        const char* pathEnv = std::getenv("PATH");
        if (pathEnv != nullptr) {
            std::string path(pathEnv);
            std::size_t start = 0;
            while (start <= path.size()) {
                const std::size_t end = path.find(
#ifdef _WIN32
                    ';',
#else
                    ':',
#endif
                    start);
                const std::string dir = path.substr(
                    start, end == std::string::npos ? std::string::npos : end - start);
                if (!dir.empty()) {
                    const std::string candidate = PathResolver::join(dir, pythonName);
                    layout.searched.push_back(candidate);
                    if (fileExists(candidate)) {
                        layout.pythonSource = PythonRuntimeSource::SYSTEM_FALLBACK;
                        layout.pythonExecutable = candidate;
                        layout.found = true;
                        layout.error.clear();
                        return layout;
                    }
                }
                if (end == std::string::npos) break;
                start = end + 1;
            }
        }
    }

    if (layout.error.empty()) {
        layout.error = "no Python runtime found in the installed application layout";
    }
    layout.found = false;
    return layout;
}

}  // namespace aura
