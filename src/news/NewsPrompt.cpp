// Prompt construction for news-intelligence analysis.

#include "news/NewsPrompt.h"

namespace aura {

namespace {

// The rules every analysis turn shares, regardless of what it is analysing.
// Kept in one place so an article turn and an event turn cannot drift apart on
// the safety contract: shadow-only, no invented numbers, Arabic output, and
// no_trade when the evidence is insufficient.
std::string sharedSystemRules() {
    return
        "You are a financial analyst for a SHADOW-ONLY XAUUSD (gold) "
        "intelligence system. You do not execute trades and you never give "
        "instructions to a trading terminal.\n"
        "Rules:\n"
        "1. Reply with ONE JSON object and nothing else. Do not use markdown "
        "code fences. Do not add commentary before or after the object.\n"
        "2. Base every claim only on the supplied data. "
        "Never invent prices, candles, sources, probabilities, win rates, or "
        "levels. If information is missing, say so in limitations_ar.\n"
        "3. Separate facts explicitly stated in the source (kind=fact) from "
        "forecasts (kind=forecast), commentary (kind=commentary), and "
        "unverified claims (kind=unverified).\n"
        "4. Choose direction only from: bullish, bearish, sideways, no_trade. "
        "Use no_trade when the evidence is insufficient or contradictory, and "
        "explain why.\n"
        "5. entry, stop_loss and take_profit are optional. Include them only "
        "when the supplied ASTRA levels justify them; otherwise "
        "omit them entirely. Never invent a number.\n"
        "6. All *_ar fields must be written in Arabic.\n"
        "7. astra_agreement must be one of: agree, disagree, unknown.\n";
}

// Keys shared by every turn's JSON object.
constexpr char kSharedSchemaKeys[] =
    "{\"summary_ar\":string, \"facts\":[{\"text\":string, "
    "\"kind\":\"fact|forecast|commentary|unverified\"}], "
    "\"market_implications_ar\":[string], \"supporting_evidence_ar\":[string], "
    "\"opposing_evidence_ar\":[string], \"gold_impact_ar\":string, "
    "\"gold_relevance\":\"high|medium|low|none\", "
    "\"direction\":\"bullish|bearish|sideways|no_trade\", "
    "\"entry\":number?, \"stop_loss\":number?, \"take_profit\":number?, "
    "\"astra_agreement\":\"agree|disagree|unknown\", "
    "\"disagreement_note_ar\":string, \"invalidating_conditions_ar\":string, "
    "\"limitations_ar\":[string]}";

}  // namespace

NewsPrompt buildNewsPrompt(const NewsPromptContext& context) {
    NewsPrompt prompt;

    prompt.system = sharedSystemRules();
    prompt.system += "JSON schema (keys):\n";
    prompt.system += kSharedSchemaKeys;

    std::string user;
    user += "ARTICLE\n";
    user += "Headline: " + context.headline + "\n";
    user += "Source: " + context.source + "\n";
    user += "Published (UTC): " + context.publishedAtIso + "\n";
    if (context.bodyAvailable && !context.bodyText.empty()) {
        user += "Body:\n" + context.bodyText + "\n";
    } else {
        user += "Body: UNAVAILABLE. Analyse only the headline and metadata, and "
                "state this limitation explicitly.\n";
    }
    user += "\nMARKET DATA (closed candles only)\n";
    user += context.candleSummary.empty() ? "UNAVAILABLE\n"
                                          : context.candleSummary + "\n";
    user += "Timeframes: " +
            (context.timeframeContext.empty() ? std::string("UNAVAILABLE")
                                              : context.timeframeContext) +
            "\n";
    user += "\nEXISTING ASTRA ANALYSIS (read-only, do not modify)\n";
    user += context.astraSummary.empty() ? "UNAVAILABLE\n"
                                         : context.astraSummary + "\n";
    user += "\nProduce the JSON object now.";

    prompt.user = std::move(user);
    return prompt;
}

NewsPrompt buildEventPrompt(EventTurn turn, const EventPromptContext& context) {
    NewsPrompt prompt;
    prompt.system = sharedSystemRules();
    prompt.system += "JSON schema (keys):\n";
    prompt.system += kSharedSchemaKeys;

    std::string user;
    user += "SCHEDULED MACRO EVENT\n";
    user += "Title: " + context.title + "\n";
    user += "Currency/region: " + context.country + "\n";
    user += "Impact (feed rating): " + context.impact + "\n";
    user += "Scheduled (UTC): " + context.scheduledAtIso + "\n";
    // A missing consensus is stated as missing. Substituting 0 would be a
    // fabricated expectation the model would then reason about as real.
    user += "Consensus (published forecast): " +
            (context.forecast.empty() ? std::string("UNAVAILABLE")
                                      : context.forecast) +
            "\n";
    user += "Previous reading: " +
            (context.previous.empty() ? std::string("UNAVAILABLE")
                                      : context.previous) +
            "\n";

    user += "\nMARKET DATA (closed candles only)\n";
    user += context.candleSummary.empty() ? "UNAVAILABLE\n"
                                          : context.candleSummary + "\n";
    user += "Timeframes: " +
            (context.timeframeContext.empty() ? std::string("UNAVAILABLE")
                                              : context.timeframeContext) +
            "\n";
    user += "\nEXISTING ASTRA ANALYSIS (read-only, do not modify)\n";
    user += context.astraSummary.empty() ? "UNAVAILABLE\n"
                                         : context.astraSummary + "\n";

    if (turn == EventTurn::Expectation) {
        user +=
            "\nTASK: the event has NOT been released yet. Write the EXPECTATION "
            "turn. Using only the consensus, the previous reading and the market "
            "data above:\n"
            "- state what the market is positioned for, in summary_ar;\n"
            "- list the transmission channels to gold in market_implications_ar;\n"
            "- state explicitly what would CONFIRM the expectation and what "
            "would CONTRADICT it, in supporting_evidence_ar and "
            "opposing_evidence_ar respectively;\n"
            "- put the conditions that would invalidate the scenario in "
            "invalidating_conditions_ar;\n"
            "- direction is a provisional lean only, and must be no_trade when "
            "the consensus is unavailable or the market data is insufficient;\n"
            "- do NOT present a released outcome: it does not exist yet. If the "
            "consensus is unavailable, say so in limitations_ar.\n";
    } else {
        user +=
            "\nTASK: the scheduled time has PASSED. Write the RESULT turn. "
            "Using only the data above:\n"
            "- state what actually happened, in summary_ar;\n"
            "- if the released figure is not present in the data above, you MUST "
            "say so explicitly in limitations_ar and set direction to no_trade: "
            "do not guess the outcome from price action alone;\n"
            "- where a released figure IS present, compare it with the consensus "
            "and previous reading and describe the surprise;\n"
            "- put the conditions that would invalidate the scenario in "
            "invalidating_conditions_ar.\n";
    }
    user += "\nProduce the JSON object now.";

    prompt.user = std::move(user);
    return prompt;
}

std::string buildRepairUserMessage(const std::string& validationError) {
    // A concrete example is the highest-signal correction for a small model that
    // has no JSON mode: it shows the exact nesting, which is precisely what the
    // model gets wrong (it tends to fold top-level keys into a facts element).
    std::string user;
    user += "Your previous reply failed schema validation: " + validationError +
            "\n"
            "Reply with ONE JSON object and nothing else. No markdown fences, no "
            "prose before or after it. It MUST have these 11 keys at the TOP "
            "level, spelled exactly like this example (replace the values with "
            "your real analysis; never copy the sample numbers):\n"
            "{\"summary_ar\":\"ملخص عربي\","
            "\"facts\":[{\"text\":\"نص الحقيقة\",\"kind\":\"fact\"}],"
            "\"market_implications_ar\":[\"أثر محتمل\"],"
            "\"supporting_evidence_ar\":[\"دليل مؤيد\"],"
            "\"opposing_evidence_ar\":[\"دليل معارض\"],"
            "\"gold_impact_ar\":\"التأثير على الذهب\","
            "\"direction\":\"no_trade\","
            "\"astra_agreement\":\"unknown\","
            "\"disagreement_note_ar\":\"ملاحظة الاختلاف\","
            "\"invalidating_conditions_ar\":\"شرط إبطال السيناريو\","
            "\"limitations_ar\":[\"قيد\"]}\n"
            "Rules: each facts element MUST contain both \"text\" and \"kind\", "
            "where kind is one of fact, forecast, commentary, unverified. "
            "direction MUST be exactly one of bullish, bearish, sideways, "
            "no_trade. astra_agreement MUST be exactly one of agree, disagree, "
            "unknown. Do NOT place any of these keys inside a facts element. "
            "All *_ar fields must be Arabic. Use no_trade when the evidence is "
            "insufficient; never invent prices or probabilities.\n\n"
            "Produce the corrected JSON object now.";
    return user;
}

}  // namespace aura
