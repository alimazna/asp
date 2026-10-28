// LLM7 provider adapter implementation.

#include "news/Llm7Provider.h"

#include "foundation/Json.h"

namespace aura {

Llm7Provider::Llm7Provider(Llm7Config config,
                           std::shared_ptr<IOutboundHttpClient> http)
    : config_(std::move(config)), http_(std::move(http)) {}

std::string Llm7Provider::buildRequestBody(const LlmRequest& request) const {
    JsonValue root = JsonValue::object();
    root.set("model", JsonValue(request.model.empty() ? config_.model
                                                      : request.model));
    JsonValue messages = JsonValue::array();
    for (const auto& m : request.messages) {
        JsonValue obj = JsonValue::object();
        obj.set("role", JsonValue(m.role));
        obj.set("content", JsonValue(m.content));
        messages.push(std::move(obj));
    }
    root.set("messages", std::move(messages));
    root.set("stream", JsonValue(false));
    root.set("temperature", JsonValue(request.temperature));
    return root.dump();
}

LlmOutcome Llm7Provider::classify(bool transportOk, int status) {
    if (!transportOk) return LlmOutcome::TransportError;
    if (status >= 200 && status < 300) return LlmOutcome::Ok;
    if (status == 401 || status == 403) return LlmOutcome::AuthFailure;
    if (status == 429) return LlmOutcome::RateLimited;
    if (status == 408 || status == 504) return LlmOutcome::Timeout;
    if (status >= 500) return LlmOutcome::ServerError;
    return LlmOutcome::BadRequest;
}

namespace {

std::string lowerAscii(std::string s) {
    for (char& c : s) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return s;
}

// Trim ASCII whitespace from both ends.
std::string trim(const std::string& s) {
    std::size_t b = 0, e = s.size();
    while (b < e && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r' ||
                     s[b] == '\n')) {
        ++b;
    }
    while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r' ||
                     s[e - 1] == '\n')) {
        --e;
    }
    return s.substr(b, e - b);
}

}  // namespace

bool Llm7Provider::isQuotaExhaustedBody(const std::string& body) {
    // The markers below are the ones the LLM7/OpenAI-compatible error envelope
    // uses for a spent daily budget. They are deliberately specific: a bare 429
    // (temporary rate limit) must NOT be classified as quota exhaustion.
    const std::string b = lowerAscii(body);
    return b.find("insufficient_quota") != std::string::npos ||
           b.find("quota_exceeded") != std::string::npos ||
           b.find("daily token quota") != std::string::npos ||
           b.find("exceeded your current quota") != std::string::npos;
}

int Llm7Provider::parseRetryAfterSeconds(const std::string& retryAfterHeader,
                                         const std::string& body) {
    // 1. Retry-After header. RFC 9110 allows either delay-seconds or an
    //    HTTP-date. Only delay-seconds is parsed here; a date would require
    //    clock reconciliation and is left to the caller as "unknown".
    const std::string h = trim(retryAfterHeader);
    if (!h.empty()) {
        bool allDigits = true;
        for (char c : h) {
            if (c < '0' || c > '9') { allDigits = false; break; }
        }
        if (allDigits && h.size() <= 9) {
            try {
                return std::stoi(h);
            } catch (...) {
                // fall through to the body
            }
        }
    }
    // 2. `retry_after` numeric field in the JSON body (LLM7 includes this).
    JsonValue parsed;
    std::string parseError;
    if (JsonValue::parse(body, parsed, parseError)) {
        const JsonValue& err = parsed["error"];
        const JsonValue& direct = parsed["retry_after"];
        if (direct.isNumber()) {
            return static_cast<int>(direct.asInt64());
        }
        if (err.isObject()) {
            const JsonValue& nested = err["retry_after"];
            if (nested.isNumber()) {
                return static_cast<int>(nested.asInt64());
            }
        }
    }
    return -1;
}

LlmResult Llm7Provider::complete(const LlmRequest& request) {
    LlmResult result;
    // Attribution is set once here so every return path reports the same
    // honest identity, including the early "no credential" return.
    result.provider = config_.providerLabel;
    result.model = request.model.empty() ? config_.model : request.model;

    if (config_.apiToken.empty()) {
        // No credential configured: report an auth failure so the caller emits
        // an `unavailable` analysis rather than attempting an anonymous call.
        result.outcome = LlmOutcome::AuthFailure;
        result.error = "LLM7_API_TOKEN is not configured";
        return result;
    }

    OutboundHttpRequest http;
    http.url = config_.baseUrl + "/chat/completions";
    http.method = "POST";
    http.body = buildRequestBody(request);
    http.connectTimeoutMillis = config_.connectTimeoutMillis;
    http.readTimeoutMillis = request.timeoutMillis > 0
                                 ? request.timeoutMillis
                                 : config_.readTimeoutMillis;
    http.headers.push_back("Content-Type: application/json");
    http.headers.push_back("Accept: application/json");
    http.headers.push_back("Authorization: Bearer " + config_.apiToken);

    const OutboundHttpResponse response = http_->send(http);
    result.httpStatus = response.statusCode;
    result.outcome = classify(response.ok, response.statusCode);

    if (!response.ok) {
        // Transport-level failure. `error` is libcurl's message, which contains
        // no credential material.
        result.error = response.error;
        return result;
    }

    // Refine a 429 into quota exhaustion vs. a temporary rate limit, and record
    // any provider-supplied retry delay. This must happen before the generic
    // error branch below so the caller can cool down correctly.
    if (response.statusCode == 429) {
        if (isQuotaExhaustedBody(response.body)) {
            result.outcome = LlmOutcome::QuotaExhausted;
        } else {
            result.outcome = LlmOutcome::RateLimited;
        }
        result.retryAfterSeconds = parseRetryAfterSeconds(
            response.header("retry-after"), response.body);
    }

    if (result.outcome != LlmOutcome::Ok) {
        // Do not echo the full body (it may be large); keep a short, safe slice.
        result.error = "provider returned HTTP " +
                       std::to_string(response.statusCode);
        if (!response.body.empty()) {
            result.error += ": " + response.body.substr(0, 200);
        }
        return result;
    }

    // Extract choices[0].message.content. Defensive: any structural surprise is
    // a BadRequest outcome, never a crash and never a fabricated value.
    JsonValue parsed;
    std::string parseError;
    if (!JsonValue::parse(response.body, parsed, parseError)) {
        result.outcome = LlmOutcome::BadRequest;
        result.error = "cannot parse provider response: " + parseError;
        return result;
    }
    const JsonValue& choices = parsed["choices"];
    if (!choices.isArray() || choices.items().empty()) {
        result.outcome = LlmOutcome::BadRequest;
        result.error = "provider response has no choices[]";
        return result;
    }
    const JsonValue& message = choices.items().front()["message"];
    const std::string content = message["content"].asString();
    if (content.empty()) {
        result.outcome = LlmOutcome::BadRequest;
        result.error = "provider response has empty message content";
        return result;
    }
    result.content = content;
    return result;
}

}  // namespace aura
