// TST-0012 - Single-instance guard and idempotent-spawn port probe.
//
// Bug 2: the installed app spawned duplicate backends and bridges because the
// only guard was a (never-created) mutex, and the loopback port probe alone
// cannot close the pre-bind race. These tests pin the two mechanisms that
// replace it: an exclusive lock file, and a real TCP listener probe.

#include "platform/windows/SingleInstance.h"
#include "foundation/HttpClient.h"

#include <cstdio>
#include <string>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace {

int g_failures = 0;

void check(bool condition, const char* name) {
    std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", name);
    if (!condition) ++g_failures;
}

std::string tempLockPath(const char* name) {
#ifdef _WIN32
    const char* base = std::getenv("TEMP");
    std::string dir = base ? base : ".";
#else
    std::string dir = "/tmp";
#endif
    return dir + "/astra-" + name + ".lock";
}

#ifdef _WIN32
struct WinsockInit {
    WinsockInit() {
        WSADATA d;
        WSAStartup(MAKEWORD(2, 2), &d);
    }
};
int closeSock(SOCKET s) { return closesocket(s); }
using Sock = SOCKET;
constexpr Sock kBad = INVALID_SOCKET;
#else
using Sock = int;
constexpr Sock kBad = -1;
int closeSock(Sock s) { return ::close(s); }
#endif

// Bind and listen on an ephemeral loopback port; returns the port, or 0.
unsigned short listenOnLoopback(Sock& out) {
    Sock s = ::socket(AF_INET, SOCK_STREAM, 0);
    if (s == kBad) return 0;
    int one = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char*>(&one), sizeof(one));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = 0;  // ephemeral
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        closeSock(s);
        return 0;
    }
    if (::listen(s, 4) != 0) {
        closeSock(s);
        return 0;
    }
    sockaddr_in actual{};
    socklen_t len = sizeof(actual);
    if (::getsockname(s, reinterpret_cast<sockaddr*>(&actual), &len) != 0) {
        closeSock(s);
        return 0;
    }
    out = s;
    return ntohs(actual.sin_port);
}

}  // namespace

int main() {
#ifdef _WIN32
    WinsockInit winsock;
#endif

    // 1. A second acquire on the same path must fail while the first is held.
    {
        const std::string path = tempLockPath("single-instance-a");
        aura::InstanceLock first;
        std::string err;
        check(first.acquire(path, err), "first lock acquire succeeds");
        check(first.held(), "first lock reports held");

        aura::InstanceLock second;
        std::string err2;
        check(!second.acquire(path, err2), "second concurrent lock acquire is refused");
        check(!second.held(), "second lock is not held");

        first.release();
        check(!first.held(), "released lock is not held");

        aura::InstanceLock third;
        std::string err3;
        check(third.acquire(path, err3), "lock can be re-acquired after release");
        third.release();
    }

    // 2. The port probe reports a real listener and a dead port distinctly.
    {
        Sock listener = kBad;
        const unsigned short port = listenOnLoopback(listener);
        check(port != 0, "test listener bound to a loopback port");
        if (port != 0) {
            check(aura::loopbackPortListening(port, 500),
                  "loopbackPortListening true for a live listener");
            closeSock(listener);
            // After close the port is no longer answering.
            check(!aura::loopbackPortListening(port, 300),
                  "loopbackPortListening false for a closed port");
        }
        // A port nobody listens on (bridge default, unused in tests).
        check(!aura::loopbackPortListening(18791, 200),
              "loopbackPortListening false for an unused port");
    }

    std::printf("Single-instance tests: %s\n",
                g_failures == 0 ? "all passed" : "FAILED");
    return g_failures == 0 ? 0 : 1;
}
