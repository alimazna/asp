#pragma once
// Minimal HTTP/1.1 client for loopback JSON services.
//
// Only what AURA needs: a GET to a local service with bounded timeouts. It is
// not a general-purpose HTTP stack and deliberately refuses non-loopback hosts.

#include <cstdint>
#include <string>

namespace aura {

struct HttpRequest {
    std::string host = "127.0.0.1";
    std::uint16_t port = 0;
    std::string path = "/";
    std::string method = "GET";
    std::string body;      // request body (POST)
    std::string headers;   // extra "Key: Value\r\n" lines (optional)
    int connectTimeoutMillis = 2000;
    int readTimeoutMillis = 5000;
};

struct HttpResponse {
    bool ok = false;         // transport succeeded (not necessarily 2xx)
    int statusCode = 0;
    std::string body;
    std::string error;

    bool is2xx() const noexcept { return statusCode >= 200 && statusCode < 300; }
};

bool isLoopbackHost(const std::string& host) noexcept;

// Send a request. `request.method` selects GET or POST; POST carries the body
// with an application/json content type. Refuses non-loopback hosts.
HttpResponse httpRequest(const HttpRequest& request);

HttpResponse httpGet(const HttpRequest& request);

}  // namespace aura
