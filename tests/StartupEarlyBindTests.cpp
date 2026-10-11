// HOST-0013 - the backend API must bind BEFORE the bridge is spawned.
//
// Reported field failure: on a fresh Windows install the backend stayed silent
// on 8790 for ~173s while the bridge was being spawned (Windows Defender
// scanning bridge.exe), so the frontend declared "backend unreachable". The fix
// is ordering: resolve + bind the API first, spawn the bridge afterwards. A warm
// CI runner cannot see the difference (the spawn is instant there), so this test
// pins the ordering deterministically with a bridge that is slow to bind.
//
// It runs the REAL host binary from a temp "installed" layout whose bridge
// bundle contains a shim whose MT5 initialize() sleeps. The host spawns that
// bridge, which takes seconds to bind 8791; meanwhile the host's API must answer
// on its port within well under that delay, and its timeline log must record the
// bind before the spawn. Linux-only: Windows is covered by the installed-flow
// smoke test in build-windows.yml.

#include "TestHarness.h"

#include "foundation/HttpClient.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#ifndef AURA_SOURCE_DIR
#define AURA_SOURCE_DIR "."
#endif

#ifndef _WIN32
#include <arpa/inet.h>
#include <csignal>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

using namespace aura;
namespace fs = std::filesystem;

namespace {

#ifndef _WIN32
std::string findPython() {
    const char* pathEnv = std::getenv("PATH");
    if (pathEnv == nullptr) return "";
    const std::string path(pathEnv);
    const std::vector<std::string> names = {"python3", "python"};
    std::size_t start = 0;
    while (start <= path.size()) {
        const std::size_t end = path.find(':', start);
        const std::string dir =
            path.substr(start, end == std::string::npos ? std::string::npos
                                                        : end - start);
        if (!dir.empty()) {
            for (const auto& name : names) {
                const std::string candidate = dir + "/" + name;
                std::error_code ec;
                if (fs::exists(candidate, ec)) return candidate;
            }
        }
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return "";
}

std::uint16_t freePort() {
    const int s = ::socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) return 0;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(0);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    std::uint16_t port = 0;
    if (::bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0) {
        sockaddr_in bound{};
        socklen_t len = sizeof(bound);
        if (::getsockname(s, reinterpret_cast<sockaddr*>(&bound), &len) == 0) {
            port = ntohs(bound.sin_port);
        }
    }
    ::close(s);
    return port;
}

// Index of the first line containing `marker`, or -1. File order is the
// deterministic signal: on a warm runner both startup steps land in the same
// millisecond, so only the write order proves which ran first.
long lineIndexOf(const std::vector<std::string>& lines,
                 const std::string& marker) {
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (lines[i].find(marker) != std::string::npos) return static_cast<long>(i);
    }
    return -1;
}

const char* kSlowShim = R"PY(
import os
import time

_initialized = False

def initialize(*a, **k):
    time.sleep(float(os.environ.get("FAKE_MT5_SLOW_INIT", "4")))
    global _initialized
    _initialized = True
    return True

def last_error():
    return (0, "ok")

def shutdown():
    return True

def terminal_info(*a, **k):
    return None

def symbol_select(*a, **k):
    return True

class _Sym:
    name = "XAUUSD"
    trade_tick_size = 0.01
    trade_tick_value = 1.0
    point = 0.01
    digits = 2
    trade_contract_size = 100.0
    volume_min = 0.01
    volume_max = 100.0
    volume_step = 0.01
    trade_stops_level = 0
    trade_freeze_level = 0
    trade_mode = 4
    filling_mode = 1
    margin_mode = 2
    visible = True
    select = True

def symbol_info(symbol=None):
    return _Sym()

def symbol_info_tick(symbol=None):
    class T:
        time = int(time.time())
        bid = 2000.0
        ask = 2000.2
        last = 2000.1
        volume = 1
    return T()

def copy_rates_from_pos(*a, **k):
    return []

def copy_rates_range(*a, **k):
    return []

def account_info(*a, **k):
    return None

def version():
    return (500, 0, 0)
)PY";
#endif

}  // namespace

