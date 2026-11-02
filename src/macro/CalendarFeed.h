#pragma once
// Economic-calendar feed: the source of scheduled events and their consensus.
//
// Why this exists: the system must be able to say what the market EXPECTS from
// an event before it is released, then read the outcome when it lands. A
// consensus figure is a published fact about other people's expectations, so it
// is fetched from a real calendar rather than guessed by the model. When the
// feed is unreachable the event is still known (title/time/impact) but the
// consensus is absent -- never filled in.
//
// The transport is pluggable so tests inject a deterministic fake and never
// touch the network.

#include "foundation/OutboundHttpClient.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace aura {

// One scheduled macro event as published by a calendar source.
struct CalendarEvent {
    std::string eventKey;      // stable identity: source|country|title|time
    std::string title;
    std::string country;       // ISO-ish currency code the event belongs to
    std::string impact;        // high | medium | low | holiday | unknown
    std::int64_t scheduledAtUtc = 0;
    // Published consensus, verbatim as the feed prints it (e.g. "90.0", "3.7%").
    // Empty means "the feed did not publish one" -- not zero.
    std::string forecast;
    std::string previous;
    std::string source;        // feed label, for attribution
};

// True when the event's currency plausibly moves gold. This is a routing
// decision only; it makes no claim about direction or magnitude.
bool isGoldRelevantCurrency(const std::string& country);

// Normalizes a feed impact string into the closed vocabulary above.
std::string normalizeImpact(const std::string& raw);

class ICalendarSource {
public:
    virtual ~ICalendarSource() = default;
    virtual std::string sourceLabel() const = 0;
    // Returns true on a successful fetch. `events` may legitimately be empty.
    virtual bool fetch(std::vector<CalendarEvent>& events,
                       std::string& error) = 0;
};

// Parses the forex factory style weekly JSON:
//   [{"title":..,"country":..,"date":"2026-10-26T05:00:00-04:00",
//     "impact":"High","forecast":"90.0","previous":"89.9"}, ...]
//
// `date` carries an explicit UTC offset, so the parser converts it to UTC
// instead of assuming the host timezone. An unparseable date drops that single
// event rather than shifting it to a wrong time.
bool parseCalendarJson(const std::string& body, const std::string& sourceLabel,
                       std::vector<CalendarEvent>& events, std::string& error);

// Builds the stable event key. Exposed for tests.
std::string makeEventKey(const std::string& source, const std::string& country,
                         const std::string& title, std::int64_t scheduledAtUtc);

// FairEconomy weekly calendar over the shared outbound HTTP client.
class FairEconomyCalendarSource : public ICalendarSource {
public:
    FairEconomyCalendarSource(std::string url,
                              std::shared_ptr<IOutboundHttpClient> http);

    std::string sourceLabel() const override { return "faireconomy"; }

    bool fetch(std::vector<CalendarEvent>& events, std::string& error) override;

private:
    std::string url_;
    std::shared_ptr<IOutboundHttpClient> http_;
};

}  // namespace aura
