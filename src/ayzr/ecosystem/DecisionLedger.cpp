// AYZR-LED-001 — Central Decision Ledger (implementation).

#include "ayzr/ecosystem/DecisionLedger.h"

#include <algorithm>
#include <sqlite3.h>

#include <cstring>

namespace aura {
namespace ayzr {
namespace eco {
namespace {

constexpr const char* kSchema =
    "CREATE TABLE IF NOT EXISTS ayzr_decision_ledger ("
    "  seq INTEGER PRIMARY KEY AUTOINCREMENT,"
    "  at_utc INTEGER NOT NULL,"
    "  type TEXT NOT NULL,"
    "  correlation_id TEXT NOT NULL DEFAULT '',"
    "  opportunity_id TEXT NOT NULL DEFAULT '',"
    "  engine_id TEXT NOT NULL DEFAULT '',"
    "  reason_code TEXT NOT NULL DEFAULT '',"
    "  detail_ar TEXT NOT NULL DEFAULT '',"
    "  payload_json TEXT NOT NULL DEFAULT ''"
    ");"
    "CREATE INDEX IF NOT EXISTS idx_ledger_opp "
    "  ON ayzr_decision_ledger(opportunity_id);"
    "CREATE INDEX IF NOT EXISTS idx_ledger_corr "
    "  ON ayzr_decision_ledger(correlation_id);";

std::string colText(sqlite3_stmt* st, int i) {
    const unsigned char* p = sqlite3_column_text(st, i);
    return p ? reinterpret_cast<const char*>(p) : std::string();
}

}  // namespace

const char* toString(LedgerEventType t) noexcept {
    switch (t) {
        case LedgerEventType::ENGINE_EVENT:          return "ENGINE_EVENT";
        case LedgerEventType::OPPORTUNITY_CREATED:   return "OPPORTUNITY_CREATED";
        case LedgerEventType::OPPORTUNITY_UPDATED:   return "OPPORTUNITY_UPDATED";
        case LedgerEventType::OPPORTUNITY_MERGED:    return "OPPORTUNITY_MERGED";
        case LedgerEventType::CONFLICT_DETECTED:     return "CONFLICT_DETECTED";
        case LedgerEventType::DECISION_PUBLISHED:    return "DECISION_PUBLISHED";
        case LedgerEventType::DECISION_WITHHELD:     return "DECISION_WITHHELD";
        case LedgerEventType::DECISION_WATCH:        return "DECISION_WATCH";
        case LedgerEventType::RISK_ALERT:            return "RISK_ALERT";
        case LedgerEventType::ENGINE_FAILURE:        return "ENGINE_FAILURE";
        case LedgerEventType::DATA_QUALITY_FAILURE:  return "DATA_QUALITY_FAILURE";
        case LedgerEventType::OPPORTUNITY_EXPIRED:   return "OPPORTUNITY_EXPIRED";
        case LedgerEventType::SESSION_STATE:         return "SESSION_STATE";
    }
    return "ENGINE_EVENT";
}

const char* ledgerEventLabelAr(LedgerEventType t) noexcept {
    switch (t) {
        case LedgerEventType::ENGINE_EVENT:          return "حدث محرك";
        case LedgerEventType::OPPORTUNITY_CREATED:   return "إنشاء فرصة";
        case LedgerEventType::OPPORTUNITY_UPDATED:   return "تحديث فرصة";
        case LedgerEventType::OPPORTUNITY_MERGED:    return "دمج فرصة";
        case LedgerEventType::CONFLICT_DETECTED:     return "تعارض مكتشف";
        case LedgerEventType::DECISION_PUBLISHED:    return "قرار منشور";
        case LedgerEventType::DECISION_WITHHELD:     return "قرار محجوب";
        case LedgerEventType::DECISION_WATCH:        return "قرار مراقبة";
        case LedgerEventType::RISK_ALERT:            return "تنبيه مخاطر";
        case LedgerEventType::ENGINE_FAILURE:        return "عطل محرك";
        case LedgerEventType::DATA_QUALITY_FAILURE:  return "خلل جودة بيانات";
        case LedgerEventType::OPPORTUNITY_EXPIRED:   return "انتهاء فرصة";
        case LedgerEventType::SESSION_STATE:         return "حالة جلسة";
    }
    return "حدث محرك";
}

DecisionLedger::~DecisionLedger() {
    if (db_ != nullptr) sqlite3_close(db_);
}

bool DecisionLedger::open(const std::string& path) {
    std::lock_guard<std::mutex> lk(mu_);
    if (db_ != nullptr) { sqlite3_close(db_); db_ = nullptr; }
    if (sqlite3_open(path.c_str(), &db_) != SQLITE_OK) {
        if (db_ != nullptr) { sqlite3_close(db_); db_ = nullptr; }
        return false;
    }
    sqlite3_busy_timeout(db_, 5000);
    sqlite3_exec(db_, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
    char* err = nullptr;
    if (sqlite3_exec(db_, kSchema, nullptr, nullptr, &err) != SQLITE_OK) {
        if (err != nullptr) sqlite3_free(err);
        sqlite3_close(db_);
        db_ = nullptr;
        return false;
    }
    // Seed nextSeq_ past any existing rows so memory/DB sequences stay monotone.
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db_, "SELECT COALESCE(MAX(seq),0) FROM ayzr_decision_ledger;",
                           -1, &st, nullptr) == SQLITE_OK) {
        if (sqlite3_step(st) == SQLITE_ROW) nextSeq_ = sqlite3_column_int64(st, 0) + 1;
        sqlite3_finalize(st);
    }
    return true;
}

std::int64_t DecisionLedger::append(LedgerEntry e) {
    std::lock_guard<std::mutex> lk(mu_);
    if (db_ == nullptr) {
        e.seq = nextSeq_++;
        mem_.push_back(std::move(e));
        return mem_.back().seq;
    }
    sqlite3_stmt* st = nullptr;
    const char* sql =
        "INSERT INTO ayzr_decision_ledger"
        "(at_utc,type,correlation_id,opportunity_id,engine_id,reason_code,"
        " detail_ar,payload_json) VALUES(?,?,?,?,?,?,?,?);";
    if (sqlite3_prepare_v2(db_, sql, -1, &st, nullptr) != SQLITE_OK) return 0;
    sqlite3_bind_int64(st, 1, e.atUtc);
    sqlite3_bind_text(st, 2, toString(e.type), -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 3, e.correlationId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 4, e.opportunityId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 5, e.engineId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 6, e.reasonCode.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 7, e.detailAr.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 8, e.payloadJson.c_str(), -1, SQLITE_TRANSIENT);
    const int rc = sqlite3_step(st);
    sqlite3_finalize(st);
    if (rc != SQLITE_DONE) return 0;
    return sqlite3_last_insert_rowid(db_);
}

namespace {
void fillFromStmt(sqlite3_stmt* st, LedgerEntry& e) {
    e.seq = sqlite3_column_int64(st, 0);
    e.atUtc = sqlite3_column_int64(st, 1);
    std::string t = colText(st, 2);
    // Unknown types are surfaced as ENGINE_EVENT but keep the raw token.
    if (t == "OPPORTUNITY_CREATED") e.type = LedgerEventType::OPPORTUNITY_CREATED;
    else if (t == "OPPORTUNITY_UPDATED") e.type = LedgerEventType::OPPORTUNITY_UPDATED;
    else if (t == "OPPORTUNITY_MERGED") e.type = LedgerEventType::OPPORTUNITY_MERGED;
    else if (t == "CONFLICT_DETECTED") e.type = LedgerEventType::CONFLICT_DETECTED;
    else if (t == "DECISION_PUBLISHED") e.type = LedgerEventType::DECISION_PUBLISHED;
    else if (t == "DECISION_WITHHELD") e.type = LedgerEventType::DECISION_WITHHELD;
    else if (t == "DECISION_WATCH") e.type = LedgerEventType::DECISION_WATCH;
    else if (t == "RISK_ALERT") e.type = LedgerEventType::RISK_ALERT;
    else if (t == "ENGINE_FAILURE") e.type = LedgerEventType::ENGINE_FAILURE;
    else if (t == "DATA_QUALITY_FAILURE") e.type = LedgerEventType::DATA_QUALITY_FAILURE;
    else if (t == "OPPORTUNITY_EXPIRED") e.type = LedgerEventType::OPPORTUNITY_EXPIRED;
    else if (t == "SESSION_STATE") e.type = LedgerEventType::SESSION_STATE;
    else e.type = LedgerEventType::ENGINE_EVENT;
    e.correlationId = colText(st, 3);
    e.opportunityId = colText(st, 4);
    e.engineId = colText(st, 5);
    e.reasonCode = colText(st, 6);
    e.detailAr = colText(st, 7);
    e.payloadJson = colText(st, 8);
}
}  // namespace

std::vector<LedgerEntry> DecisionLedger::recent(std::size_t limit) const {
    std::lock_guard<std::mutex> lk(mu_);
    std::vector<LedgerEntry> out;
    if (db_ == nullptr) {
        const std::size_t n = (limit == 0 || limit > mem_.size()) ? mem_.size() : limit;
        out.assign(mem_.end() - static_cast<long>(n), mem_.end());
        return out;
    }
    const char* sql = limit == 0
        ? "SELECT seq,at_utc,type,correlation_id,opportunity_id,engine_id,"
          "reason_code,detail_ar,payload_json FROM ayzr_decision_ledger "
          "ORDER BY seq ASC;"
        : "SELECT seq,at_utc,type,correlation_id,opportunity_id,engine_id,"
          "reason_code,detail_ar,payload_json FROM ayzr_decision_ledger "
          "ORDER BY seq DESC LIMIT ?;";
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &st, nullptr) != SQLITE_OK) return out;
    if (limit != 0) sqlite3_bind_int64(st, 1, static_cast<sqlite3_int64>(limit));
    while (sqlite3_step(st) == SQLITE_ROW) {
        LedgerEntry e; fillFromStmt(st, e); out.push_back(std::move(e));
    }
    sqlite3_finalize(st);
    if (limit != 0) std::reverse(out.begin(), out.end());  // newest-first -> oldest-first
    return out;
}

