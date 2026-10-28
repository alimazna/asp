// Economic-calendar feed implementation.

#include "macro/CalendarFeed.h"

#include "foundation/Json.h"

#include <cctype>
#include <cstdio>
#include <ctime>

namespace aura {
namespace {

// Days since 1970-01-01 for a civil date (Howard Hinnant's algorithm). Kept
// local so the conversion does not depend on mktime's timezone handling.
std::int64_t daysFromCivil(int y, int m, int d) {
    y -= m <= 2;
    const std::int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<std::int64_t>(doe) - 719468;
}

// Parses "YYYY-MM-DDTHH:MM:SS" plus an optional "+HH:MM"/"-HH:MM"/"Z" suffix
// and returns UTC epoch seconds. Returns false when the shape is not recognized
// so the caller can drop the event rather than mis-time it.
bool parseIsoWithOffset(const std::string& raw, std::int64_t& outUtc) {
    int y = 0, mo = 0, d = 0, h = 0, mi = 0, s = 0;
    char sep = 0;
    // The seconds part is optional in some feeds.
    int n = std::sscanf(raw.c_str(), "%d-%d-%d%c%d:%d:%d", &y, &mo, &d, &sep, &h,
                        &mi, &s);
    if (n < 6) return false;
    if (sep != 'T' && sep != ' ') return false;
    if (n < 7) s = 0;
    if (mo < 1 || mo > 12 || d < 1 || d > 31) return false;
    if (h < 0 || h > 23 || mi < 0 || mi > 59 || s < 0 || s > 60) return false;

    std::int64_t epoch = daysFromCivil(y, mo, d) * 86400 +
                         static_cast<std::int64_t>(h) * 3600 +
                         static_cast<std::int64_t>(mi) * 60 + s;

    // Offset suffix. The scan above consumed exactly the date+time, so the rest
    // starts at the first character after the seconds (or minutes) field.
    std::size_t pos = raw.find_first_of("+-Z", n >= 7 ? 19 : 16);
    if (pos == std::string::npos) {
        // No offset published: the feed contract is UTC, so treat it as UTC.
        outUtc = epoch;
        return true;
    }
    if (raw[pos] == 'Z') {
        outUtc = epoch;
        return true;
    }
    int oh = 0, om = 0;
    if (std::sscanf(raw.c_str() + pos + 1, "%d:%d", &oh, &om) != 2) {
        // A bare "+HH" is valid ISO 8601.
        if (std::sscanf(raw.c_str() + pos + 1, "%d", &oh) != 1) return false;
        om = 0;
    }
    const std::int64_t offset = static_cast<std::int64_t>(oh) * 3600 + om * 60;
    outUtc = (raw[pos] == '-') ? epoch + offset : epoch - offset;
    return true;
}

std::string lowerCopy(const std::string& in) {
    std::string out = in;
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

}  // namespace

bool isGoldRelevantCurrency(const std::string& country) {
    const std::string c = lowerCopy(country);
    // USD prices gold directly; EUR/GBP/JPY/CNY/CHF move it through the dollar
    // and safe-haven channels. This is a routing list, not a claim about size.
    return c == "usd" || c == "eur" || c == "gbp" || c == "jpy" || c == "cny" ||
           c == "chf" || c == "xau";
}

std::string normalizeImpact(const std::string& raw) {
    const std::string r = lowerCopy(raw);
    if (r == "high") return "high";
    if (r == "medium") return "medium";
    if (r == "low") return "low";
    if (r == "holiday") return "holiday";
    return "unknown";
}

std::string makeEventKey(const std::string& source, const std::string& country,
                         const std::string& title,
                         std::int64_t scheduledAtUtc) {
    return source + "|" + lowerCopy(country) + "|" + title + "|" +
           std::to_string(scheduledAtUtc);
}

bool parseCalendarJson(const std::string& body, const std::string& sourceLabel,
                       std::vector<CalendarEvent>& events, std::string& error) {
    JsonValue root;
    std::string parseError;
    if (!JsonValue::parse(body, root, parseError)) {
        error = "calendar json: " + parseError;
        return false;
    }
    if (!root.isArray()) {
        error = "calendar json: root is not an array";
        return false;
    }
    for (const auto& item : root.items()) {
        if (!item.isObject()) continue;
        CalendarEvent e;
        e.title = item["title"].asString();
        e.country = item["country"].asString();
        if (e.title.empty() || e.country.empty()) continue;
        std::int64_t when = 0;
        if (!parseIsoWithOffset(item["date"].asString(), when)) continue;
        e.scheduledAtUtc = when;
        e.impact = normalizeImpact(item["impact"].asString());
        e.forecast = item["forecast"].asString();
        e.previous = item["previous"].asString();
        e.source = sourceLabel;
        e.eventKey = makeEventKey(sourceLabel, e.country, e.title, when);
        events.push_back(std::move(e));
    }
    return true;
}

FairEconomyCalendarSource::FairEconomyCalendarSource(
    std::string url, std::shared_ptr<IOutboundHttpClient> http)
    : url_(std::move(url)), http_(std::move(http)) {}

bool FairEconomyCalendarSource::fetch(std::vector<CalendarEvent>& events,
                                      std::string& error) {
    if (http_ == nullptr) {
        error = "no outbound http client";
        return false;
    }
    OutboundHttpRequest request;
    request.url = url_;
    request.method = "GET";
    // The feed 429s an anonymous client, so send a plain UA. No credential is
    // involved and none is logged.
    request.headers.push_back("User-Agent: AYZR/1.0 (personal market reader)");
    request.headers.push_back("Accept: application/json");
    request.connectTimeoutMillis = 5000;
    request.readTimeoutMillis = 20000;

    const OutboundHttpResponse response = http_->send(request);
    if (!response.ok) {
        error = "calendar transport: " + response.error;
        return false;
    }
    if (response.statusCode != 200) {
        error = "calendar http " + std::to_string(response.statusCode);
        return false;
    }
    return parseCalendarJson(response.body, sourceLabel(), events, error);
}

}  // namespace aura
