// Public HTTPS-facing API server implementation.

#include "api/AstraApiServer.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <sstream>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
using SocketHandle = SOCKET;
static const SocketHandle kInvalidSocket = INVALID_SOCKET;
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
using SocketHandle = int;
static const SocketHandle kInvalidSocket = -1;
#endif

namespace aura {

namespace {

void closeSocket(SocketHandle s) {
#ifdef _WIN32
    closesocket(s);
#else
    ::close(s);
#endif
}

void shutdownSocket(SocketHandle s) {
#ifdef _WIN32
    ::shutdown(s, SD_BOTH);
#else
    ::shutdown(s, SHUT_RDWR);
#endif
}

#ifdef _WIN32
struct WinsockInit {
    WinsockInit() {
        WSADATA data;
        WSAStartup(MAKEWORD(2, 2), &data);
    }
    ~WinsockInit() { WSACleanup(); }
};
#endif

const char* reasonPhrase(int status) {
    switch (status) {
        case 200: return "OK";
        case 201: return "Created";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 413: return "Payload Too Large";
        case 429: return "Too Many Requests";
        case 500: return "Internal Server Error";
        case 503: return "Service Unavailable";
        default:  return "Error";
    }
}

// Content-Length split at a single space is insufficient for "Origin" with
// value; we parse headers manually.
struct ParsedHeaders {
    std::string authorization;
    std::string origin;
    std::string contentType;
    std::size_t contentLength = 0;
};

std::string trim(const std::string& in) {
    std::size_t b = 0;
    std::size_t e = in.size();
    while (b < e && std::isspace(static_cast<unsigned char>(in[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(in[e - 1]))) --e;
    return in.substr(b, e - b);
}

std::string lower(std::string s) {
    for (char& c : s) {
        c = static_cast<char>(
            std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

std::string encodeHttpResponse(int status, const std::string& body,
                               const std::string& extraHeaders) {
    std::ostringstream out;
    out << "HTTP/1.1 " << status << " " << reasonPhrase(status) << "\r\n"
        << "Content-Type: application/json\r\n"
        << "Content-Length: " << body.size() << "\r\n"
        << "Connection: close\r\n"
        << extraHeaders
        << "\r\n"
        << body;
    return out.str();
}

}  // namespace

AstraApiServer::AstraApiServer(BackendFacade* facade, AstraApiServerConfig config)
    : facade_(facade), config_(std::move(config)) {}

AstraApiServer::~AstraApiServer() { stop(); }

bool AstraApiServer::start(std::string& error) {
    if (running_.load()) return true;
    if (facade_ == nullptr) {
        error = "astra api server has no facade";
        return false;
    }

#ifdef _WIN32
    static WinsockInit winsock;
#endif

    SocketHandle fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd == kInvalidSocket) {
        error = "socket() failed";
        return false;
    }

    int reuse = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char*>(&reuse), sizeof(reuse));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(config_.port);
    if (::inet_pton(AF_INET, config_.host.c_str(), &addr.sin_addr) != 1) {
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
    }

    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        closeSocket(fd);
        error = "bind() failed on " + config_.host + ":" +
                std::to_string(config_.port);
        return false;
    }
    if (::listen(fd, 16) != 0) {
        closeSocket(fd);
        error = "listen() failed";
        return false;
    }

    sockaddr_in bound{};
#ifdef _WIN32
    int boundLen = sizeof(bound);
#else
    socklen_t boundLen = sizeof(bound);
#endif
    if (::getsockname(fd, reinterpret_cast<sockaddr*>(&bound), &boundLen) == 0) {
        boundPort_ = ntohs(bound.sin_port);
    } else {
        boundPort_ = config_.port;
    }

    listenFd_ = static_cast<int>(fd);
    running_.store(true);
    acceptThread_ = std::thread([this] { acceptLoop(); });
    return true;
}

void AstraApiServer::stop() {
    if (!running_.exchange(false)) {
        if (listenFd_ >= 0) {
            closeSocket(static_cast<SocketHandle>(listenFd_));
            listenFd_ = -1;
        }
        return;
    }
    if (listenFd_ >= 0) {
        shutdownSocket(static_cast<SocketHandle>(listenFd_));
        closeSocket(static_cast<SocketHandle>(listenFd_));
        listenFd_ = -1;
    }
    if (acceptThread_.joinable()) acceptThread_.join();
    {
        std::unique_lock<std::mutex> lock(workersMutex_);
        workersCv_.wait(lock, [this] { return activeWorkers_ == 0; });
    }
}

void AstraApiServer::acceptLoop() {
    while (running_.load()) {
        const SocketHandle listen = static_cast<SocketHandle>(listenFd_);
        if (listen == kInvalidSocket) break;
        sockaddr_in client{};
#ifdef _WIN32
        int clientLen = sizeof(client);
#else
        socklen_t clientLen = sizeof(client);
#endif
        SocketHandle conn = ::accept(listen, reinterpret_cast<sockaddr*>(&client),
                                     &clientLen);
        if (conn == kInvalidSocket) {
            if (!running_.load()) break;
            continue;
        }

        if (config_.readTimeoutMillis > 0) {
            timeval tv{};
            tv.tv_sec = config_.readTimeoutMillis / 1000;
            tv.tv_usec = (config_.readTimeoutMillis % 1000) * 1000;
            setsockopt(conn, SOL_SOCKET, SO_RCVTIMEO,
                       reinterpret_cast<const char*>(&tv), sizeof(tv));
        }

        {
            std::lock_guard<std::mutex> lock(workersMutex_);
            ++activeWorkers_;
        }
        std::thread([this, conn]() {
            handleConnection(static_cast<std::intptr_t>(conn));
            {
                std::lock_guard<std::mutex> lock(workersMutex_);
                --activeWorkers_;
            }
            workersCv_.notify_all();
        }).detach();
    }
}

void AstraApiServer::handleConnection(std::intptr_t connHandle) {
    const SocketHandle conn = static_cast<SocketHandle>(connHandle);
    bool hasOrigin = false;
    std::string origin;
    {
        std::string raw;
        char buffer[4096];
        std::size_t headerEnd = std::string::npos;
        while (true) {
#ifdef _WIN32
            int n = ::recv(conn, buffer, sizeof(buffer), 0);
#else
            ssize_t n = ::recv(conn, buffer, sizeof(buffer), 0);
#endif
            if (n <= 0) break;
            raw.append(buffer, static_cast<std::size_t>(n));
            headerEnd = raw.find("\r\n\r\n");
            if (headerEnd != std::string::npos) break;
            if (raw.size() > config_.maxRequestBytes) break;
        }

        ApiResponse response;
        bool corsOk = false;
        if (headerEnd == std::string::npos) {
            response = errorResponse(400, "malformed_request",
                                     "missing HTTP header terminator");
        } else {
            std::string method = "GET";
            std::string target = "/";
            std::istringstream headerStream(raw.substr(0, headerEnd));
            std::string requestLine;
            std::getline(headerStream, requestLine);
            {
                std::istringstream rl(requestLine);
                std::string version;
                rl >> method >> target >> version;
            }

            ParsedHeaders headers;
            std::string line;
            while (std::getline(headerStream, line)) {
                const std::size_t colon = line.find(':');
                if (colon == std::string::npos) continue;
                const std::string name = lower(trim(line.substr(0, colon)));
                const std::string value = trim(line.substr(colon + 1));
                if (name == "authorization") headers.authorization = value;
                else if (name == "origin") headers.origin = value;
                else if (name == "content-type") headers.contentType = value;
                else if (name == "content-length") {
                    try {
                        headers.contentLength = static_cast<std::size_t>(
                            std::stoul(value));
                    } catch (...) {
                    }
                }
            }

            const bool authenticated = authorize(headers.authorization);
            hasOrigin = !headers.origin.empty();
            origin = headers.origin;
            corsOk = !hasOrigin || originAllowed(origin);

            if (corsOk && method == "OPTIONS") {
                // CORS preflight. ignored OPTIONS is still answered 204 with
                // permissive CORS headers only for allow-listed origins.
                const std::string out =
                    "HTTP/1.1 204 No Content\r\n"
                    "Access-Control-Allow-Origin: " +
                    (hasOrigin ? origin : std::string("*")) +
                    "\r\n"
                    "Access-Control-Allow-Headers: Authorization, Content-Type\r\n"
                    "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
                    "Access-Control-Max-Age: 86400\r\n"
                    "Content-Length: 0\r\n"
                    "Connection: close\r\n"
                    "\r\n";
                {
                    std::size_t sent = 0;
                    while (sent < out.size()) {
#ifdef _WIN32
                        int n = ::send(conn, out.data() + sent,
                                       static_cast<int>(out.size() - sent), 0);
#else
                        ssize_t n = ::send(conn, out.data() + sent, out.size() - sent, 0);
#endif
                        if (n <= 0) break;
                        sent += static_cast<std::size_t>(n);
                    }
                }
                shutdownSocket(conn);
                closeSocket(conn);
                return;
            }

            if (!corsOk) {
                response = errorResponse(403, "origin_not_allowed",
                                         "origin is not allow-listed");
            } else if (target.empty() || target[0] != '/') {
                response = errorResponse(400, "malformed_request",
                                         "invalid request target");
            } else if (headers.contentLength > config_.maxRequestBytes) {
                response = errorResponse(413, "request_too_large",
                                         "request body exceeds limit");
            } else {
                std::string body = raw.substr(headerEnd + 4);
                while (body.size() < headers.contentLength) {
#ifdef _WIN32
                    int n = ::recv(conn, buffer, sizeof(buffer), 0);
#else
                    ssize_t n = ::recv(conn, buffer, sizeof(buffer), 0);
#endif
                    if (n <= 0) break;
                    body.append(buffer, static_cast<std::size_t>(n));
                }
                if (body.size() > headers.contentLength) {
                    body.resize(headers.contentLength);
                }
                std::string path = target;
                std::string query;
                const std::size_t q = path.find('?');
                if (q != std::string::npos) {
                    query = path.substr(q + 1);
                    path = path.substr(0, q);
                }
                response = handleRequest(method, path, query, body,
                                         authenticated, headers.authorization,
                                         hasOrigin, origin, corsOk);
            }
        }

        // Reflect the (already allow-listed) origin so browsers can read the
        // response. `origin` is empty for non-browser clients and no CORS
        // header is emitted then.
        std::string corsHeaders;
        if (corsOk && hasOrigin) {
            corsHeaders += "Access-Control-Allow-Origin: " + origin + "\r\n";
            corsHeaders += "Vary: Origin\r\n";
        }
        const std::string out =
            encodeHttpResponse(response.status, response.body,
                               corsHeaders + response.headers);
        std::size_t sent = 0;
        while (sent < out.size()) {
#ifdef _WIN32
            int n = ::send(conn, out.data() + sent,
                           static_cast<int>(out.size() - sent), 0);
#else
            ssize_t n = ::send(conn, out.data() + sent, out.size() - sent, 0);
#endif
            if (n <= 0) break;
            sent += static_cast<std::size_t>(n);
        }
        shutdownSocket(conn);
        closeSocket(conn);
    }
}

bool AstraApiServer::authorize(const std::string& authorizationHeader) const {
    if (authorizationHeader.empty() || config_.senderTokens.empty()) return false;
    // Expect "Bearer <token>".
    constexpr const char* kBearer = "Bearer ";
    const std::string trimmed = trim(authorizationHeader);
    if (trimmed.rfind(kBearer, 0) != 0) return false;
    const std::string presented = trim(trimmed.substr(6));
    if (presented.empty()) return false;
    for (const auto& token : config_.senderTokens) {
        if (presented == token) return true;
    }
    return false;
}

bool AstraApiServer::authorizeAdmin(const std::string& authorizationHeader) const {
    // Admin tokens are checked against a SEPARATE list from sender tokens, so a
    // leaked ingestion credential can never reach the destructive surface.
    if (authorizationHeader.empty() || config_.adminTokens.empty()) return false;
    constexpr const char* kBearer = "Bearer ";
    const std::string trimmed = trim(authorizationHeader);
    if (trimmed.rfind(kBearer, 0) != 0) return false;
    const std::string presented = trim(trimmed.substr(6));
    if (presented.empty()) return false;
    for (const auto& token : config_.adminTokens) {
        if (presented == token) return true;
    }
    return false;
}

bool AstraApiServer::originAllowed(const std::string& origin) const {
    if (origin.empty()) return true;   // non-browser requests need no CORS
    for (const auto& allowed : config_.allowedOrigins) {
        if (allowed == origin) return true;
    }
    return false;
}

bool AstraApiServer::rateAllow(const std::string& route,
                               const std::string& authorizationHeader,
                               int limitPerMinute) {
    if (limitPerMinute <= 0) return true;   // disabled

    // Identify the credential without storing it: FNV-1a over the raw header is
    // enough to separate callers and is not reversible.
    std::uint64_t hash = 1469598103934665603ULL;
    for (char c : authorizationHeader) {
        hash ^= static_cast<unsigned char>(c);
        hash *= 1099511628211ULL;
    }
    const std::string key = route + "|" + std::to_string(hash);

    const std::int64_t nowSec = Timestamp::now().epochMillis() / 1000;
    constexpr std::int64_t kWindowSec = 60;

    std::lock_guard<std::mutex> lock(rateMutex_);
    RateWindow& window = rateWindows_[key];
    if (nowSec - window.windowStartSec >= kWindowSec) {
        window.windowStartSec = nowSec;
        window.count = 0;
    }
    if (window.count >= limitPerMinute) {
        return false;
    }
    ++window.count;
    return true;
}

ApiResponse AstraApiServer::handleRequest(
    const std::string& method, const std::string& path, const std::string& query,
    const std::string& body, bool authenticated, const std::string& authHeader,
    bool hasCorsOrigin, const std::string& origin, bool& isCorsOk) {

    // Ingestion and sync routes require the sender credential. The destructive
    // admin route requires the separate admin credential.
    const bool requiresAuth =
        (path == "/api/v1/candles/ingest") || (path == "/api/v1/sync/latest");

    if (requiresAuth && !authenticated) {
        return errorResponse(401, "unauthorized",
                             "a valid sender bearer token is required");
    }

    if (path == "/api/v1/admin/candles") {
        if (method != "DELETE" && method != "GET") {
            return errorResponse(405, "method_not_allowed",
                                 "admin candle route requires DELETE or GET");
        }
        if (!authorizeAdmin(authHeader)) {
            return errorResponse(401, "unauthorized",
                                 "a valid admin bearer token is required");
        }
        if (!rateAllow("admin", authHeader, config_.ingestRequestsPerMinute)) {
            ApiResponse tooMany = errorResponse(
                429, "rate_limited",
                "admin rate limit exceeded; retry after backoff");
            tooMany.headers += "Retry-After: 60\r\n";
            return tooMany;
        }
        if (method == "GET") {
            // Inspection mode: list the raw rows (including any the guarded read
            // APIs withhold) so an operator can identify a contaminating row
            // before purging it by full identity.
            std::string tf = "M15";
            int limit = 500;
            std::istringstream qs(query);
            std::string pair;
            while (std::getline(qs, pair, '&')) {
                const std::size_t eq = pair.find('=');
                if (eq == std::string::npos) continue;
                const std::string key = pair.substr(0, eq);
                const std::string value = pair.substr(eq + 1);
                if (key == "tf" || key == "timeframe") {
                    if (!value.empty()) tf = value;
                } else if (key == "limit") {
                    try {
                        limit = std::stoi(value);
                    } catch (...) {
                    }
                }
            }
            if (limit < 1 || limit > 1000) limit = 500;
            if (adminListHandler) return adminListHandler(tf, limit);
            return errorResponse(503, "dependency_unavailable",
                                 "admin list handler not configured");
        }
        if (body.empty()) {
            return errorResponse(400, "malformed_request",
                                 "admin delete requires a JSON body");
        }
        if (adminDeleteHandler) return adminDeleteHandler(body);
        return errorResponse(503, "dependency_unavailable",
                             "admin delete handler not configured");
    }

    if (path == "/api/v1/candles/ingest") {
        if (method != "POST") {
            return errorResponse(405, "method_not_allowed",
                                 "ingest route requires POST");
        }
        if (!rateAllow("ingest", authHeader,
                       config_.ingestRequestsPerMinute)) {
            ApiResponse tooMany = errorResponse(
                429, "rate_limited",
                "ingestion rate limit exceeded; retry after backoff");
            tooMany.headers += "Retry-After: 60\r\n";
            return tooMany;
        }
        if (body.empty()) {
            return errorResponse(400, "malformed_request",
                                 "ingest requires a JSON body");
        }
        if (ingestHandler) return ingestHandler(body);
        return errorResponse(503, "dependency_unavailable",
                             "ingest handler not configured");
    }

    if (path == "/api/v1/sync/latest") {
        if (method != "GET") {
            return errorResponse(405, "method_not_allowed",
                                 "sync route requires GET");
        }
        if (!rateAllow("sync", authHeader,
                       config_.syncRequestsPerMinute)) {
            ApiResponse tooMany = errorResponse(
                429, "rate_limited",
                "sync rate limit exceeded; retry after backoff");
            tooMany.headers += "Retry-After: 60\r\n";
            return tooMany;
        }
        if (syncHandler) return syncHandler(query);
        return errorResponse(503, "dependency_unavailable",
                             "sync handler not configured");
    }

    // Full contract for the chart route from the persisted candle store.
    if (path == "/api/v1/candles") {
        if (method != "GET") {
            return errorResponse(405, "method_not_allowed",
                                 "candles route requires GET");
        }
        std::string tf = "M15";
        int limit = 500;
        // Parse ?tf=...&limit=... from the query string (form-encoded keys).
        // `timeframe` is accepted as an alias for `tf`: the documented wire
        // contract uses `tf`, but the response field, the ingestion request and
        // the MT5 bridge all spell it `timeframe`, so a caller can reasonably
        // send either. Without the alias such a caller silently received M15 —
        // a wrong answer with no error.
        std::istringstream qs(query);
        std::string pair;
        while (std::getline(qs, pair, '&')) {
            const std::size_t eq = pair.find('=');
            if (eq == std::string::npos) continue;
            const std::string key = pair.substr(0, eq);
            const std::string value = pair.substr(eq + 1);
            if (key == "tf" || key == "timeframe") tf = value;
            else if (key == "limit") {
                try {
                    limit = std::stoi(value);
                } catch (...) {
                }
            }
        }
        if (candlesHandler) return candlesHandler(tf, limit);
        return errorResponse(503, "dependency_unavailable",
                             "candles handler not configured");
    }

    // Everything else is the frozen read surface served by the facade.
    (void)body;
    (void)hasCorsOrigin;
    (void)origin;
    (void)isCorsOk;
    return facade_->handle(method, path, query);
}

}  // namespace aura