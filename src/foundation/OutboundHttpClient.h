#pragma once
// Outbound HTTPS client for external services (news providers, LLM7).
//
// Distinct from `HttpClient` (foundation/HttpClient.h), which is a loopback-only
// helper for the local bridge. This one deliberately talks to the public
// internet and therefore:
//   * enforces explicit connect/read timeouts so a hung provider cannot stall a
//     request handler;
//   * returns a transport error instead of throwing;
//   * never logs the request body or Authorization header (see the callers).
//
// The transport is pluggable so tests can inject a deterministic fake without a
// network. The default `CurlHttpClient` is compiled only when libcurl is found;
// otherwise `makeDefaultOutboundHttpClient()` returns one that reports a clear
// "transport unavailable" error rather than silently succeeding.

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace aura {

struct OutboundHttpRequest {
    std::string url;                 // absolute, e.g. https://api.llm7.io/v1/...
    std::string method = "POST";
    std::string body;
    std::vector<std::string> headers;  // each "Key: Value"
    int connectTimeoutMillis = 5000;
    int readTimeoutMillis = 30000;
};

struct OutboundHttpResponse {
    bool ok = false;          // transport completed (status may still be 4xx/5xx)
    int statusCode = 0;
    std::string body;
    std::string error;        // transport-level failure detail, empty on ok
    // Response headers, lowercased key -> value (last value wins). Used to read
    // `Retry-After` so a 429 can be classified without parsing provider prose.
    std::vector<std::pair<std::string, std::string>> headers;

    // Case-insensitive header lookup; returns "" when absent.
    std::string header(const std::string& name) const;
};

class IOutboundHttpClient {
public:
    virtual ~IOutboundHttpClient() = default;
    virtual OutboundHttpResponse send(const OutboundHttpRequest& request) = 0;
};

// Returns the libcurl-backed client when built with CURL, else a stub that
// fails every send with a clear error.
std::shared_ptr<IOutboundHttpClient> makeDefaultOutboundHttpClient();

}  // namespace aura
