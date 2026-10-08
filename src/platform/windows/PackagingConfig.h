#pragma once
// PKG-0005 - Packaging configuration.
//
// Describes the installed desktop layout so the runtime can resolve the Python
// bridge and its dependencies without relying on the current working
// directory. The layout is data; path resolution logic lives in PathResolver.

#include "platform/windows/BundleLocator.h"

#include <string>
#include <vector>

namespace aura {

struct PythonDependency {
    std::string name;
    std::string versionSpec;
    bool required = true;
};

struct PackagingConfig {
    std::string productName = "ASTRA";
    std::string executableName = "aura_backend_host";
    std::string bridgeRelativePath = "bridge/mt5_python/bridge_service.py";
    std::string requirementsRelativePath = "bridge/mt5_python/requirements.txt";
    std::string pythonRelativePath = "python/python.exe";
    std::string dataRelativePath = "data";
    std::string logsRelativePath = "logs";
    bool bundlePython = true;         // ship a private interpreter
    bool allowSystemPython = false;   // development only
    std::vector<PythonDependency> pythonDependencies;

    // Validate the layout description. Returns false with a reason when a
    // required path is empty or contradictory.
    bool validate(std::string& reason) const;

    // The bridge entry path: the located script when available, otherwise the
    // declared layout resolved against the app root.
    std::string bridgeEntryPath(const BundleLayout& bundle,
                                const AppPaths& paths) const;

    // The interpreter to use, preferring the bundled one.
    std::string interpreterPath(const BundleLayout& bundle,
                                const AppPaths& paths) const;
};

// Default packaging configuration for ASTRA (XAUUSD, SHADOW).
PackagingConfig defaultPackagingConfig();

}  // namespace aura
