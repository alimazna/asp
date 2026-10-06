// TEL-0002 - Telegram gateway implementation.

#include "telegram/TelegramGateway.h"

#include <sstream>

namespace aura {

EntityId TelegramGateway::enqueue(const std::string& chatId,
                                  const std::string& text, Timestamp now) {
    if (queue_.size() >= config_.maxQueue) return EntityId();
    TelegramMessage message;
    message.messageId = EntityId("tg-" + std::to_string(++sequence_));
    message.chatId = chatId.empty() ? config_.defaultChatId : chatId;
    message.text = text;
    message.state = TelegramDeliveryState::QUEUED;
    message.queuedAt = now.isUnknown() ? Timestamp::now() : now;
    queue_.push_back(message);
    return message.messageId;
}

std::string TelegramGateway::formatHealth(const BackendHealth& health) {
    std::ostringstream out;
    out << "[ASTRA] Backend health: " << toString(health.aggregate);
    out << " | decision-grade data: "
        << (health.decisionGradeData ? "yes" : "no");
    if (!health.degradedReasons.empty()) {
        out << "\nReasons:";
        for (const auto& reason : health.degradedReasons) out << "\n- " << reason;
    }
    out << "\nNote: shadow mode. Not a profitability or live-trading signal.";
    return out.str();
}

std::string TelegramGateway::formatIncident(const Incident& incident) {
    std::ostringstream out;
    out << "[ASTRA] Incident " << toString(incident.severity) << " "
        << toString(incident.state) << ": " << incident.title;
    if (!incident.summary.empty()) out << "\n" << incident.summary;
    return out.str();
}

EntityId TelegramGateway::notifyHealth(const BackendHealth& health,
                                       Timestamp now) {
    if (health.aggregate == ServiceState::ONLINE) return EntityId();
    return enqueue(config_.defaultChatId, formatHealth(health), now);
}

EntityId TelegramGateway::notifyIncident(const Incident& incident,
                                         Timestamp now) {
    if (!incident.valid()) return EntityId();
    return enqueue(config_.defaultChatId, formatIncident(incident), now);
}

std::size_t TelegramGateway::flush(Timestamp now) {
    if (transport_ == nullptr) return 0;   // no transport: stay queued

    const Timestamp when = now.isUnknown() ? Timestamp::now() : now;
    std::size_t sent = 0;
    std::deque<TelegramMessage> remaining;
    while (!queue_.empty()) {
        TelegramMessage message = queue_.front();
        queue_.pop_front();
        std::string error;
        if (transport_->send(message.chatId, message.text, error)) {
            message.state = TelegramDeliveryState::SENT;
            message.sentAt = when;
            history_.push_back(message);
            ++sent;
        } else {
            message.state = TelegramDeliveryState::FAILED;
            message.error = error;
            history_.push_back(message);
        }
    }
    queue_ = remaining;
    return sent;
}

std::vector<TelegramMessage> TelegramGateway::messages() const {
    std::vector<TelegramMessage> result(history_.begin(), history_.end());
    result.insert(result.end(), queue_.begin(), queue_.end());
    return result;
}

std::size_t TelegramGateway::queuedCount() const { return queue_.size(); }

}  // namespace aura
