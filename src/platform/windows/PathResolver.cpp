// HOST-0002 - Path resolution implementation (portable).

#include "platform/windows/PathResolver.h"

#include <cstdlib>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <climits>
#include <unistd.h>
#endif

namespace aura {

namespace {
constexpr char kSep =
#ifdef _WIN32
    '\\';
#else
    '/';
#endif
}  // namespace

bool PathResolver::isAbsolute(const std::string& path) {
    if (path.empty()) return false;
#ifdef _WIN32
    if (path.size() >= 2 && path[1] == ':') return true;
    if (path.size() >= 2 && (path[0] == '\\' || path[0] == '/')) return true;
    return false;
#else
    return path[0] == '/';
#endif
}

std::string PathResolver::join(const std::string& base, const std::string& relative) {
    if (relative.empty()) return base;
    if (isAbsolute(relative)) return relative;
    if (base.empty()) return relative;
    std::string out = base;
    if (out.back() != '/' && out.back() != '\\') out.push_back(kSep);
    std::string rel = relative;
    while (!rel.empty() && (rel.front() == '/' || rel.front() == '\\')) {
        rel.erase(rel.begin());
    }
    return out + rel;
}

std::string PathResolver::dirName(const std::string& path) {
    const std::size_t pos = path.find_last_of("/\\");
    if (pos == std::string::npos) return ".";
    if (pos == 0) return path.substr(0, 1);
    return path.substr(0, pos);
}

std::string PathResolver::executablePath() {
#ifdef _WIN32
    std::vector<char> buffer(MAX_PATH);
    while (true) {
        DWORD n = GetModuleFileNameA(nullptr, buffer.data(),
                                     static_cast<DWORD>(buffer.size()));
        if (n == 0) return "";
        if (n < buffer.size()) return std::string(buffer.data(), n);
        buffer.resize(buffer.size() * 2);
    }
#else
    std::vector<char> buffer(PATH_MAX);
    const ssize_t n = ::readlink("/proc/self/exe", buffer.data(), buffer.size() - 1);
    if (n <= 0) return "";
    return std::string(buffer.data(), static_cast<std::size_t>(n));
#endif
}

std::string PathResolver::userRootDir() {
    // Program Files is read-only to a normally-launched process, so the writable
    // runtime root must be a per-user location. The "ASTRA" component matches the
    // frontend's %LOCALAPPDATA%\ASTRA\logs (see autostartLogDir in main.cpp).
#ifdef _WIN32
    const char* local = std::getenv("LOCALAPPDATA");
    const std::string base =
        (local != nullptr && *local != '\0') ? std::string(local)
                                             : std::string(".");
    return join(base, "ASTRA");
#else
    const char* xdg = std::getenv("XDG_DATA_HOME");
    if (xdg != nullptr && *xdg != '\0') return join(xdg, "astra");
    const char* home = std::getenv("HOME");
    const std::string h = (home != nullptr && *home != '\0') ? std::string(home)
                                                             : std::string(".");
    return join(join(h, ".local/share"), "astra");
#endif
}

bool PathResolver::resolve(AppPaths& out, std::string& error) const {
    const std::string exe = executablePath();
    if (exe.empty()) {
        error = "cannot determine executable path";
        return false;
    }
    out.executablePath = exe;
    out.appRootDir = dirName(exe);
    out.resourceDir = join(out.appRootDir, "resources");
    out.bridgeDir = join(out.resourceDir, "bridge/mt5_python");
    out.bridgeScript = join(out.bridgeDir, "bridge_service.py");
    out.pythonRuntimeDir = join(out.resourceDir, "python");
    out.configDir = join(out.appRootDir, "config");
    out.userRootDir = userRootDir();
    out.dataDir = join(out.userRootDir, "data");
    out.logDir = join(out.userRootDir, "logs");
    error.clear();
    return true;
}

}  // namespace aura
