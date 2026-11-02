// AYZR-ECO-001 — Standard Engine Contract (implementation).

#include "ayzr/ecosystem/EngineContract.h"

#include <cstdio>

namespace aura {
namespace ayzr {
namespace eco {

const char* toString(EventStatus s) noexcept {
    switch (s) {
        case EventStatus::DETECTED:             return "DETECTED";
        case EventStatus::PENDING_CONFIRMATION: return "PENDING_CONFIRMATION";
        case EventStatus::PUBLISHED:            return "PUBLISHED";
        case EventStatus::ACTIVE:               return "ACTIVE";
        case EventStatus::TARGET_HIT:           return "TARGET_HIT";
        case EventStatus::WARNING:              return "WARNING";
        case EventStatus::CLOSE_RECOMMENDED:    return "CLOSE_RECOMMENDED";
        case EventStatus::INVALIDATED:          return "INVALIDATED";
        case EventStatus::EXPIRED:              return "EXPIRED";
        case EventStatus::CLOSED:               return "CLOSED";
        case EventStatus::WITHHELD:             return "WITHHELD";
    }
    return "DETECTED";
}

bool parseEventStatus(const std::string& text, EventStatus& out) noexcept {
    static const EventStatus kAll[] = {
        EventStatus::DETECTED, EventStatus::PENDING_CONFIRMATION,
        EventStatus::PUBLISHED, EventStatus::ACTIVE, EventStatus::TARGET_HIT,
        EventStatus::WARNING, EventStatus::CLOSE_RECOMMENDED,
        EventStatus::INVALIDATED, EventStatus::EXPIRED, EventStatus::CLOSED,
        EventStatus::WITHHELD};
    for (EventStatus s : kAll) {
        if (text == toString(s)) { out = s; return true; }
    }
    return false;
}

const char* eventStatusLabelAr(EventStatus s) noexcept {
    switch (s) {
        case EventStatus::DETECTED:             return "مكتشفة";
        case EventStatus::PENDING_CONFIRMATION: return "بانتظار التأكيد";
        case EventStatus::PUBLISHED:            return "منشورة";
        case EventStatus::ACTIVE:               return "نشطة";
        case EventStatus::TARGET_HIT:           return "تحقق هدف";
        case EventStatus::WARNING:              return "تحذير";
        case EventStatus::CLOSE_RECOMMENDED:    return "توصية إغلاق";
        case EventStatus::INVALIDATED:          return "ملغاة";
        case EventStatus::EXPIRED:              return "منتهية";
        case EventStatus::CLOSED:               return "مغلقة";
        case EventStatus::WITHHELD:             return "محجوبة";
    }
    return "مكتشفة";
}

const char* toString(EngineDirection d) noexcept {
    switch (d) {
        case EngineDirection::BULLISH:  return "BULLISH";
        case EngineDirection::BEARISH:  return "BEARISH";
        case EngineDirection::SIDEWAYS: return "SIDEWAYS";
        case EngineDirection::NO_TRADE: return "NO_TRADE";
    }
    return "NO_TRADE";
}

bool parseEngineDirection(const std::string& text, EngineDirection& out) noexcept {
    static const EngineDirection kAll[] = {
        EngineDirection::BULLISH, EngineDirection::BEARISH,
        EngineDirection::SIDEWAYS, EngineDirection::NO_TRADE};
    for (EngineDirection d : kAll) {
        if (text == toString(d)) { out = d; return true; }
    }
    return false;
}

const char* engineDirectionLabelAr(EngineDirection d) noexcept {
    switch (d) {
        case EngineDirection::BULLISH:  return "صاعد";
        case EngineDirection::BEARISH:  return "هابط";
        case EngineDirection::SIDEWAYS: return "عرضي";
        case EngineDirection::NO_TRADE: return "لا توجد صفقة";
    }
    return "لا توجد صفقة";
}

const char* toString(StrategyFamily f) noexcept {
    switch (f) {
        case StrategyFamily::TREND_FOLLOWING: return "TREND_FOLLOWING";
        case StrategyFamily::MEAN_REVERSION:  return "MEAN_REVERSION";
        case StrategyFamily::BREAKOUT:        return "BREAKOUT";
        case StrategyFamily::MICROSTRUCTURE:  return "MICROSTRUCTURE";
        case StrategyFamily::EVENT_DRIVEN:    return "EVENT_DRIVEN";
        case StrategyFamily::HORIZON_SHORT:   return "HORIZON_SHORT";
        case StrategyFamily::HORIZON_MEDIUM:  return "HORIZON_MEDIUM";
        case StrategyFamily::HORIZON_LONG:    return "HORIZON_LONG";
        case StrategyFamily::ASTRA_CORE:      return "ASTRA_CORE";
        case StrategyFamily::UNKNOWN:         return "UNKNOWN";
    }
    return "UNKNOWN";
}

const char* strategyFamilyLabelAr(StrategyFamily f) noexcept {
    switch (f) {
        case StrategyFamily::TREND_FOLLOWING: return "تتبع الاتجاه";
        case StrategyFamily::MEAN_REVERSION:  return "العودة للمتوسط";
        case StrategyFamily::BREAKOUT:        return "الاختراق";
        case StrategyFamily::MICROSTRUCTURE:  return "بنية السوق";
        case StrategyFamily::EVENT_DRIVEN:    return "مدفوع بالأحداث";
        case StrategyFamily::HORIZON_SHORT:   return "قصير المدى";
        case StrategyFamily::HORIZON_MEDIUM:  return "متوسط المدى";
        case StrategyFamily::HORIZON_LONG:    return "بعيد المدى";
        case StrategyFamily::ASTRA_CORE:      return "المحرك الأساسي";
        case StrategyFamily::UNKNOWN:         return "غير معروف";
    }
    return "غير معروف";
}

const char* toString(EngineHealth h) noexcept {
    switch (h) {
        case EngineHealth::HEALTHY:          return "HEALTHY";
        case EngineHealth::DEGRADED:         return "DEGRADED";
        case EngineHealth::UNAVAILABLE:      return "UNAVAILABLE";
        case EngineHealth::DISABLED:         return "DISABLED";
        case EngineHealth::WAITING_FOR_DATA: return "WAITING_FOR_DATA";
        case EngineHealth::STALE_OUTPUT:     return "STALE_OUTPUT";
    }
    return "UNAVAILABLE";
}

const char* engineHealthLabelAr(EngineHealth h) noexcept {
    switch (h) {
        case EngineHealth::HEALTHY:          return "سليم";
        case EngineHealth::DEGRADED:         return "متدهور";
        case EngineHealth::UNAVAILABLE:      return "غير متاح";
        case EngineHealth::DISABLED:         return "معطّل";
        case EngineHealth::WAITING_FOR_DATA: return "بانتظار البيانات";
        case EngineHealth::STALE_OUTPUT:     return "مخرجات قديمة";
    }
    return "غير متاح";
}

bool isPublishable(const EngineResult& r, std::int64_t nowSec) noexcept {
    if (r.direction == EngineDirection::NO_TRADE) return false;
    if (r.status == EventStatus::WITHHELD || r.status == EventStatus::EXPIRED ||
        r.status == EventStatus::INVALIDATED) {
        return false;
    }
    // Decision-grade data only: VALID is the sole qualifying state.
    if (r.dataQuality != DataQualityState::VALID) return false;
    if (r.expiresAt.has_value() && nowSec > *r.expiresAt) return false;
    return true;
}

std::string makeEventId(const std::string& engineId, const std::string& symbol,
                        Timeframe tf, std::int64_t dataTimestamp) noexcept {
    // FNV-1a over the identity tuple. Deterministic and dependency-free; the id
    // is a stable handle, not a cryptographic digest.
    std::uint64_t h = 1469598103934665603ULL;
    auto mix = [&h](const std::string& s) {
        for (unsigned char c : s) {
            h ^= static_cast<std::uint64_t>(c);
            h *= 1099511628211ULL;
        }
    };
    mix(engineId);
    mix("|");
    mix(symbol);
    mix("|");
    mix(toString(tf));
    mix("|");
    mix(std::to_string(dataTimestamp));
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%016llx",
                  static_cast<unsigned long long>(h));
    return std::string("ev-") + buf;
}

}  // namespace eco
}  // namespace ayzr
}  // namespace aura
