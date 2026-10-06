// PKG-0006 - Packaging configuration implementation.

#include "platform/windows/PackagingConfig.h"

namespace aura {

namespace {

std::string join(const std::string& root, const std::string& relative) {
    if (root.empty()) return relative;
    if (relative.empty()) return root;
    const char last = root.back();
    if (last == '/' || last == '\\') return root + relative;
    return root + "/" + relative;
}

}  // namespace

bool PackagingConfig::validate(std::string& reason) const {
    if (productName.empty()) {
        reason = "productName is empty";
        return false;
    }
    if (executableName.empty()) {
        reason = "executableName is empty";
        return false;
    }
    if (bridgeRelativePath.empty()) {
        reason = "bridgeRelativePath is empty";
        return false;
    }
    if (bundlePython && pythonRelativePath.empty()) {
        reason = "bundlePython is set but pythonRelativePath is empty";
        return false;
    }
    if (!bundlePython && !allowSystemPython) {
        reason = "no interpreter available: bundling disabled and system "
                 "fallback not allowed";
        return false;
    }
    for (const auto& dependency : pythonDependencies) {
        if (dependency.required && dependency.name.empty()) {
            reason = "a required python dependency has an empty name";
            return false;
        }
    }
    return true;
}

std::string PackagingConfig::bridgeEntryPath(const BundleLayout& bundle,
                                             const AppPaths& paths) const {
    // Prefer the located bridge script; fall back to the declared layout
    // resolved against the deterministic app root (never the CWD).
    if (!bundle.bridgeScript.empty()) return bundle.bridgeScript;
    return join(paths.resourceDir, bridgeRelativePath);
}

std::string PackagingConfig::interpreterPath(const BundleLayout& bundle,
                                             const AppPaths& paths) const {
    if (!bundle.pythonExecutable.empty()) return bundle.pythonExecutable;
    if (bundlePython) return join(paths.resourceDir, pythonRelativePath);
    return "python";   // development-only system fallback
}

PackagingConfig defaultPackagingConfig() {
    PackagingConfig config;
    config.pythonDependencies = {
        {"MetaTrader5", ">=5.0.45", true},
        {"pandas", ">=2.0", true},
        {"numpy", ">=1.24", true},
    };
    return config;
}

}  // namespace aura
