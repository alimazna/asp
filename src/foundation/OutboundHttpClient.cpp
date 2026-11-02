// Outbound HTTPS client implementation (libcurl when available).

#include "foundation/OutboundHttpClient.h"

#ifdef ASTRA_HAVE_CURL
#include <curl/curl.h>

#include <mutex>
#endif

namespace aura {
namespace {

std::string toLowerAscii(std::string s) {
    for (char& c : s) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return s;
}

}  // namespace

std::string OutboundHttpResponse::header(const std::string& name) const {
    const std::string want = toLowerAscii(name);
    for (const auto& kv : headers) {
        if (kv.first == want) return kv.second;
    }
    return std::string();
}

namespace {

#ifdef ASTRA_HAVE_CURL

std::size_t writeCallback(char* data, std::size_t size, std::size_t nmemb,
                          void* userData) {
    const std::size_t total = size * nmemb;
    static_cast<std::string*>(userData)->append(data, total);
    return total;
}

// Collect response headers so callers can read `Retry-After` without parsing
// provider prose. Only the header block of the final response is kept; the
// terminating CRLF pair is ignored.
std::size_t headerCallback(char* data, std::size_t size, std::size_t nmemb,
                           void* userData) {
    const std::size_t total = size * nmemb;
    auto* headers =
        static_cast<std::vector<std::pair<std::string, std::string>>*>(userData);
    std::string line(data, total);
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) {
        line.pop_back();
    }
    const std::size_t colon = line.find(':');
    if (colon != std::string::npos) {
        std::string key = toLowerAscii(line.substr(0, colon));
        std::size_t vs = colon + 1;
        while (vs < line.size() && (line[vs] == ' ' || line[vs] == '\t')) ++vs;
        headers->emplace_back(std::move(key), line.substr(vs));
    }
    return total;
}

// libcurl's global init is process-wide and not thread-safe; guard it once.
void ensureCurlGlobalInit() {
    static std::once_flag once;
    std::call_once(once, []() { curl_global_init(CURL_GLOBAL_DEFAULT); });
}

class CurlHttpClient : public IOutboundHttpClient {
public:
    OutboundHttpResponse send(const OutboundHttpRequest& request) override {
        OutboundHttpResponse response;
        ensureCurlGlobalInit();

        CURL* handle = curl_easy_init();
        if (handle == nullptr) {
            response.error = "curl_easy_init failed";
            return response;
        }

        curl_slist* headerList = nullptr;
        for (const auto& header : request.headers) {
            headerList = curl_slist_append(headerList, header.c_str());
        }

        std::string body;
        std::vector<std::pair<std::string, std::string>> headers;
        curl_easy_setopt(handle, CURLOPT_URL, request.url.c_str());
        curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, writeCallback);
        curl_easy_setopt(handle, CURLOPT_WRITEDATA, &body);
        curl_easy_setopt(handle, CURLOPT_HEADERFUNCTION, headerCallback);
        curl_easy_setopt(handle, CURLOPT_HEADERDATA, &headers);
        curl_easy_setopt(handle, CURLOPT_CONNECTTIMEOUT_MS,
                         static_cast<long>(request.connectTimeoutMillis));
        curl_easy_setopt(handle, CURLOPT_TIMEOUT_MS,
                         static_cast<long>(request.readTimeoutMillis));
        curl_easy_setopt(handle, CURLOPT_FOLLOWLOCATION, 0L);
        // Never forward the Authorization header to a redirect target: redirects
        // are disabled outright, which also prevents an open-redirect from
        // exfiltrating the provider token.
        curl_easy_setopt(handle, CURLOPT_USERAGENT, "astra-news/1.0");
        // TLS is verified explicitly (not merely by libcurl default): a
        // misconfigured CA bundle must fail closed, never fall back to plaintext.
        curl_easy_setopt(handle, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(handle, CURLOPT_SSL_VERIFYHOST, 2L);
        curl_easy_setopt(handle, CURLOPT_USE_SSL, CURLUSESSL_ALL);
        // Restrict to HTTPS for the request and any redirect, so a downgrade to
        // http:// is refused before any credential is sent.
        curl_easy_setopt(handle, CURLOPT_PROTOCOLS_STR, "https");
        curl_easy_setopt(handle, CURLOPT_REDIR_PROTOCOLS_STR, "https");
        // Bound the response body so a hostile or runaway endpoint cannot exhaust
        // memory.
        curl_easy_setopt(handle, CURLOPT_MAXFILESIZE_LARGE,
                         static_cast<curl_off_t>(8 * 1024 * 1024));
        if (headerList != nullptr) {
            curl_easy_setopt(handle, CURLOPT_HTTPHEADER, headerList);
        }
        if (request.method == "POST") {
            curl_easy_setopt(handle, CURLOPT_POST, 1L);
            curl_easy_setopt(handle, CURLOPT_POSTFIELDS, request.body.c_str());
            curl_easy_setopt(handle, CURLOPT_POSTFIELDSIZE,
                             static_cast<long>(request.body.size()));
        } else if (request.method != "GET") {
            curl_easy_setopt(handle, CURLOPT_CUSTOMREQUEST,
                             request.method.c_str());
        }

        const CURLcode code = curl_easy_perform(handle);
        if (code != CURLE_OK) {
            response.ok = false;
            response.error = curl_easy_strerror(code);
        } else {
            long status = 0;
            curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &status);
            response.ok = true;
            response.statusCode = static_cast<int>(status);
            response.body = std::move(body);
            response.headers = std::move(headers);
        }

        if (headerList != nullptr) curl_slist_free_all(headerList);
        curl_easy_cleanup(handle);
        return response;
    }
};

#else  // !ASTRA_HAVE_CURL

// Built without libcurl: every send fails explicitly. The news service maps this
// to an `unavailable` state; it never fabricates an analysis.
class UnavailableHttpClient : public IOutboundHttpClient {
public:
    OutboundHttpResponse send(const OutboundHttpRequest&) override {
        OutboundHttpResponse response;
        response.error = "outbound HTTP transport not compiled (libcurl missing)";
        return response;
    }
};

#endif  // ASTRA_HAVE_CURL

}  // namespace

std::shared_ptr<IOutboundHttpClient> makeDefaultOutboundHttpClient() {
#ifdef ASTRA_HAVE_CURL
    return std::make_shared<CurlHttpClient>();
#else
    return std::make_shared<UnavailableHttpClient>();
#endif
}

}  // namespace aura
