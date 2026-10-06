#pragma once
// TEL-0001 - Auxiliary Telegram backend gateway.
//
// An outbound notification gateway. It formats health and incident state into
// operator messages and queues them for delivery. Delivery itself is behind a
// transport interface; when no transport is configured, messages remain
// QUEUED and are never reported as sent. This gateway is auxiliary: it can
// never carry execution authority.

#include "foundation/EntityId.h"
#include "foundation/Timestamp.h"
#include "governance/IncidentManager.h"
#include "health/HealthMonitor.h"

#include <deque>
#include <string>
#include <vector>

namespace aura {

enum class TelegramDeliveryState {
    QUEUED,
    SENT,
    FAILED,
    SUPPRESSED,   // deduplicated or throttled
};

inline const char* toString(TelegramDeliveryState s) noexcept {
    switch (s) {
        case TelegramDeliveryState::QUEUED:     return "QUEUED";
        case TelegramDeliveryState::SENT:       return "SENT";
        case TelegramDeliveryState::FAILED:     return "FAILED";
        case TelegramDeliveryState::SUPPRESSED: return "SUPPRESSED";
    }
    return "QUEUED";
}

struct TelegramMessage {
    EntityId messageId;
    std::string chatId;
    std::string text;
    TelegramDeliveryState state = TelegramDeliveryState::QUEUED;
    Timestamp queuedAt;
    Timestamp sentAt;
    std::string error;
};

// Transport is implemented by a real sender (not provided in the backend-only
// build). Returning false leaves the message queued/failed; it is never
// silently marked sent.
class ITelegramTransport {
public:
    virtual ~ITelegramTransport() = default;
    virtual bool send(const std::string& chatId, const std::string& text,
                      std::string& outError) = 0;
};

struct TelegramConfig {
    std::string defaultChatId;
    std::size_t maxQueue = 500;
    std::int64_t minSecondsBetweenDuplicates = 60;
};

class TelegramGateway {
public:
    explicit TelegramGateway(TelegramConfig config = {})
        : config_(std::move(config)) {}

    void setTransport(ITelegramTransport* transport) { transport_ = transport; }

    // Queue an arbitrary operator message. Returns the message id (empty on
    // refusal, e.g. queue full).
    EntityId enqueue(const std::string& chatId, const std::string& text,
                     Timestamp now);

    // Format and queue a health notification. Returns an empty id when health
    // is ONLINE (nothing worth notifying).
    EntityId notifyHealth(const BackendHealth& health, Timestamp now);

    // Format and queue an incident notification.
    EntityId notifyIncident(const Incident& incident, Timestamp now);

    // Attempt delivery of queued messages. Without a transport this is a no-op
    // and messages stay QUEUED.
    std::size_t flush(Timestamp now);

    std::vector<TelegramMessage> messages() const;
    std::size_t queuedCount() const;

private:
    static std::string formatHealth(const BackendHealth& health);
    static std::string formatIncident(const Incident& incident);

    TelegramConfig config_;
    ITelegramTransport* transport_ = nullptr;
    std::deque<TelegramMessage> queue_;
    std::vector<TelegramMessage> history_;
    std::uint64_t sequence_ = 0;
};

}  // namespace aura
