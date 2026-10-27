#pragma once
// Defensive parser/validator for the LLM's structured analysis output.
//
// `nemotron-3-nano:30b` does NOT support JSON mode, so structured output cannot
// be guaranteed by the provider. The contract is therefore:
//   1. the prompt asks for a single JSON object;
//   2. the parser locates the first balanced `{...}` in the text (tolerating
//      markdown fences or surrounding prose);
//   3. every field is validated against this schema;
//   4. any violation yields `invalid_model_output` — never a coerced value.
//
// The parser never invents a field the model omitted: a missing required field
// is a validation failure.

#include "news/NewsModels.h"

#include <string>

namespace aura {

struct ParsedAnalysis {
    bool valid = false;
    std::string error;      // why validation failed, safe to log
    NewsAnalysis analysis;  // populated only when valid
};

// Parse and validate `text`. `articleId`, `providerId`, `modelId` and
// `analyzedAtUtc` are stamped by the caller onto the returned analysis.
ParsedAnalysis parseAnalysisResponse(const std::string& text);

// Extract the first balanced JSON object from arbitrary text. Returns false when
// none is found. Exposed for tests.
bool extractFirstJsonObject(const std::string& text, std::string& out);

}  // namespace aura
