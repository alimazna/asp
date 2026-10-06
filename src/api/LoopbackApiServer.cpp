// PKG-0008 - Loopback API transport implementation.
//
// A deliberately small HTTP/1.1 server: one request per connection, no
// keep-alive, bounded request size. It exists only to carry the facade
// contract over loopback. It parses the request line and Content-Length, then
// delegates to the facade and forwards the facade's own status code. It never
// inspects backend internals.

#include "api/LoopbackApiServer.h"

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

// Extract a JSON string field's value from a flat object body. This is a
// deliberately minimal reader for the command envelope only; it never
// pretends to be a general JSON parser.
std::string jsonField(const std::string& body, const std::string& key) {
    const std::string needle = "\"" + key + "\"";
    std::size_t pos = body.find(needle);
    if (pos == std::string::npos) return "";
    pos = body.find(':', pos + needle.size());
    if (pos == std::string::npos) return "";
    ++pos;
    while (pos < body.size() && (body[pos] == ' ' || body[pos] == '\t')) ++pos;
    if (pos >= body.size() || body[pos] != '"') return "";
    ++pos;
    std::string out;
    while (pos < body.size() && body[pos] != '"') {
        if (body[pos] == '\\' && pos + 1 < body.size()) {
            ++pos;
            switch (body[pos]) {
                case 'n': out.push_back('\n'); break;
                case 't': out.push_back('\t'); break;
                case 'r': out.push_back('\r'); break;
                default:  out.push_back(body[pos]); break;
            }
        } else {
            out.push_back(body[pos]);
        }
        ++pos;
    }
    return out;
}

const char* reasonPhrase(int status) {
    switch (status) {
        case 200: return "OK";
        case 202: return "Accepted";
        case 400: return "Bad Request";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 413: return "Payload Too Large";
        case 429: return "Too Many Requests";
        case 503: return "Service Unavailable";
        default:  return "Error";
    }
}

std::string encodeHttpResponse(int status, const std::string& body) {
    std::ostringstream out;
    out << "HTTP/1.1 " << status << " " << reasonPhrase(status) << "\r\n"
        << "Content-Type: application/json\r\n"
        << "Content-Length: " << body.size() << "\r\n"
        << "Connection: close\r\n"
        << "\r\n"
        << body;
    return out.str();
}

}  // namespace

LoopbackApiServer::LoopbackApiServer(BackendFacade* facade,
                                     LoopbackApiServerConfig config)
    : facade_(facade), config_(std::move(config)) {}

LoopbackApiServer::~LoopbackApiServer() { stop(); }

bool LoopbackApiServer::start(std::string& error) {
    if (running_.load()) return true;
    if (facade_ == nullptr) {
        error = "loopback api server has no facade";
        return false;
    }
    if (!isLoopbackHost(config_.host)) {
        error = "refusing to bind non-loopback host: " + config_.host;
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
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);   // "localhost" etc.
    }

    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        closeSocket(fd);
        error = "bind() failed on " + config_.host + ":" +
                std::to_string(config_.port);
        return false;
    }
    if (::listen(fd, 8) != 0) {
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

void LoopbackApiServer::stop() {
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
}

void LoopbackApiServer::acceptLoop() {
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

        // Bound how long a client may stall so one connection cannot wedge the
        // accept loop. Loopback clients are local and fast.
        if (config_.readTimeoutMillis > 0) {
            timeval tv{};
            tv.tv_sec = config_.readTimeoutMillis / 1000;
            tv.tv_usec = (config_.readTimeoutMillis % 1000) * 1000;
            setsockopt(conn, SOL_SOCKET, SO_RCVTIMEO,
                       reinterpret_cast<const char*>(&tv), sizeof(tv));
        }

        // Read until the header terminator, then honor Content-Length.
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
        if (headerEnd == std::string::npos) {
            response = errorResponse(400, "malformed_request",
                                     "missing HTTP header terminator");
        } else {
            std::string method = "GET";
            std::string path = "/";
            std::istringstream headerStream(raw.substr(0, headerEnd));
            std::string requestLine;
            std::getline(headerStream, requestLine);
            {
                std::istringstream rl(requestLine);
                std::string version;
                rl >> method >> path >> version;
            }

            std::size_t contentLength = 0;
            std::string line;
            while (std::getline(headerStream, line)) {
                std::string lower = line;
                for (char& c : lower) {
                    c = static_cast<char>(
                        std::tolower(static_cast<unsigned char>(c)));
                }
                if (lower.rfind("content-length:", 0) == 0) {
                    try {
                        contentLength = static_cast<std::size_t>(
                            std::stoul(line.substr(15)));
                    } catch (...) {
                    }
                }
            }

            if (path.empty() || path[0] != '/') {
                response = errorResponse(400, "malformed_request",
                                         "invalid request target");
            } else if (contentLength > config_.maxRequestBytes) {
                response = errorResponse(413, "request_too_large",
                                         "request body exceeds limit");
            } else {
                std::string body = raw.substr(headerEnd + 4);
                while (body.size() < contentLength) {
#ifdef _WIN32
                    int n = ::recv(conn, buffer, sizeof(buffer), 0);
#else
                    ssize_t n = ::recv(conn, buffer, sizeof(buffer), 0);
#endif
                    if (n <= 0) break;
                    body.append(buffer, static_cast<std::size_t>(n));
                }
                if (body.size() > contentLength) body.resize(contentLength);
                response = handleRequest(method, path, body);
            }
        }

        const std::string out =
            encodeHttpResponse(response.status, response.body);
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

ApiResponse LoopbackApiServer::handleRequest(const std::string& method,
                                             const std::string& path,
                                             const std::string& body) {
    if (path == "/api/v1/command") {
        if (method != "POST") {
            return errorResponse(405, "method_not_allowed",
                                 "command route requires POST");
        }
        CommandRequest request;
        request.command = jsonField(body, "command");
        request.actor = jsonField(body, "actor");
        request.payload = jsonField(body, "payload");
        return facade_->command(request);
    }
    // All other paths are read routes owned by the facade (which enforces
    // GET-only, 404, and dependency availability).
    return facade_->handle(method, path);
}

}  // namespace aura
