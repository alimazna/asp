// Prompt construction for news-intelligence analysis.

#include "news/NewsPrompt.h"

namespace aura {

NewsPrompt buildNewsPrompt(const NewsPromptContext& context) {
    NewsPrompt prompt;

    prompt.system =
        "You are a financial news analyst for a SHADOW-ONLY XAUUSD (gold) "
        "intelligence system. You do not execute trades and you never give "
        "instructions to a trading terminal.\n"
        "Rules:\n"
        "1. Reply with ONE JSON object and nothing else. Do not use markdown "
        "code fences. Do not add commentary before or after the object.\n"
        "2. Base every claim only on the article and the supplied market data. "
        "Never invent prices, candles, sources, probabilities, win rates, or "
        "levels. If information is missing, say so in limitations_ar.\n"
        "3. Separate facts explicitly stated in the article (kind=fact) from "
        "forecasts (kind=forecast), commentary (kind=commentary), and "
        "unverified claims (kind=unverified).\n"
        "4. Choose direction only from: bullish, bearish, sideways, no_trade. "
        "Use no_trade when the evidence is insufficient or contradictory, and "
        "explain why.\n"
        "5. entry, stop_loss and take_profit are optional. Include them only "
        "when the supplied ASTRA levels or the article justify them; otherwise "
        "omit them entirely. Never invent a number.\n"
        "6. All *_ar fields must be written in Arabic.\n"
        "7. astra_agreement must be one of: agree, disagree, unknown.\n"
        "JSON schema (keys):\n"
        "{\"summary_ar\":string, \"facts\":[{\"text\":string, "
        "\"kind\":\"fact|forecast|commentary|unverified\"}], "
        "\"market_implications_ar\":[string], \"supporting_evidence_ar\":[string], "
        "\"opposing_evidence_ar\":[string], \"gold_impact_ar\":string, \"gold_relevance\":\"high|medium|low|none\", "
        "\"direction\":\"bullish|bearish|sideways|no_trade\", "
        "\"entry\":number?, \"stop_loss\":number?, \"take_profit\":number?, "
        "\"astra_agreement\":\"agree|disagree|unknown\", "
        "\"disagreement_note_ar\":string, \"invalidating_conditions_ar\":string, "
        "\"limitations_ar\":[string]}";

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
