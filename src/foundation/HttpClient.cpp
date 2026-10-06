// Minimal loopback HTTP client.

#include "foundation/HttpClient.h"

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
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
using SocketHandle = int;
static const SocketHandle kInvalidSocket = -1;
#endif

namespace aura {

bool isLoopbackHost(const std::string& host) noexcept {
    if (host == "127.0.0.1" || host == "localhost" || host == "::1") return true;
    // Any address in 127.0.0.0/8.
    if (host.rfind("127.", 0) == 0) return true;
    return false;
}

namespace {

void closeSocket(SocketHandle s) {
#ifdef _WIN32
    closesocket(s);
#else
    ::close(s);
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

void applyTimeouts(SocketHandle s, int connectMs, int readMs) {
#ifdef _WIN32
    DWORD tv = static_cast<DWORD>(readMs);
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&tv), sizeof(tv));
    tv = static_cast<DWORD>(connectMs);
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&tv), sizeof(tv));
#else
    timeval rtv{};
    rtv.tv_sec = readMs / 1000;
    rtv.tv_usec = (readMs % 1000) * 1000;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &rtv, sizeof(rtv));
    timeval ctv{};
    ctv.tv_sec = connectMs / 1000;
    ctv.tv_usec = (connectMs % 1000) * 1000;
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &ctv, sizeof(ctv));
#endif
}

}  // namespace

HttpResponse httpGet(const HttpRequest& request) {
    HttpResponse response;
    if (!isLoopbackHost(request.host)) {
        response.error = "httpGet refuses non-loopback host: " + request.host;
        return response;
    }

#ifdef _WIN32
    static WinsockInit winsock;
#endif

    SocketHandle sock = ::socket(AF_INET, SOCK_STREAM, 0);
    if (sock == kInvalidSocket) {
        response.error = "socket() failed";
        return response;
    }
    applyTimeouts(sock, request.connectTimeoutMillis, request.readTimeoutMillis);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(request.port);
    if (::inet_pton(AF_INET, request.host.c_str(), &addr.sin_addr) != 1) {
        // Fall back to localhost if a non-numeric loopback name was used.
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    }

    if (::connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        closeSocket(sock);
        response.error = "connect failed";
        return response;
    }

    std::ostringstream req;
    req << "GET " << request.path << " HTTP/1.1\r\n"
        << "Host: " << request.host << ":" << request.port << "\r\n"
        << "Connection: close\r\n"
        << request.headers
        << "\r\n";
    const std::string requestText = req.str();

    std::size_t sent = 0;
    while (sent < requestText.size()) {
#ifdef _WIN32
        int n = ::send(sock, requestText.data() + sent,
                       static_cast<int>(requestText.size() - sent), 0);
#else
        ssize_t n = ::send(sock, requestText.data() + sent,
                           requestText.size() - sent, 0);
#endif
        if (n <= 0) {
            closeSocket(sock);
            response.error = "send failed";
            return response;
        }
        sent += static_cast<std::size_t>(n);
    }

    std::string raw;
    char buffer[4096];
    while (true) {
#ifdef _WIN32
        int n = ::recv(sock, buffer, sizeof(buffer), 0);
#else
        ssize_t n = ::recv(sock, buffer, sizeof(buffer), 0);
#endif
        if (n <= 0) break;
        raw.append(buffer, static_cast<std::size_t>(n));
        if (raw.size() > 64u * 1024u * 1024u) break;  // safety bound
    }
    closeSocket(sock);

    const std::size_t headerEnd = raw.find("\r\n\r\n");
    if (headerEnd == std::string::npos) {
        response.error = "malformed HTTP response (no header terminator)";
        return response;
    }

    const std::string headerBlock = raw.substr(0, headerEnd);
    response.body = raw.substr(headerEnd + 4);

    std::istringstream headerStream(headerBlock);
    std::string statusLine;
    std::getline(headerStream, statusLine);
    {
        std::istringstream sl(statusLine);
        std::string httpVersion;
        sl >> httpVersion >> response.statusCode;
    }
    if (response.statusCode == 0) {
        response.error = "malformed HTTP status line";
        return response;
    }

    // Honor Content-Length for chunkless responses.
    std::string line;
    while (std::getline(headerStream, line)) {
        std::string lower = line;
        for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (lower.rfind("content-length:", 0) == 0) {
            try {
                std::size_t len = static_cast<std::size_t>(std::stoul(line.substr(15)));
                if (response.body.size() >= len) response.body.resize(len);
            } catch (...) {
            }
        }
    }

    response.ok = true;
    return response;
}

}  // namespace aura
