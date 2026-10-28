#pragma once
// Prompt construction for the news-intelligence analysis.
//
// The prompt is built from real data only: the article, the available closed
// candles, and the existing ASTRA analysis. It explicitly instructs the model
// to emit a single JSON object, to separate facts from inference, and to return
// no_trade when the evidence is insufficient. It also states that the model
// must not invent prices or probabilities.

#include <cstdint>
#include <string>
#include <vector>

namespace aura {

struct NewsPromptContext {
    std::string headline;
    std::string source;
    std::string bodyText;      // may be empty when the article body is unavailable
    bool bodyAvailable = false;
    std::string publishedAtIso;
    // Compact, real candle context (already validated, closed bars only).
    std::string candleSummary;
    // Existing ASTRA analysis, verbatim from the backend (may be "unavailable").
    std::string astraSummary;
    std::string timeframeContext;
};

struct NewsPrompt {
    std::string system;
    std::string user;
};

NewsPrompt buildNewsPrompt(const NewsPromptContext& context);

// Which of the two macro-event turns is being requested.
enum class EventTurn {
    Expectation,  // before release: what the market expects, and what would
                  // confirm or contradict it
    Result,       // after release: read the outcome against the expectation
};

// Context for a scheduled macro event. Every field is sourced: `forecast` and
// `previous` come from the calendar feed and are empty when the feed published
// none (never zero-filled).
struct EventPromptContext {
    std::string title;
    std::string country;
    std::string impact;
    std::int64_t scheduledAtUtc = 0;
    std::string scheduledAtIso;
    std::string forecast;
    std::string previous;
    // Compact, real candle context (validated, closed bars only).
    std::string candleSummary;
    // Existing ASTRA analysis, verbatim from the backend.
    std::string astraSummary;
    std::string timeframeContext;
};

NewsPrompt buildEventPrompt(EventTurn turn, const EventPromptContext& context);

// Build the corrective user instruction for a bounded repair turn. It is sent
// as a follow-up turn after the model's own invalid reply (an assistant turn),
// which is what makes the model correct itself: an inline restatement in a
// single turn was verified NOT to work with nemotron-3-nano:30b, while the
// assistant+user turn sequence recovers a valid object. `validationError` is
// the parser's safe reason. The instruction restates the exact required keys;
// it does not relax the schema, so a model that cannot comply still yields
// invalid_model_output.
std::string buildRepairUserMessage(const std::string& validationError);

}  // namespace aura
