#pragma once
// HOST-0003 - Locates the bundled Python runtime and bridge assets.
//
// Runtime dependencies must be discoverable from the installed layout. If the
// bundled runtime is missing we report it; we never fall back to a system
// interpreter silently, because that would make the product non-reproducible.

#include "platform/windows/PathResolver.h"

#include <string>
#include <vector>

namespace aura {

enum class PythonRuntimeSource {
    BUNDLED,        // resources/python
    SIDECAR,        // resources/python-embed
    SYSTEM_FALLBACK,// development only
    NONE,
};

struct BundleLayout {
    bool found = false;
    PythonRuntimeSource pythonSource = PythonRuntimeSource::NONE;
    std::string pythonExecutable;    // absolute path to python interpreter
    std::string bridgeScript;        // absolute path to bridge_service.py
    // True when the bridge is a frozen executable (bridge.exe) rather than a
    // script run by an interpreter. The supervisor then launches it directly,
    // with no interpreter argument.
    bool bridgeIsFrozen = false;
    std::vector<std::string> searched;   // candidates examined (diagnostics)
    std::string error;
};

class BundleLocator {
public:
    explicit BundleLocator(AppPaths paths) : paths_(std::move(paths)) {}

    // `allowSystemFallback` should be true only in development builds.
    BundleLayout locate(bool allowSystemFallback = false) const;

private:
    AppPaths paths_;
};

}  // namespace aura
