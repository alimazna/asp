#pragma once
// Public HTTPS-facing API server for the deployed ASTRA backend.
//
// Unlike `LoopbackApiServer` (loopback-only by construction), this server binds
// a configurable host (Railway-style `0.0.0.0:$PORT`) and adds the security
// boundary the loopback transport does not need:
//   * bearer-token authentication for ingestion / sync routes;
//   * a CORS allow-list for browser origins;
//   * request size limits and per-connection timeouts.
//
// It intentionally does NOT expose the desktop `POST /api/v1/command` surface.
// Read routes (the frozen v1 contract) are served through the same
// `BackendFacade`.

#include "api/BackendFacade.h"
#include "api/BackendApiSchema.h"
#include "foundation/HttpClient.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace aura {

struct AstraApiServerConfig {
    std::string host = "0.0.0.0";          // public bind for the deployed service
    std::uint16_t port = 8080;
    int readTimeoutMillis = 10000;
    std::size_t maxRequestBytes = 1024 * 1024;
    // Sender bearer tokens accepted for ingestion / sync routes.
    std::vector<std::string> senderTokens;
    // Administrative bearer tokens accepted for the destructive admin surface.
    // Deliberately separate from senderTokens so a leaked ingestion credential
    // cannot purge stored data.
    std::vector<std::string> adminTokens;
    // Read-only client bearer tokens accepted for the private AYZR mobile read
    // surface (news + mobile-specific views). Separate from senderTokens so a
    // leaked read credential cannot write candles. When empty the mobile routes
    // are disabled rather than served unauthenticated.
    std::vector<std::string> clientTokens;
    // Allowed browser origins for CORS (exact match, no trailing slash).
    std::vector<std::string> allowedOrigins;

    // Rate limits for the authenticated write/sync surface, per credential.
    // A denied request returns 429 with a Retry-After hint. 0 disables the
    // corresponding limit (used by tests that must not be throttled).
    int ingestRequestsPerMinute = 240;
    int syncRequestsPerMinute = 240;
    // Largest accepted ingestion batch. A sender paginates; the server refuses
    // an unbounded batch rather than parse it. Enforced in the ingest handler.
    std::size_t maxCandlesPerBatch = 500;
    // Hard cap on concurrent connection-handler threads. Each connection is
    // served on its own thread, so without a cap a client that opens many
    // sockets (or dribbles headers) parks unbounded threads. Excess connections
    // over the cap are refused with 503 rather than queued, so a flood cannot
    // exhaust the process. <= 0 disables the cap (tests that drive many
    // sequential connections).
    int maxConcurrentConnections = 256;
};

class AstraApiServer {
public:
    AstraApiServer(BackendFacade* facade, AstraApiServerConfig config);

    ~AstraApiServer();

    // Bind and start serving. Returns false with `error` on failure.
    bool start(std::string& error);

    void stop();

    bool isRunning() const noexcept { return running_.load(); }
    std::uint16_t port() const noexcept { return boundPort_; }
    const std::string& bindAddress() const noexcept { return config_.host; }

    // Route hooks (exclusive to this server, not in the facade):
    //   POST /api/v1/candles/ingest        -> ingestHandler(body)
    //   GET  /api/v1/sync/latest           -> syncHandler(query)
    //   GET  /api/v1/candles?tf&limit      -> candlesHandler(tf, limit)
    // All three pass only after a valid sender bearer token is presented.
    // syncHandler receives the raw query so a caller can scope reconciliation
    // to its own source identity with ?source_id=...
    std::function<ApiResponse(const std::string&)> ingestHandler;
    std::function<ApiResponse(const std::string&)> syncHandler;
    std::function<ApiResponse(const std::string&, int)> candlesHandler;
    // DELETE /api/v1/admin/candles -> adminDeleteHandler(body). Requires the
    // admin credential (config_.adminTokens), never a sender token.
    std::function<ApiResponse(const std::string&)> adminDeleteHandler;
    // GET /api/v1/admin/candles?tf&limit -> adminListHandler(tf, limit). The
    // quarantine inspection view: it returns rows the guarded read APIs hide,
    // so it too requires the admin credential.
    std::function<ApiResponse(const std::string&, int)> adminListHandler;

    // Private mobile read surface (AYZR). Requires the client credential
    // (config_.clientTokens). Each receives the raw query string.
    //   GET  /api/v1/news                    -> newsListHandler(query)
    //   GET  /api/v1/news/analyze?id=...     -> newsAnalyzeHandler(query)
    //   GET  /api/v1/news/quota              -> newsQuotaHandler()
    //   GET  /api/v1/history                 -> historyHandler(query)
    std::function<ApiResponse(const std::string&)> newsListHandler;
    std::function<ApiResponse(const std::string&)> newsAnalyzeHandler;
    std::function<ApiResponse()> newsQuotaHandler;
    std::function<ApiResponse(const std::string&)> historyHandler;
    //   POST /api/v1/news/ingest             -> newsIngestHandler(body)
    std::function<ApiResponse(const std::string&)> newsIngestHandler;

private:
    void acceptLoop();
    void handleConnection(std::intptr_t conn);
    ApiResponse handleRequest(const std::string& method, const std::string& path,
                              const std::string& query, const std::string& body,
                              bool authenticated, const std::string& authHeader,
                              bool hasCorsOrigin,
                              const std::string& origin, bool& isCorsOk);

    // CORS / auth helpers.
    bool authorize(const std::string& authorizationHeader) const;
    bool authorizeAdmin(const std::string& authorizationHeader) const;
    bool authorizeClient(const std::string& authorizationHeader) const;
    // Per-route throttle for failed authentication. Returns true while the
    // route is under its unauthenticated-probe budget (the route's own limit).
    bool unauthAllowed(const std::string& route, int limitPerMinute);
    bool originAllowed(const std::string& origin) const;

    // Fixed-window, per-credential rate limit. Returns true when the request is
    // allowed, false when the caller must be told to back off. Keyed on the
    // presented bearer token so one sender cannot starve another.
    bool rateAllow(const std::string& route, const std::string& authorizationHeader,
                   int limitPerMinute);

    BackendFacade* facade_;
    AstraApiServerConfig config_;
    std::atomic<bool> running_{false};
    std::thread acceptThread_;
    int listenFd_ = -1;
    std::uint16_t boundPort_ = 0;
    std::mutex workersMutex_;
    std::condition_variable workersCv_;
    int activeWorkers_ = 0;

    // Rate-limit state: open-address-free map guarded by a mutex. Each key is
    // "<route>|<sha-free token id>" -> (windowStartSec, count). The token itself
    // is never stored; a stable FNV-1a hash identifies the credential.
    struct RateWindow {
        std::int64_t windowStartSec = 0;
        int count = 0;
    };
    std::mutex rateMutex_;
    std::map<std::string, RateWindow> rateWindows_;
};

}  // namespace aura