// TST-0004 - Executable-root startup behavior (no CMD / no CWD dependence).

#include "TestHarness.h"

#include "platform/windows/BundleLocator.h"
#include "platform/windows/PackagingConfig.h"
#include "platform/windows/PathResolver.h"

#include <filesystem>
#include <string>

using namespace aura;

namespace fs = std::filesystem;

TEST_CASE(paths_resolve_from_executable_not_cwd) {
    PathResolver resolver;
    AppPaths paths;
    std::string error;
    REQUIRE(resolver.resolve(paths, error));

    CHECK(!paths.executablePath.empty());
    CHECK(PathResolver::isAbsolute(paths.executablePath));
    CHECK(PathResolver::isAbsolute(paths.appRootDir));
    CHECK(PathResolver::isAbsolute(paths.bridgeScript));

    // Every derived path lives under the application root, so a double-clicked
    // launch and a developer launch resolve identically.
    CHECK(paths.appRootDir == PathResolver::dirName(paths.executablePath));
    CHECK(paths.bridgeScript.find(paths.appRootDir) == 0);
}

TEST_CASE(path_resolution_is_cwd_independent) {
    PathResolver resolver;
    AppPaths first;
    std::string error;
    REQUIRE(resolver.resolve(first, error));

    const fs::path original = fs::current_path();
    std::error_code ec;
    fs::current_path(fs::temp_directory_path(), ec);
    AppPaths second;
    const bool ok = resolver.resolve(second, error);
    fs::current_path(original, ec);

    REQUIRE(ok);
    CHECK_EQ(first.appRootDir, second.appRootDir);
    CHECK_EQ(first.bridgeScript, second.bridgeScript);
}

TEST_CASE(join_normalizes_without_touching_cwd) {
    const std::string joined =
        PathResolver::join("/opt/astra/resources", "bridge/mt5_python");
    CHECK_EQ(joined, std::string("/opt/astra/resources/bridge/mt5_python"));

    // An absolute relative path replaces the base rather than nesting.
    CHECK_EQ(PathResolver::join("/opt/astra", "/etc/aura"),
             std::string("/etc/aura"));
}

TEST_CASE(bundle_locator_reports_missing_bridge_explicitly) {
    AppPaths paths;
    paths.executablePath = "/nonexistent/astra/aura_backend_host";
    paths.appRootDir = "/nonexistent/astra";
    paths.resourceDir = "/nonexistent/astra/resources";
    paths.bridgeDir = "/nonexistent/astra/resources/bridge/mt5_python";
    paths.bridgeScript = "/nonexistent/astra/resources/bridge/mt5_python/bridge_service.py";
    paths.pythonRuntimeDir = "/nonexistent/astra/resources/python";

    BundleLocator locator(paths);
    const BundleLayout layout = locator.locate(false);

    // No bundled runtime and no system fallback: found is false with a reason,
    // never a silent success.
    CHECK(!layout.found);
    CHECK(!layout.error.empty());
}

TEST_CASE(startup_does_not_require_manual_python) {
    // The packaging model bundles the interpreter and manages the bridge; the
    // operator is never asked to run python or a CMD script.
    const PackagingConfig config = defaultPackagingConfig();
    std::string reason;
    CHECK(config.validate(reason));
    CHECK(config.bundlePython);
    CHECK(!config.allowSystemPython);
    CHECK(!config.pythonDependencies.empty());
}
