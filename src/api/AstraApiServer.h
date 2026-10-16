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
    // Allowed browser origins for CORS (exact match, no trailing slash).
    std::vector<std::string> allowedOrigins;
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

private:
    void acceptLoop();
    void handleConnection(std::intptr_t conn);
    ApiResponse handleRequest(const std::string& method, const std::string& path,
                              const std::string& query, const std::string& body,
                              bool authenticated, bool hasCorsOrigin,
                              const std::string& origin, bool& isCorsOk);

    // CORS / auth helpers.
    bool authorize(const std::string& authorizationHeader) const;
    bool originAllowed(const std::string& origin) const;

    BackendFacade* facade_;
    AstraApiServerConfig config_;
    std::atomic<bool> running_{false};
    std::thread acceptThread_;
    int listenFd_ = -1;
    std::uint16_t boundPort_ = 0;
    std::mutex workersMutex_;
    std::condition_variable workersCv_;
    int activeWorkers_ = 0;
};

}  // namespace aura