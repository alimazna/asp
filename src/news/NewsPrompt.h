#pragma once
// Prompt construction for the news-intelligence analysis.
//
// The prompt is built from real data only: the article, the available closed
// candles, and the existing ASTRA analysis. It explicitly instructs the model
// to emit a single JSON object, to separate facts from inference, and to return
// no_trade when the evidence is insufficient. It also states that the model
// must not invent prices or probabilities.

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
