#pragma once
// HOST-0001 - Deterministic application/resource path resolution.
//
// AURA must never depend on the current working directory. All paths are
// derived from the executable's own location so a double-clicked desktop
// launch and a developer launch resolve identically.

#include <string>

namespace aura {

struct AppPaths {
    std::string executablePath;   // absolute path to the running binary
    std::string appRootDir;       // directory containing the executable
    std::string resourceDir;      // appRoot/resources
    std::string bridgeDir;        // resourceDir/bridge/mt5_python
    std::string bridgeScript;     // bridgeDir/bridge_service.py
    std::string pythonRuntimeDir; // resourceDir/python (bundled runtime)
    std::string configDir;        // appRoot/config
    std::string dataDir;          // appRoot/data
    std::string logDir;           // appRoot/logs
};

class PathResolver {
public:
    PathResolver() = default;

    // Resolve against the running executable. Returns false only if the
    // executable path cannot be determined at all.
    bool resolve(AppPaths& out, std::string& error) const;

    // Absolute path of the currently running executable.
    static std::string executablePath();

    // Normalize to an absolute path without touching the CWD.
    static std::string join(const std::string& base, const std::string& relative);
    static std::string dirName(const std::string& path);
    static bool isAbsolute(const std::string& path);
};

}  // namespace aura