TEST_CASE(api_binds_before_the_bridge_spawn) {
#ifdef _WIN32
    std::cout << "  [SKIP] bind-before-spawn ordering is exercised on Windows by "
                 "the installed-flow smoke test in build-windows.yml\n";
#else
    const std::string python = findPython();
    if (python.empty()) {
        std::cout << "  [SKIP] no python interpreter on PATH\n";
        return;
    }
    const std::string hostBin =
        std::string(AURA_SOURCE_DIR) + "/build/aura_backend_host";
    if (!fs::exists(hostBin)) {
        std::cout << "  [SKIP] " << hostBin << " not built\n";
        return;
    }

    const fs::path tmp = fs::temp_directory_path() /
                         ("aura_early_bind_" + std::to_string(::getpid()));
    fs::remove_all(tmp);
    const fs::path bundle = tmp / "resources/bridge/mt5_python";
    fs::create_directories(bundle);
    const fs::path xdg = tmp / "xdg";
    fs::create_directories(xdg);

    fs::copy_file(hostBin, tmp / "aura_backend_host");
    fs::permissions(tmp / "aura_backend_host",
                    fs::perms::owner_all | fs::perms::group_read |
                        fs::perms::group_exec | fs::perms::others_read |
                        fs::perms::others_exec);
    const fs::path srcBridge = std::string(AURA_SOURCE_DIR) + "/bridge/mt5_python";
    for (const char* name : {"bridge_service.py", "mt5_client.py", "schemas.py"}) {
        fs::copy_file(srcBridge / name, bundle / name);
    }
    {
        std::ofstream shim(bundle / "MetaTrader5.py");
        shim << kSlowShim;
    }

    ::setenv("XDG_DATA_HOME", xdg.c_str(), 1);
    ::setenv("FAKE_MT5_SLOW_INIT", "5", 1);
    ::setenv("PYTHONUNBUFFERED", "1", 1);

    const std::uint16_t apiPort = freePort();
    REQUIRE(apiPort != 0);

    const pid_t pid = ::fork();
    REQUIRE(pid >= 0);
    if (pid == 0) {
        ::setsid();  // own session so the host AND its bridge child can be reaped
        const std::string exe = (tmp / "aura_backend_host").string();
        const std::string port = std::to_string(apiPort);
        ::execl(exe.c_str(), exe.c_str(), "--api-port", port.c_str(),
                "--dev-system-python", static_cast<char*>(nullptr));
        _exit(127);
    }

    // The API must answer while the spawned bridge is still sleeping 5s in
    // initialize(). Poll for 3s: comfortably inside that window, comfortably
    // outside a healthy host's bind (which is milliseconds).
    bool apiAnswered = false;
    long answeredMs = 0;
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < 60 && !apiAnswered; ++i) {
        HttpRequest req;
        req.host = "127.0.0.1";
        req.port = apiPort;
        req.path = "/api/v1/health";
        req.connectTimeoutMillis = 300;
        req.readTimeoutMillis = 500;
        const HttpResponse resp = httpGet(req);
        if (resp.ok && resp.statusCode >= 200 && resp.statusCode < 300) {
            apiAnswered = true;
            answeredMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - start)
                             .count();
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    // Reap the host and its bridge child (the host is the session leader).
    ::kill(-pid, SIGKILL);
    int status = 0;
    ::waitpid(pid, &status, 0);

    std::vector<std::string> lines;
    {
        std::ifstream log(xdg / "astra/logs/backend-host.log");
        std::string line;
        while (std::getline(log, line)) lines.push_back(line);
    }
    fs::remove_all(tmp);

    CHECK(apiAnswered);
    CHECK(answeredMs < 3000);

    const long bindAt = lineIndexOf(lines, "frontend api bound");
    const long spawnAt = lineIndexOf(lines, "startAsync (bridge spawn)");
    CHECK(bindAt >= 0);
    CHECK(spawnAt >= 0);
    if (bindAt >= 0 && spawnAt >= 0) {
        // The whole point of the fix: the bind line is written before the spawn
        // line, so the API is already answering while the bridge spawns.
        CHECK(bindAt < spawnAt);
    }
#endif
}