std::vector<LedgerEntry> DecisionLedger::byOpportunity(
    const std::string& opportunityId) const {
    std::lock_guard<std::mutex> lk(mu_);
    std::vector<LedgerEntry> out;
    if (db_ == nullptr) {
        for (const auto& e : mem_) if (e.opportunityId == opportunityId) out.push_back(e);
        return out;
    }
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db_,
        "SELECT seq,at_utc,type,correlation_id,opportunity_id,engine_id,"
        "reason_code,detail_ar,payload_json FROM ayzr_decision_ledger "
        "WHERE opportunity_id=? ORDER BY seq ASC;", -1, &st, nullptr) != SQLITE_OK)
        return out;
    sqlite3_bind_text(st, 1, opportunityId.c_str(), -1, SQLITE_TRANSIENT);
    while (sqlite3_step(st) == SQLITE_ROW) {
        LedgerEntry e; fillFromStmt(st, e); out.push_back(std::move(e));
    }
    sqlite3_finalize(st);
    return out;
}

std::vector<LedgerEntry> DecisionLedger::byCorrelation(
    const std::string& correlationId) const {
    std::lock_guard<std::mutex> lk(mu_);
    std::vector<LedgerEntry> out;
    if (db_ == nullptr) {
        for (const auto& e : mem_) if (e.correlationId == correlationId) out.push_back(e);
        return out;
    }
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db_,
        "SELECT seq,at_utc,type,correlation_id,opportunity_id,engine_id,"
        "reason_code,detail_ar,payload_json FROM ayzr_decision_ledger "
        "WHERE correlation_id=? ORDER BY seq ASC;", -1, &st, nullptr) != SQLITE_OK)
        return out;
    sqlite3_bind_text(st, 1, correlationId.c_str(), -1, SQLITE_TRANSIENT);
    while (sqlite3_step(st) == SQLITE_ROW) {
        LedgerEntry e; fillFromStmt(st, e); out.push_back(std::move(e));
    }
    sqlite3_finalize(st);
    return out;
}

