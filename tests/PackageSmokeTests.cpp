// TST-0010 - Packaged layout and startup prerequisites.

#include "TestHarness.h"

#include "api/BackendApiSchema.h"
#include "foundation/Json.h"
#include "platform/windows/PackagingConfig.h"
#include "platform/windows/PathResolver.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using namespace aura;

#ifndef AURA_SOURCE_DIR
#define AURA_SOURCE_DIR "."
#endif

namespace {

std::string readFile(const std::string& path) {
    std::ifstream in(path);
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

AppPaths samplePaths() {
    AppPaths paths;
    paths.executablePath = "/opt/astra/aura_backend_host";
    paths.appRootDir = "/opt/astra";
    paths.resourceDir = "/opt/astra/resources";
    paths.bridgeDir = "/opt/astra/resources/bridge/mt5_python";
    paths.bridgeScript = "/opt/astra/resources/bridge/mt5_python/bridge_service.py";
    paths.pythonRuntimeDir = "/opt/astra/resources/python";
    return paths;
}

}  // namespace

TEST_CASE(default_packaging_config_is_coherent) {
    const PackagingConfig config = defaultPackagingConfig();
    std::string reason;
    CHECK(config.validate(reason));
    CHECK_EQ(config.productName, std::string("ASTRA"));
    CHECK(config.bundlePython);
    CHECK(!config.allowSystemPython);
}

TEST_CASE(packaging_rejects_contradictory_layout) {
    PackagingConfig config = defaultPackagingConfig();
    config.bundlePython = false;
    config.allowSystemPython = false;
    std::string reason;
    CHECK(!config.validate(reason));
    CHECK(!reason.empty());
}

TEST_CASE(bridge_and_interpreter_paths_resolve_deterministically) {
    const PackagingConfig config = defaultPackagingConfig();
    const AppPaths paths = samplePaths();

    BundleLayout emptyBundle;   // nothing located yet
    const std::string bridge = config.bridgeEntryPath(emptyBundle, paths);
    const std::string interpreter = config.interpreterPath(emptyBundle, paths);

    CHECK(bridge.find(paths.resourceDir) == 0);
    CHECK(bridge.find("bridge_service.py") != std::string::npos);
    CHECK(interpreter.find(paths.resourceDir) == 0);

    // A located bundle takes precedence over the declared layout.
    BundleLayout located;
    located.bridgeScript = "/opt/astra/resources/bridge/mt5_python/bridge_service.py";
    located.pythonExecutable = "/opt/astra/resources/python/python.exe";
    CHECK_EQ(config.bridgeEntryPath(located, paths), located.bridgeScript);
    CHECK_EQ(config.interpreterPath(located, paths), located.pythonExecutable);
}

TEST_CASE(runtime_manifest_is_valid_and_declares_no_manual_cmd) {
    const std::string path =
        std::string(AURA_SOURCE_DIR) + "/src/api/RuntimeManifest.json";
    REQUIRE(std::filesystem::exists(path));

    const std::string text = readFile(path);
    JsonValue root;
    std::string parseError;
    REQUIRE(JsonValue::parse(text, root, parseError));
    REQUIRE(root.isObject());

    CHECK_EQ(root["api"].asString(), std::string("v1"));
    CHECK_EQ(root["mode"].asString(), std::string("SHADOW"));
    CHECK_EQ(root["asset"].asString(), std::string("XAUUSD"));
    CHECK_EQ(root["live_trading_authorised"].asBool(), false);
    CHECK_EQ(root["timeframes"].size(), static_cast<std::size_t>(9));

    const JsonValue& bridge = root["components"]["python_bridge"];
    CHECK_EQ(bridge["requires_manual_cmd"].asBool(), false);
    CHECK_EQ(bridge["managed_by_application"].asBool(), true);
}

TEST_CASE(json_schema_never_emits_invalid_numbers) {
    // NaN/Infinity are not valid JSON and must be emitted as null.
    CHECK_EQ(jsonNumber(std::nan("")), std::string("null"));
    CHECK_EQ(jsonNumber(1.0 / 0.0), std::string("null"));

    // Strings are escaped.
    CHECK_EQ(jsonString("a\"b"), std::string("\"a\\\"b\""));
}

TEST_CASE(error_envelope_is_structured) {
    const ApiResponse response = errorResponse(404, "not_found", "no such route");
    CHECK(!response.ok);
    CHECK_EQ(response.status, 404);
    CHECK(response.body.find("\"error\":true") != std::string::npos);
    CHECK(response.body.find("not_found") != std::string::npos);
}
