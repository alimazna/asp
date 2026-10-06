// PKG-0002 - Backend facade implementation.

#include "api/BackendFacade.h"

#include <sstream>

namespace aura {

namespace {

ApiResponse unavailable(const std::string& what) {
    return errorResponse(503, "dependency_unavailable",
                         std::string("backend component unavailable: ") + what);
}

std::string qualityJson(DataQualityState quality) {
    std::vector<ApiField> fields;
    fields.push_back({"state", toString(quality)});
    fields.push_back({"decision_grade", isDecisionGrade(quality) ? "true" : "false",
                      true});
    return jsonObject(fields);
}

}  // namespace

ApiResponse BackendFacade::systemState() const {
    if (deps_.runtime == nullptr) return unavailable("runtime");
    const SystemMode mode = deps_.runtime->mode();
    std::vector<ApiField> fields;
    fields.push_back({"mode", toString(mode)});
    fields.push_back({"shadow_only", jsonBool(true), true});
    fields.push_back({"ready", jsonBool(deps_.runtime->isReady()), true});
    fields.push_back({"bridge_state", toString(deps_.runtime->bridgeState())});
    fields.push_back({"api", kApiVersion});
    fields.push_back({"schema", kApiSchemaVersion.toString()});
    return ApiResponse{200, "application/json",
                       envelope(jsonObject(fields)), true};
}

ApiResponse BackendFacade::health() const {
    if (deps_.health == nullptr) return unavailable("health monitor");
    const BackendHealth snapshot = deps_.health->snapshot();

    std::vector<std::string> reasons;
    for (const auto& reason : snapshot.degradedReasons) {
        reasons.push_back(jsonString(reason));
    }
    std::vector<ApiField> fields;
    fields.push_back({"aggregate", toString(snapshot.aggregate)});
    fields.push_back({"decision_grade_data", jsonBool(snapshot.decisionGradeData), true});
    fields.push_back({"degraded_reasons", jsonArray(reasons), true});
    fields.push_back({"observed_at", jsonInteger(snapshot.observedAt.epochMillis()), true});
    fields.push_back({"unknown_is_not_safe", jsonBool(true), true});
    return ApiResponse{200, "application/json",
                       envelope(jsonObject(fields)), true};
}

ApiResponse BackendFacade::timeframes() const {
    if (deps_.runtime == nullptr) return unavailable("runtime");
    const auto snapshot = deps_.runtime->timeframeStore().snapshot();

    std::vector<std::string> elements;
    for (const auto& kv : snapshot) {
        const TimeframeState& state = kv.second;
        std::vector<ApiField> fields;
        fields.push_back({"timeframe", kv.first});
        fields.push_back({"has_closed_bar", jsonBool(state.hasClosedBar), true});
        fields.push_back({"quality", toString(state.quality)});
        fields.push_back({"decision_grade", jsonBool(isDecisionGrade(state.quality)), true});
        fields.push_back({"last_closed_bar_open",
                          jsonInteger(state.lastClosedBar.openTimeSec), true});
        fields.push_back({"sequence",
                          jsonInteger(static_cast<std::int64_t>(state.sequence)), true});
        elements.push_back(jsonObject(fields));
    }
    return ApiResponse{200, "application/json",
                       envelope(jsonArray(elements)), true};
}

ApiResponse BackendFacade::timeframeSnapshot(const std::string& timeframe) const {
    if (deps_.runtime == nullptr) return unavailable("runtime");
    Timeframe parsed;
    bool matched = false;
    for (Timeframe tf : allTimeframes()) {
        if (timeframe == toString(tf)) {
            parsed = tf;
            matched = true;
            break;
        }
    }
    if (!matched) {
        return errorResponse(400, "unknown_timeframe",
                             "unknown timeframe: " + timeframe);
    }

    TimeframeState state;
    if (!deps_.runtime->timeframeStore().get(parsed, state)) {
        // Explicit not-observed state; never an empty-but-fine snapshot.
        std::vector<ApiField> fields;
        fields.push_back({"timeframe", timeframe});
        fields.push_back({"has_closed_bar", jsonBool(false), true});
        fields.push_back({"quality", toString(DataQualityState::UNKNOWN)});
        fields.push_back({"observed", jsonBool(false), true});
        return ApiResponse{200, "application/json",
                           envelope(jsonObject(fields)), true};
    }

    std::vector<ApiField> fields;
    fields.push_back({"timeframe", timeframe});
    fields.push_back({"has_closed_bar", jsonBool(state.hasClosedBar), true});
    fields.push_back({"observed", jsonBool(true), true});
    fields.push_back({"quality", qualityJson(state.quality), true});
    fields.push_back({"sequence",
                      jsonInteger(static_cast<std::int64_t>(state.sequence)), true});
    fields.push_back({"open", jsonNumber(state.lastClosedBar.open), true});
    fields.push_back({"high", jsonNumber(state.lastClosedBar.high), true});
    fields.push_back({"low", jsonNumber(state.lastClosedBar.low), true});
    fields.push_back({"close", jsonNumber(state.lastClosedBar.close), true});
    fields.push_back({"open_time",
                      jsonInteger(state.lastClosedBar.openTimeSec), true});
    return ApiResponse{200, "application/json",
                       envelope(jsonObject(fields)), true};
}

ApiResponse BackendFacade::latestSignal() const {
    if (deps_.ledger == nullptr) return unavailable("prediction ledger");
    const auto& records = deps_.ledger->records();
    if (records.empty()) {
        std::vector<ApiField> fields;
        fields.push_back({"available", jsonBool(false), true});
        fields.push_back({"reason", "no decisions recorded yet"});
        return ApiResponse{200, "application/json",
                           envelope(jsonObject(fields)), true};
    }
    const PredictionRecord& record = records.back();
    std::vector<ApiField> fields;
    fields.push_back({"available", jsonBool(true), true});
    fields.push_back({"decision_id", record.decisionId.value()});
    fields.push_back({"trigger_timeframe", toString(record.timeframe)});
    fields.push_back({"closed_bar_id", jsonInteger(record.asOfBarOpenSec), true});
    fields.push_back({"signal", toString(record.direction)});
    fields.push_back({"score", jsonNumber(record.score), true});
    fields.push_back({"score_is_probability", jsonBool(false), true});
    fields.push_back({"confidence", jsonNumber(record.confidence), true});
    // Probability stays explicitly N/A until a calibration exists.
    fields.push_back({"probability", "null", true});
    fields.push_back({"probability_calibrated", jsonBool(false), true});
    fields.push_back({"reference_price", jsonNumber(record.referencePrice), true});
    fields.push_back({"system_mode", "SHADOW"});
    fields.push_back({"has_outcome", jsonBool(record.hasOutcome), true});
    return ApiResponse{200, "application/json",
                       envelope(jsonObject(fields)), true};
}

ApiResponse BackendFacade::latestRisk() const {
    // Risk proposals are surfaced through the shadow command view; the facade
    // does not recompute risk.
    if (deps_.positions == nullptr) return unavailable("position simulator");
    std::vector<ApiField> fields;
    fields.push_back({"available", jsonBool(true), true});
    fields.push_back({"open_positions", jsonInteger(
        static_cast<std::int64_t>(deps_.positions->openCount())), true});
    fields.push_back({"aggregate_open_risk_fraction",
                      jsonNumber(deps_.positions->aggregateOpenRiskFraction()), true});
    fields.push_back({"risk_bounded_by_guardian", jsonBool(true), true});
    return ApiResponse{200, "application/json",
                       envelope(jsonObject(fields)), true};
}

ApiResponse BackendFacade::shadowPositions() const {
    if (deps_.positions == nullptr) return unavailable("position simulator");
    std::vector<std::string> elements;
    for (const auto& position : deps_.positions->positions()) {
        std::vector<ApiField> fields;
        fields.push_back({"position_id", position.positionId.value()});
        fields.push_back({"decision_id", position.decisionId.value()});
        fields.push_back({"direction", toString(position.direction)});
        fields.push_back({"state", toString(position.state)});
        fields.push_back({"entry_price", jsonNumber(position.entryPrice), true});
        fields.push_back({"stop_price", jsonNumber(position.stopPrice), true});
        fields.push_back({"target_price", jsonNumber(position.targetPrice), true});
        fields.push_back({"lots", jsonNumber(position.lots), true});
        fields.push_back({"realized_r", jsonNumber(position.realizedR), true});
        fields.push_back({"shadow_only", jsonBool(true), true});
        elements.push_back(jsonObject(fields));
    }
    return ApiResponse{200, "application/json",
                       envelope(jsonArray(elements)), true};
}

ApiResponse BackendFacade::shadowOutcomes() const {
    if (deps_.ledger == nullptr) return unavailable("prediction ledger");
    std::vector<std::string> elements;
    for (const auto& record : deps_.ledger->records()) {
        if (!record.hasOutcome) continue;
        std::vector<ApiField> fields;
        fields.push_back({"decision_id", record.decisionId.value()});
        fields.push_back({"outcome_id", record.outcomeId.value()});
        fields.push_back({"realized_r", jsonNumber(record.realizedR), true});
        fields.push_back({"outcome_class", toString(record.outcomeClass)});
        elements.push_back(jsonObject(fields));
    }
    return ApiResponse{200, "application/json",
                       envelope(jsonArray(elements)), true};
}

ApiResponse BackendFacade::researchStatus() const {
    std::vector<ApiField> fields;
    fields.push_back({"available", jsonBool(true), true});
    fields.push_back({"mode", "SHADOW"});
    fields.push_back({"note",
                      "research output never grants execution authority"});
    return ApiResponse{200, "application/json",
                       envelope(jsonObject(fields)), true};
}

ApiResponse BackendFacade::governanceStatus() const {
    if (deps_.approvals == nullptr) return unavailable("approval gate");
    std::vector<std::string> pending;
    for (const auto& request : deps_.approvals->pending()) {
        std::vector<ApiField> fields;
        fields.push_back({"request_id", request.requestId.value()});
        fields.push_back({"kind", toString(request.kind)});
        fields.push_back({"subject_id", request.subjectId.value()});
        fields.push_back({"requested_at",
                          jsonInteger(request.requestedAt.epochMillis()), true});
        pending.push_back(jsonObject(fields));
    }
    std::vector<ApiField> fields;
    fields.push_back({"pending_count", jsonInteger(
        static_cast<std::int64_t>(pending.size())), true});
    fields.push_back({"pending", jsonArray(pending), true});
    fields.push_back({"live_trading_authorised", jsonBool(false), true});
    return ApiResponse{200, "application/json",
                       envelope(jsonObject(fields)), true};
}

ApiResponse BackendFacade::recentAudit() const {
    // The facade surfaces incident-derived audit context; the full audit
    // stream lives in the persistence layer and is queried by the backend.
    if (deps_.incidents == nullptr) return unavailable("incident manager");
    std::vector<std::string> active;
    for (const auto& incident : deps_.incidents->active()) {
        std::vector<ApiField> fields;
        fields.push_back({"incident_id", incident.incidentId.value()});
        fields.push_back({"severity", toString(incident.severity)});
        fields.push_back({"state", toString(incident.state)});
        fields.push_back({"title", incident.title});
        active.push_back(jsonObject(fields));
    }
    std::vector<ApiField> fields;
    fields.push_back({"active_incidents", jsonArray(active), true});
    fields.push_back({"count", jsonInteger(
        static_cast<std::int64_t>(active.size())), true});
    return ApiResponse{200, "application/json",
                       envelope(jsonObject(fields)), true};
}

ApiResponse BackendFacade::handle(const std::string& method,
                                  const std::string& path) const {
    if (method != "GET") {
        return errorResponse(405, "method_not_allowed",
                             "only GET is supported on read routes");
    }
    if (path == "/api/v1/system/state") return systemState();
    if (path == "/api/v1/health") return health();
    if (path == "/api/v1/timeframes") return timeframes();
    if (path == "/api/v1/signals/latest") return latestSignal();
    if (path == "/api/v1/risk/latest") return latestRisk();
    if (path == "/api/v1/shadow/positions") return shadowPositions();
    if (path == "/api/v1/shadow/outcomes") return shadowOutcomes();
    if (path == "/api/v1/research/status") return researchStatus();
    if (path == "/api/v1/governance/status") return governanceStatus();
    if (path == "/api/v1/audit/recent") return recentAudit();

    const std::string prefix = "/api/v1/timeframes/";
    if (path.size() > prefix.size() &&
        path.compare(0, prefix.size(), prefix) == 0) {
        const std::string tail = path.substr(prefix.size());
        const std::string suffix = "/snapshot";
        if (tail.size() > suffix.size() &&
            tail.compare(tail.size() - suffix.size(), suffix.size(), suffix) == 0) {
            const std::string timeframe =
                tail.substr(0, tail.size() - suffix.size());
            return timeframeSnapshot(timeframe);
        }
    }
    return errorResponse(404, "not_found", "unknown route: " + path);
}

ApiResponse BackendFacade::command(const CommandRequest& request) {
    // Allow-list only. There is deliberately no command that can enable live
    // execution or bypass policy.
    const std::vector<std::string> allowed = {"notify", "request_approval"};
    bool permitted = false;
    for (const auto& name : allowed) {
        if (request.command == name) permitted = true;
    }
    if (!permitted) {
        return errorResponse(403, "command_not_permitted",
                             "command is not on the allow-list: " +
                                 request.command);
    }
    if (request.actor.empty()) {
        return errorResponse(400, "actor_required",
                             "every command must be attributable to an actor");
    }

    if (request.command == "notify") {
        if (deps_.telegram == nullptr) return unavailable("telegram gateway");
        const EntityId id =
            deps_.telegram->enqueue("", request.payload, Timestamp::now());
        if (id.empty()) {
            return errorResponse(429, "queue_full",
                                 "notification queue is full");
        }
        commandLog_.push_back(request.command + ":" + id.value());
        std::vector<ApiField> fields;
        fields.push_back({"accepted", jsonBool(true), true});
        fields.push_back({"message_id", id.value()});
        return ApiResponse{202, "application/json",
                           envelope(jsonObject(fields)), true};
    }

    // request_approval
    if (deps_.approvals == nullptr) return unavailable("approval gate");
    const EntityId id = deps_.approvals->request(
        ApprovalRequestKind::POLICY_CHANGE, EntityId(), request.actor,
        request.payload, Timestamp::now());
    commandLog_.push_back(request.command + ":" + id.value());
    std::vector<ApiField> fields;
    fields.push_back({"accepted", jsonBool(true), true});
    fields.push_back({"request_id", id.value()});
    fields.push_back({"status", toString(ApprovalStatus::PENDING)});
    return ApiResponse{202, "application/json",
                       envelope(jsonObject(fields)), true};
}

}  // namespace aura