std::int64_t DecisionLedger::countOf(LedgerEventType type) const {
    std::lock_guard<std::mutex> lk(mu_);
    if (db_ == nullptr) {
        std::int64_t n = 0;
        for (const auto& e : mem_) if (e.type == type) ++n;
        return n;
    }
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db_,
        "SELECT COUNT(*) FROM ayzr_decision_ledger WHERE type=?;", -1, &st,
        nullptr) != SQLITE_OK) return -1;
    sqlite3_bind_text(st, 1, toString(type), -1, SQLITE_STATIC);
    std::int64_t n = 0;
    if (sqlite3_step(st) == SQLITE_ROW) n = sqlite3_column_int64(st, 0);
    sqlite3_finalize(st);
    return n;
}

std::size_t DecisionLedger::size() const {
    std::lock_guard<std::mutex> lk(mu_);
    if (db_ == nullptr) return mem_.size();
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db_, "SELECT COUNT(*) FROM ayzr_decision_ledger;",
                           -1, &st, nullptr) != SQLITE_OK) return 0;
    std::size_t n = 0;
    if (sqlite3_step(st) == SQLITE_ROW)
        n = static_cast<std::size_t>(sqlite3_column_int64(st, 0));
    sqlite3_finalize(st);
    return n;
}

}  // namespace eco
}  // namespace ayzr
}  // namespace aura
