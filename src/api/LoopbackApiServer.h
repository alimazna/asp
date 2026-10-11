#pragma once
// PKG-0007 - Loopback transport for the frontend-facing API.
//
// The ASTRA frontend talks to the backend over a loopback-only HTTP/JSON
// service. The server is a thin transport over the in-process BackendFacade:
// it owns no state, reaches no runtime internals, and never touches the Python
// bridge or MT5. It refuses to bind anything other than 127.0.0.1.
//
// Routes are exactly the facade contract:
//   GET  <read routes>            -> BackendFacade::handle("GET", path)
//   POST /api/v1/command          -> BackendFacade::command(...)
//
// The command route is the transport binding of the already-defined command
// contract (allow-list + actor attribution enforced by the facade).

#include "api/BackendFacade.h"
#include "foundation/HttpClient.h"   // isLoopbackHost

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

namespace aura {

struct LoopbackApiServerConfig {
    std::string host = "127.0.0.1";   // loopback only; non-loopback is refused
    std::uint16_t port = 8790;        // distinct from the Python bridge port
    int readTimeoutMillis = 5000;
    std::size_t maxRequestBytes = 64 * 1024;
};

class LoopbackApiServer {
public:
    explicit LoopbackApiServer(BackendFacade* facade,
                               LoopbackApiServerConfig config = {});

    ~LoopbackApiServer();

    // Bind and start serving. Returns false (with `error`) if the host is not
    // loopback or the socket cannot be bound. Idempotent while running.
    bool start(std::string& error);

    // Stop accepting and release the socket. Safe to call when not running.
    void stop();

    bool isRunning() const noexcept { return running_.load(); }
    bool isLoopbackOnly() const noexcept { return isLoopbackHost(config_.host); }

    // The actually-bound port (useful when port 0 was requested).
    std::uint16_t port() const noexcept { return boundPort_; }
    const std::string& bindAddress() const noexcept { return config_.host; }

private:
    void acceptLoop();
    // Serves one accepted connection to completion on its own thread. Requests
    // are handled concurrently so a slow proxy (e.g. /candles, which reads MT5
    // through the Python bridge) can never block a fast route such as /health
    // behind it. The facade reads shared state and serializes bridge access
    // internally, so concurrent handling is safe.
    void handleConnection(std::intptr_t conn);
    ApiResponse handleRequest(const std::string& method, const std::string& path,
                              const std::string& body);

    BackendFacade* facade_;
    LoopbackApiServerConfig config_;
    std::atomic<bool> running_{false};
    std::thread acceptThread_;
    int listenFd_ = -1;
    std::uint16_t boundPort_ = 0;
    // Tracks in-flight connection threads so stop() can join them before the
    // server is destroyed (the threads call into `facade_`).
    std::mutex workersMutex_;
    std::condition_variable workersCv_;
    int activeWorkers_ = 0;
};

}  // namespace aura
