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

LlmResult Llm7Provider::complete(const LlmRequest& request) {
    LlmResult result;

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
