// Google Gemini provider adapter implementation.

#include "news/GeminiProvider.h"

#include "foundation/Json.h"

namespace aura {

GeminiProvider::GeminiProvider(GeminiConfig config,
                               std::shared_ptr<IOutboundHttpClient> http)
    : config_(std::move(config)), http_(std::move(http)) {}

std::string GeminiProvider::buildUrl() const {
    return config_.baseUrl + "/models/" + config_.model + ":generateContent";
}

std::string GeminiProvider::buildRequestBody(const LlmRequest& request) const {
    JsonValue root = JsonValue::object();

    // Gemini separates the system prompt from the turn list.
    JsonValue systemParts = JsonValue::array();
    JsonValue contents = JsonValue::array();
    for (const auto& m : request.messages) {
        if (m.role == "system") {
            JsonValue part = JsonValue::object();
            part.set("text", JsonValue(m.content));
            systemParts.push(std::move(part));
            continue;
        }
        // Gemini names the assistant role "model"; anything else is "user".
        JsonValue content = JsonValue::object();
        content.set("role", JsonValue(m.role == "assistant" ? "model" : "user"));
        JsonValue part = JsonValue::object();
        part.set("text", JsonValue(m.content));
        JsonValue parts = JsonValue::array();
        parts.push(std::move(part));
        content.set("parts", std::move(parts));
        contents.push(std::move(content));
    }
    if (!systemParts.items().empty()) {
        JsonValue instruction = JsonValue::object();
        instruction.set("parts", std::move(systemParts));
        root.set("systemInstruction", std::move(instruction));
    }
    root.set("contents", std::move(contents));

    JsonValue generation = JsonValue::object();
    generation.set("temperature", JsonValue(request.temperature));
    if (config_.jsonMode) {
        generation.set("responseMimeType", JsonValue("application/json"));
    }
    root.set("generationConfig", std::move(generation));
    return root.dump();
}

LlmOutcome GeminiProvider::classify(bool transportOk, int status) {
    if (!transportOk) return LlmOutcome::TransportError;
    if (status >= 200 && status < 300) return LlmOutcome::Ok;
    if (status == 401 || status == 403) return LlmOutcome::AuthFailure;
    if (status == 429) return LlmOutcome::RateLimited;
    if (status == 408 || status == 504) return LlmOutcome::Timeout;
    // Google reports "model overloaded" as 503. That is a transient capacity
    // condition, so it is retryable like a 5xx rather than a bad request.
    if (status >= 500) return LlmOutcome::ServerError;
    return LlmOutcome::BadRequest;
}

bool GeminiProvider::extractText(const std::string& body, std::string& out,
                                 std::string& error) {
    JsonValue parsed;
    std::string parseError;
    if (!JsonValue::parse(body, parsed, parseError)) {
        error = "cannot parse provider response: " + parseError;
        return false;
    }
    // A blocked prompt or a safety stop has no candidates; report that rather
    // than returning an empty analysis as if it were a valid one.
    const JsonValue& candidates = parsed["candidates"];
    if (!candidates.isArray() || candidates.items().empty()) {
        const JsonValue& feedback = parsed["promptFeedback"];
        const std::string reason = feedback["blockReason"].asString();
        error = reason.empty() ? "provider response has no candidates[]"
                               : ("prompt blocked by provider: " + reason);
        return false;
    }
    const JsonValue& parts = candidates.items().front()["content"]["parts"];
    if (!parts.isArray() || parts.items().empty()) {
        error = "provider response has no content parts";
        return false;
    }
    std::string text;
    for (const auto& part : parts.items()) {
        const std::string piece = part["text"].asString();
        if (!piece.empty()) {
            if (!text.empty()) text += "\n";
            text += piece;
        }
    }
    if (text.empty()) {
        error = "provider response has empty text";
        return false;
    }
    out = std::move(text);
    return true;
}

int GeminiProvider::parseRetryAfterHeader(const std::string& value) {
    std::string h = value;
    // Trim ASCII whitespace.
    std::size_t b = 0, e = h.size();
    while (b < e && (h[b] == ' ' || h[b] == '\t')) ++b;
    while (e > b && (h[e - 1] == ' ' || h[e - 1] == '\t' || h[e - 1] == '\r'))
        --e;
    h = h.substr(b, e - b);
    if (h.empty() || h.size() > 9) return -1;
    for (char c : h) {
        if (c < '0' || c > '9') return -1;
    }
    try {
        return std::stoi(h);
    } catch (...) {
        return -1;
    }
}

bool GeminiProvider::isQuotaExhaustedBody(const std::string& body) {
    // Google reports both a spent quota and a temporary rate limit with HTTP 429
    // and status RESOURCE_EXHAUSTED, so the status alone cannot tell them apart.
    // Only the exhaustion-specific wording below counts: a plain rate limit
    // ("Resource has been exhausted") must stay RateLimited, or the provider
    // would be cooled down and hidden from the app for a condition that would
    // have cleared on the next call.
    std::string b;
    b.reserve(body.size());
    for (char c : body) {
        b.push_back(c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c);
    }
    return b.find("exceeded your current quota") != std::string::npos ||
           b.find("insufficient_quota") != std::string::npos ||
           b.find("quota_exceeded") != std::string::npos ||
           b.find("check your plan and billing") != std::string::npos;
}

int GeminiProvider::parseRetryInfoSeconds(const std::string& body) {
    // google.rpc.RetryInfo carries `"retryDelay": "3.5s"`. Only a whole-second
    // or fractional-seconds value ending in `s` is honoured; anything else
    // (including an absent field) yields -1 = unknown.
    JsonValue parsed;
    std::string parseError;
    if (!JsonValue::parse(body, parsed, parseError)) return -1;
    const JsonValue& err = parsed["error"];
    if (!err.isObject()) return -1;
    const JsonValue& details = err["details"];
    if (!details.isArray()) return -1;
    for (const auto& detail : details.items()) {
        const JsonValue& delay = detail["retryDelay"];
        if (!delay.isString()) continue;
        const std::string raw = delay.asString();
        if (raw.empty() || raw.back() != 's') continue;
        const std::string num = raw.substr(0, raw.size() - 1);
        try {
            const double seconds = std::stod(num);
            if (seconds < 0) return -1;
            return static_cast<int>(seconds);
        } catch (...) {
            continue;
        }
    }
    return -1;
}

LlmResult GeminiProvider::complete(const LlmRequest& request) {
    LlmResult result;
    result.provider = config_.providerLabel;
    result.model = config_.model;

    if (config_.apiKey.empty()) {
        result.outcome = LlmOutcome::AuthFailure;
        result.error = "GEMINI_API_KEY is not configured";
        return result;
    }

    OutboundHttpRequest http;
    http.url = buildUrl();
    http.method = "POST";
    http.body = buildRequestBody(request);
    http.connectTimeoutMillis = config_.connectTimeoutMillis;
    http.readTimeoutMillis = request.timeoutMillis > 0
                                 ? request.timeoutMillis
                                 : config_.readTimeoutMillis;
    http.headers.push_back("Content-Type: application/json");
    http.headers.push_back("Accept: application/json");
    // Gemini authenticates with a header, not a bearer token.
    http.headers.push_back("X-goog-api-key: " + config_.apiKey);

    const OutboundHttpResponse response = http_->send(http);
    result.httpStatus = response.statusCode;
    result.outcome = classify(response.ok, response.statusCode);

    if (!response.ok) {
        result.error = response.error;
        return result;
    }

    if (response.statusCode == 429) {
        // Google reports both a temporary rate limit and a spent quota as 429.
        // Refine the outcome and record any reset the provider actually gave
        // (RetryInfo in the body, or a plain Retry-After header). Absent either,
        // the reset is reported as unknown rather than guessed.
        if (isQuotaExhaustedBody(response.body)) {
            result.outcome = LlmOutcome::QuotaExhausted;
        }
        result.retryAfterSeconds = parseRetryAfterHeader(
            response.header("retry-after"));
        if (result.retryAfterSeconds < 0) {
            result.retryAfterSeconds = parseRetryInfoSeconds(response.body);
        }
    }

    if (result.outcome != LlmOutcome::Ok) {
        result.error = "provider returned HTTP " +
                       std::to_string(response.statusCode);
        if (!response.body.empty()) {
            result.error += ": " + response.body.substr(0, 200);
        }
        return result;
    }

    std::string text;
    std::string error;
    if (!extractText(response.body, text, error)) {
        result.outcome = LlmOutcome::BadRequest;
        result.error = error;
        return result;
    }
    result.content = std::move(text);
    return result;
}

}  // namespace aura
