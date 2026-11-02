// AYZR-ECO-API — JSON serialization (implementation).

#include "ayzr/ecosystem/EcosystemJson.h"

#include <cstdio>
#include <sstream>

namespace aura {
namespace ayzr {
namespace eco {
namespace {

std::string num(double v) {
    std::ostringstream o;
    o.precision(10);
    o << v;
    return o.str();
}
std::string i64(std::int64_t v) { return std::to_string(v); }

std::string optNum(const std::optional<double>& v) {
    return v.has_value() ? num(*v) : "null";
}
std::string optI64(const std::optional<std::int64_t>& v) {
    return v.has_value() ? i64(*v) : "null";
}
std::string optStr(const std::optional<std::string>& v) {
    return v.has_value() ? std::string("\"") + jsonEscape(*v) + "\"" : "null";
}

std::string levelJson(const PriceLevel& l) {
    return "{\"price\":" + num(l.price) + ",\"source\":\"" +
           jsonEscape(l.source) + "\",\"noteAr\":\"" + jsonEscape(l.noteAr) +
           "\"}";
}

std::string evidenceArray(const std::vector<Evidence>& v) {
    std::string s = "[";
    for (std::size_t i = 0; i < v.size(); ++i) {
        if (i) s += ",";
        s += "{\"code\":\"" + jsonEscape(v[i].code) + "\",\"textAr\":\"" +
             jsonEscape(v[i].textAr) + "\",\"value\":" + optNum(v[i].value) + "}";
    }
    return s + "]";
}

std::string stringArray(const std::vector<std::string>& v) {
    std::string s = "[";
    for (std::size_t i = 0; i < v.size(); ++i) {
        if (i) s += ",";
        s += "\"" + jsonEscape(v[i]) + "\"";
    }
    return s + "]";
}

}  // namespace

std::string jsonEscape(const std::string& s) {
    std::string o;
    o.reserve(s.size() + 8);
    for (unsigned char c : s) {
        switch (c) {
            case '"':  o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n";  break;
            case '\r': o += "\\r";  break;
            case '\t': o += "\\t";  break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    o += buf;
                } else {
                    o += static_cast<char>(c);   // UTF-8 bytes pass through
                }
        }
    }
    return o;
}

std::string engineResultJson(const EngineResult& r) {
    std::string j = "{";
    j += "\"eventId\":\"" + jsonEscape(r.eventId) + "\"";
    j += ",\"opportunityId\":" + optStr(r.opportunityId);
    j += ",\"engineId\":\"" + jsonEscape(r.engineId) + "\"";
    j += ",\"engineVersion\":\"" + jsonEscape(r.engineVersion) + "\"";
    j += ",\"strategyFamily\":\"" + std::string(toString(r.strategyFamily)) + "\"";
    j += ",\"strategyFamilyAr\":\"" +
         std::string(strategyFamilyLabelAr(r.strategyFamily)) + "\"";
    j += ",\"eventType\":\"" + jsonEscape(r.eventType) + "\"";
    j += ",\"symbol\":\"" + jsonEscape(r.symbol) + "\"";
    j += ",\"direction\":\"" + std::string(toString(r.direction)) + "\"";
    j += ",\"directionAr\":\"" +
         std::string(engineDirectionLabelAr(r.direction)) + "\"";
    j += ",\"timeframe\":\"" + std::string(toString(r.timeframe)) + "\"";
    j += ",\"createdAt\":" + i64(r.createdAt);
    j += ",\"updatedAt\":" + i64(r.updatedAt);
    j += ",\"expiresAt\":" + optI64(r.expiresAt);
    j += ",\"dataTimestamp\":" + i64(r.dataTimestamp);
    j += ",\"inputSnapshotReference\":\"" + jsonEscape(r.inputSnapshotReference) + "\"";
    j += ",\"dataSource\":\"" + jsonEscape(r.dataSource) + "\"";
    j += ",\"dataQuality\":\"" + std::string(aura::toString(r.dataQuality)) + "\"";
    j += ",\"status\":\"" + std::string(toString(r.status)) + "\"";
    j += ",\"statusAr\":\"" + std::string(eventStatusLabelAr(r.status)) + "\"";
    j += ",\"entry\":" + (r.entry ? levelJson(*r.entry) : "null");
    j += ",\"stopLoss\":" + (r.stopLoss ? levelJson(*r.stopLoss) : "null");
    j += ",\"takeProfits\":[";
    for (std::size_t i = 0; i < r.takeProfitLevels.size(); ++i) {
        if (i) j += ",";
        j += levelJson(r.takeProfitLevels[i]);
    }
    j += "]";
    j += ",\"reasonCodes\":" + stringArray(r.reasonCodes);
    j += ",\"explanationAr\":\"" + jsonEscape(r.explanationAr) + "\"";
    j += ",\"supportingEvidence\":" + evidenceArray(r.supportingEvidence);
    j += ",\"contradictingEvidence\":" + evidenceArray(r.contradictingEvidence);
    j += ",\"riskFlags\":" + stringArray(r.riskFlags);
    // Confidence: probability is emitted ONLY when calibrated; otherwise null.
    j += ",\"confidence\":{\"calibrated\":";
    j += r.confidence.calibrated ? "true" : "false";
    j += ",\"probability\":" + optNum(r.confidence.probability);
    j += ",\"calibrationRef\":\"" + jsonEscape(r.confidence.calibrationRef) + "\"";
    j += ",\"score\":" + optNum(r.confidence.score);
    j += ",\"scoreKind\":\"" + jsonEscape(r.confidence.scoreKind) + "\"}";
    j += ",\"correlationId\":\"" + jsonEscape(r.correlationId) + "\"";
    j += ",\"immutableCoreOutput\":";
    j += r.immutableCoreOutput ? "true" : "false";
    j += "}";
    return j;
}

std::string sessionsJson(const SessionSnapshot& snap) {
    std::string j = "{\"asOfUtc\":" + i64(snap.asOfUtc);
    j += ",\"londonNewYorkOverlap\":";
    j += snap.londonNewYorkOverlap ? "true" : "false";
    j += ",\"openSessionIds\":" + stringArray(snap.openSessionIds);
    j += ",\"unavailableZones\":" + stringArray(snap.unavailableZones);
    j += ",\"sessions\":[";
    for (std::size_t i = 0; i < snap.sessions.size(); ++i) {
        const SessionState& s = snap.sessions[i];
        if (i) j += ",";
        j += "{\"id\":\"" + jsonEscape(s.id) + "\"";
        j += ",\"nameAr\":\"" + jsonEscape(s.nameAr) + "\"";
        j += ",\"zoneId\":\"" + jsonEscape(s.zoneId) + "\"";
        j += ",\"phase\":\"" + std::string(toString(s.phase)) + "\"";
        j += ",\"phaseAr\":\"" + std::string(sessionPhaseLabelAr(s.phase)) + "\"";
        j += ",\"nextOpenUtc\":" + optI64(s.nextOpenUtc);
        j += ",\"nextCloseUtc\":" + optI64(s.nextCloseUtc);
        j += ",\"secondsToChange\":" + optI64(s.secondsToChange);
        j += ",\"inDst\":";
        j += s.inDst ? "true" : "false";
        if (s.localNow.available) {
            j += ",\"localTime\":\"" + formatLocal(s.localNow.local) + "\"";
            j += ",\"utcOffsetSeconds\":" + i64(s.localNow.utcOffsetSeconds);
        } else {
            j += ",\"localTime\":null,\"utcOffsetSeconds\":null";
        }
        j += ",\"error\":\"" + jsonEscape(s.error) + "\"}";
    }
    j += "]}";
    return j;
}

std::string enginesJson(const std::vector<EngineDescriptor>& engines) {
    std::string j = "{\"contractSchemaVersion\":" +
                    std::to_string(kEngineContractSchemaVersion);
    j += ",\"engines\":[";
    for (std::size_t i = 0; i < engines.size(); ++i) {
        const EngineDescriptor& e = engines[i];
        if (i) j += ",";
        j += "{\"engineId\":\"" + jsonEscape(e.engineId) + "\"";
        j += ",\"version\":\"" + jsonEscape(e.version) + "\"";
        j += ",\"strategyFamily\":\"" + std::string(toString(e.family)) + "\"";
        j += ",\"strategyFamilyAr\":\"" +
             std::string(strategyFamilyLabelAr(e.family)) + "\"";
        j += ",\"timeframe\":\"" + std::string(toString(e.timeframe)) + "\"";
        j += ",\"health\":\"" + std::string(toString(e.health)) + "\"";
        j += ",\"healthAr\":\"" + std::string(engineHealthLabelAr(e.health)) + "\"";
        j += ",\"noteAr\":\"" + jsonEscape(e.noteAr) + "\"}";
    }
    j += "]}";
    return j;
}

std::string opportunityJson(const Opportunity& op) {
    std::string j = "{\"opportunityId\":\"" + jsonEscape(op.opportunityId) + "\"";
    j += ",\"symbol\":\"" + jsonEscape(op.symbol) + "\"";
    j += ",\"timeframe\":\"" + std::string(toString(op.timeframe)) + "\"";
    j += ",\"direction\":\"" + std::string(toString(op.direction)) + "\"";
    j += ",\"directionAr\":\"" +
         std::string(engineDirectionLabelAr(op.direction)) + "\"";
    j += ",\"anchorBarUtc\":" + i64(op.anchorBarUtc);
    j += ",\"createdAt\":" + i64(op.createdAt);
    j += ",\"updatedAt\":" + i64(op.updatedAt);
    j += ",\"expiresAt\":" + optI64(op.expiresAt);
    j += ",\"state\":\"" + std::string(toString(op.state)) + "\"";
    j += ",\"stateAr\":\"" + std::string(decisionStateLabelAr(op.state)) + "\"";
    j += ",\"rationaleAr\":\"" + jsonEscape(op.rationaleAr) + "\"";
    j += ",\"entry\":" + (op.entry ? levelJson(*op.entry) : "null");
    j += ",\"stopLoss\":" + (op.stopLoss ? levelJson(*op.stopLoss) : "null");
    j += ",\"takeProfits\":[";
    for (std::size_t i = 0; i < op.takeProfitLevels.size(); ++i) {
        if (i) j += ",";
        j += levelJson(op.takeProfitLevels[i]);
    }
    j += "]";
    j += ",\"levelSourceEngine\":\"" + jsonEscape(op.levelSourceEngine) + "\"";
    j += ",\"agreeingEngines\":" + stringArray(op.agreeingEngines);
    j += ",\"disagreeingEngines\":" + stringArray(op.disagreeingEngines);
    j += ",\"conflictNotesAr\":" + stringArray(op.conflictNotesAr);
    j += ",\"riskFlags\":" + stringArray(op.riskFlags);
    j += ",\"astraCorePresent\":";
    j += op.astraCorePresent ? "true" : "false";
    j += ",\"hasConflict\":";
    j += op.hasConflict ? "true" : "false";
    j += ",\"contributingEvents\":[";
    for (std::size_t i = 0; i < op.contributingEvents.size(); ++i) {
        if (i) j += ",";
        j += engineResultJson(op.contributingEvents[i]);
    }
    j += "]}";
    return j;
}

std::string opportunitiesJson(const CycleResult& cycle) {
    std::string j = "{\"asOfUtc\":" + i64(cycle.asOfUtc);
    j += ",\"correlationId\":\"" + jsonEscape(cycle.correlationId) + "\"";
    j += ",\"engineFailures\":" + stringArray(cycle.engineFailures);
    j += ",\"dataQualityWarnings\":" + stringArray(cycle.dataQualityWarnings);
    j += ",\"opportunities\":[";
    for (std::size_t i = 0; i < cycle.opportunities.size(); ++i) {
        if (i) j += ",";
        j += opportunityJson(cycle.opportunities[i]);
    }
    j += "]}";
    return j;
}

std::string ledgerEntryJson(const LedgerEntry& e) {
    std::string j = "{\"seq\":" + i64(e.seq);
    j += ",\"atUtc\":" + i64(e.atUtc);
    j += ",\"type\":\"" + std::string(toString(e.type)) + "\"";
    j += ",\"typeAr\":\"" + std::string(ledgerEventLabelAr(e.type)) + "\"";
    j += ",\"correlationId\":\"" + jsonEscape(e.correlationId) + "\"";
    j += ",\"opportunityId\":\"" + jsonEscape(e.opportunityId) + "\"";
    j += ",\"engineId\":\"" + jsonEscape(e.engineId) + "\"";
    j += ",\"reasonCode\":\"" + jsonEscape(e.reasonCode) + "\"";
    j += ",\"detailAr\":\"" + jsonEscape(e.detailAr) + "\"";
    j += ",\"payloadJson\":\"" + jsonEscape(e.payloadJson) + "\"}";
    return j;
}

std::string ledgerJson(const std::vector<LedgerEntry>& entries) {
    std::string j = "{\"count\":" + i64(static_cast<std::int64_t>(entries.size()));
    j += ",\"entries\":[";
    for (std::size_t i = 0; i < entries.size(); ++i) {
        if (i) j += ",";
        j += ledgerEntryJson(entries[i]);
    }
    j += "]}";
    return j;
}

}  // namespace eco
}  // namespace ayzr
}  // namespace aura
