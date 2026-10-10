#include "ApiClient.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QJsonArray>
#include <QDebug>

namespace astra {

ApiClient::ApiClient(QObject* parent)
    : QObject(parent)
    , mBaseUrl("http://127.0.0.1:8790/api/v1/")
{
    // Bound every request: without a transfer timeout a hung backend leaves
    // replies pending forever, so the UI never flips to OFFLINE.
    mNetworkManager.setTransferTimeout(5000);

    connect(&mRetryTimer, &QTimer::timeout, this, [this]() {
        mRetryTimer.stop();
        // Delay is 1s, 2s, 4s, then pinned at 8s. Clamp the exponent at 3 so a
        // long outage cannot shift int by >= 31 (undefined behaviour); the
        // qMin cap already makes any larger count redundant.
        if (mRetryCount < 3) mRetryCount++;
        int delay = qMin(1000 * (1 << mRetryCount), 8000);
        mRetryTimer.start(delay);
    });
}

void ApiClient::setBaseUrl(const QString& baseUrl) {
    if (!baseUrl.endsWith("/")) {
        mBaseUrl = baseUrl + "/";
    } else {
        mBaseUrl = baseUrl;
    }
}

QString ApiClient::baseUrl() const {
    return mBaseUrl;
}

void ApiClient::cancelAll() {
    for (auto* reply : std::exchange(mPendingReplies, {})) {
        reply->abort();
        reply->deleteLater();
    }
}

void ApiClient::fetchAnalysisLatest() {
    QNetworkRequest request(QUrl(mBaseUrl + "analysis/latest"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    auto* reply = mNetworkManager.get(request);
    mPendingReplies.append(reply);
    handleReply(reply,
        [this](const QByteArray& raw) {
            try {
                ApiEnvelope env;
                parseEnvelope(raw, env);
                AnalysisResponse resp;
                resp.envelope = env;
                resp.data = parseAnalysisData(env.data);
                mCurrentAnalysis = resp;
                emit analysisReceived(resp);
            } catch (const std::exception& ex) {
                emit error(QString::fromUtf8(ex.what()), "parse_error");
            }
        },
        [this](const QString& msg, const QString& code) {
            mIsOnline = false;
            emit offline();
            emit error(msg, code);
            scheduleRetry();
        });
}

void ApiClient::fetchHealth() {
    QNetworkRequest request(QUrl(mBaseUrl + "health/v1"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    auto* reply = mNetworkManager.get(request);
    mPendingReplies.append(reply);
    handleReply(reply,
        [this](const QByteArray& raw) {
            try {
                ApiEnvelope env;
                parseEnvelope(raw, env);
                HealthResponse resp;
                resp.envelope = env;
                resp.data = parseHealthData(env.data);
                mCurrentHealth = resp;
                mIsOnline = true;
                mIsDegraded = resp.data.status == "degraded";
                if (mIsOnline && mRetryCount > 0) {
                    mRetryCount = 0;
                }
                emit healthReceived(resp);
                if (mIsOnline) emit online();
            } catch (const std::exception& ex) {
                emit error(QString::fromUtf8(ex.what()), "parse_error");
            }
        },
        [this](const QString& msg, const QString& code) {
            mIsOnline = false;
            emit offline();
            emit error(msg, code);
            scheduleRetry();
        });
}

void ApiClient::fetchContextLatest() {
    QNetworkRequest request(QUrl(mBaseUrl + "context/latest"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    auto* reply = mNetworkManager.get(request);
    mPendingReplies.append(reply);
    handleReply(reply,
        [this](const QByteArray& raw) {
            ApiEnvelope env;
            parseEnvelope(raw, env);
            ContextResponse resp;
            resp.envelope = env;
            resp.data = parseContextData(env.data);
            mCurrentContext = resp;
            emit contextReceived(resp);
        },
        [this](const QString& msg, const QString& code) {
            emit error(msg, code);
        });
}

void ApiClient::fetchAnalysisHistory(int limit) {
    QNetworkRequest request(QUrl(mBaseUrl + "analysis/history?limit=" + QString::number(qBound(1, limit, 500))));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    auto* reply = mNetworkManager.get(request);
    mPendingReplies.append(reply);
    handleReply(reply,
        [this](const QByteArray& raw) {
            // GET /analysis/history -> envelope { api, schema, data: [ ... ] }
            // (data is an ARRAY here, not an object)
            try {
                QJsonParseError err;
                QJsonDocument doc = QJsonDocument::fromJson(raw, &err);
                if (err.error != QJsonParseError::NoError) {
                    throw std::runtime_error("JSON parse error: " + err.errorString().toStdString());
                }
                QJsonObject obj = doc.object();
                if (obj.value("api").toString() != "v1") {
                    throw std::runtime_error("Unexpected api field");
                }
                if (obj.value("schema").toString() != "1.0") {
                    throw std::runtime_error("Unexpected schema field");
                }
                QVector<AnalysisData> items;
                const QJsonArray arr = obj.value("data").toArray();
                items.reserve(arr.size());
                for (const QJsonValue& v : arr) {
                    items.append(parseAnalysisData(v.toObject()));
                }
                mCurrentHistory = items;
                mIsOnline = true;
                emit historyReceived(items);
            } catch (const std::exception& ex) {
                emit error(QString::fromUtf8(ex.what()), "parse_error");
            }
        },
        [this](const QString& msg, const QString& code) {
            emit error(msg, code);
        });
}

void ApiClient::fetchCandles(const QString& tf, int limit) {
    // The frontend never talks to MT5 or the bridge directly: this is the only
    // candle source, and it goes through the C++ loopback API.
    QNetworkRequest request(QUrl(mBaseUrl + "candles?tf=" + tf +
                                 "&limit=" + QString::number(qBound(1, limit, 1000))));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    auto* reply = mNetworkManager.get(request);
    mPendingReplies.append(reply);
    handleReply(reply,
        [this](const QByteArray& raw) {
            try {
                ApiEnvelope env;
                parseEnvelope(raw, env);
                CandlesResponse resp;
                resp.envelope = env;
                resp.data = parseCandlesData(env.data);
                mCurrentCandles = resp;
                mIsOnline = true;
                emit candlesReceived(resp);
            } catch (const std::exception& ex) {
                emit error(QString::fromUtf8(ex.what()), "parse_error");
            }
        },
        [this](const QString& msg, const QString& code) {
            emit error(msg, code);
        });
}

void ApiClient::fetchResearchStatus() {
    QNetworkRequest request(QUrl(mBaseUrl + "research/status"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    auto* reply = mNetworkManager.get(request);
    mPendingReplies.append(reply);
    handleReply(reply,
        [this](const QByteArray& raw) {
            try {
                ApiEnvelope env;
                parseEnvelope(raw, env);
                mCurrentResearch = parseResearchData(env.data);
                emit researchReceived(mCurrentResearch);
            } catch (const std::exception& ex) {
                emit error(QString::fromUtf8(ex.what()), "parse_error");
            }
        },
        [this](const QString& msg, const QString& code) { emit error(msg, code); });
}

void ApiClient::fetchGovernanceStatus() {
    QNetworkRequest request(QUrl(mBaseUrl + "governance/status"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    auto* reply = mNetworkManager.get(request);
    mPendingReplies.append(reply);
    handleReply(reply,
        [this](const QByteArray& raw) {
            try {
                ApiEnvelope env;
                parseEnvelope(raw, env);
                mCurrentGovernance = parseGovernanceData(env.data);
                emit governanceReceived(mCurrentGovernance);
            } catch (const std::exception& ex) {
                emit error(QString::fromUtf8(ex.what()), "parse_error");
            }
        },
        [this](const QString& msg, const QString& code) { emit error(msg, code); });
}

void ApiClient::fetchAuditRecent() {
    QNetworkRequest request(QUrl(mBaseUrl + "audit/recent"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    auto* reply = mNetworkManager.get(request);
    mPendingReplies.append(reply);
    handleReply(reply,
        [this](const QByteArray& raw) {
            try {
                ApiEnvelope env;
                parseEnvelope(raw, env);
                mCurrentAudit = parseAuditData(env.data);
                emit auditReceived(mCurrentAudit);
            } catch (const std::exception& ex) {
                emit error(QString::fromUtf8(ex.what()), "parse_error");
            }
        },
        [this](const QString& msg, const QString& code) { emit error(msg, code); });
}

void ApiClient::fetchSystemState() {
    QNetworkRequest request(QUrl(mBaseUrl + "system/state"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    auto* reply = mNetworkManager.get(request);
    mPendingReplies.append(reply);
    handleReply(reply,
        [this](const QByteArray& raw) {
            try {
                ApiEnvelope env;
                parseEnvelope(raw, env);
                mCurrentSystemState = parseSystemStateData(env.data);
                emit systemStateReceived(mCurrentSystemState);
            } catch (const std::exception& ex) {
                emit error(QString::fromUtf8(ex.what()), "parse_error");
            }
        },
        [this](const QString& msg, const QString& code) { emit error(msg, code); });
}

void ApiClient::parseEnvelope(const QByteArray& raw, ApiEnvelope& envelope) {
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(raw, &err);
    if (err.error != QJsonParseError::NoError) {
        throw std::runtime_error("JSON parse error: " + err.errorString().toStdString());
    }
    QJsonObject obj = doc.object();
    envelope.api = obj.value("api").toString();
    envelope.schema = obj.value("schema").toString();
    envelope.data = obj.value("data").toObject();

    if (envelope.api != "v1") {
        throw std::runtime_error("Unexpected api field: " + envelope.api.toStdString());
    }
    if (envelope.schema != "1.0") {
        throw std::runtime_error("Unexpected schema field: " + envelope.schema.toStdString());
    }
}

AnalysisData ApiClient::parseAnalysisData(const QJsonObject& obj) {
    AnalysisData d;

    // timestamp — may be null
    QJsonValue tsVal = obj.value("timestamp");
    if (!isNullOrMissing(tsVal)) {
        d.timestamp = tsVal.toString();
    }

    d.symbol = obj.value("symbol").toString("XAUUSD");

    // context
    QJsonObject ctxObj = obj.value("context").toObject();
    d.context.regime = ctxObj.value("regime").toString("UNKNOWN");
    d.context.h4Bias = ctxObj.value("h4_bias").toString("NONE");
    d.context.m15Trigger = ctxObj.value("m15_trigger").toString("NONE");
    QJsonValue mtfVal = ctxObj.value("mtf_agreement");
    if (!isNullOrMissing(mtfVal) && mtfVal.isDouble()) {
        d.context.mtfAgreement = mtfVal.toDouble();
    }
    d.context.volatilityState = ctxObj.value("volatility_state").toString("UNKNOWN");

    // signal
    QJsonObject sigObj = obj.value("signal").toObject();
    d.signal.direction = sigObj.value("direction").toString("NONE");
    QJsonValue probVal = sigObj.value("probability");
    if (!isNullOrMissing(probVal) && probVal.isDouble()) {
        d.signal.probability = probVal.toDouble();
    }
    d.signal.probabilityCalibrated = sigObj.value("probability_calibrated").toBool(false);
    QJsonValue scoreVal = sigObj.value("score");
    if (!isNullOrMissing(scoreVal) && scoreVal.isDouble()) {
        d.signal.score = scoreVal.toDouble();
    } else {
        d.signal.score = 0.5; // fallback — should not happen
    }
    QJsonValue cloVal = sigObj.value("confidence_lo");
    if (!isNullOrMissing(cloVal) && cloVal.isDouble()) {
        d.signal.confidenceLo = cloVal.toDouble();
    }
    QJsonValue chiVal = sigObj.value("confidence_hi");
    if (!isNullOrMissing(chiVal) && chiVal.isDouble()) {
        d.signal.confidenceHi = chiVal.toDouble();
    }
    QJsonValue mvVal = sigObj.value("model_version");
    if (!isNullOrMissing(mvVal)) {
        d.signal.modelVersion = mvVal.toString();
    }
    QJsonValue fcVal = sigObj.value("features_contributing");
    if (fcVal.isArray()) {
        for (const QJsonValue& v : fcVal.toArray()) {
            d.signal.featuresContributing.append(v.toString());
        }
    }

    // levels
    QJsonObject lvlObj = obj.value("levels").toObject();
    auto optDouble = [&](const QString& key) -> std::optional<double> {
        QJsonValue v = lvlObj.value(key);
        if (isNullOrMissing(v) || !v.isDouble()) return std::nullopt;
        return v.toDouble();
    };
    auto optString = [&](const QString& key) -> std::optional<QString> {
        QJsonValue v = lvlObj.value(key);
        if (isNullOrMissing(v)) return std::nullopt;
        return v.toString();
    };
    d.levels.entry = optDouble("entry");
    d.levels.stopLoss = optDouble("stop_loss");
    d.levels.takeProfit = optDouble("take_profit");
    d.levels.rewardRisk = optDouble("reward_risk");
    d.levels.suggestedRiskPct = optDouble("suggested_risk_pct");
    d.levels.slMethod = optString("sl_method");
    d.levels.tpMethod = optString("tp_method");

    // meta
    QJsonObject metaObj = obj.value("meta").toObject();
    d.meta.coverageTier = metaObj.value("coverage_tier").toString("unknown");
    QJsonValue fsVal = metaObj.value("data_freshness_sec");
    if (!isNullOrMissing(fsVal) && fsVal.isDouble()) {
        d.meta.dataFreshnessSec = fsVal.toInt();
    }
    d.meta.degraded = metaObj.value("degraded").toBool(false);
    d.meta.scoreIsProbability = metaObj.value("score_is_probability").toBool(false);
    d.meta.disclaimer = metaObj.value("disclaimer").toString("Decision support only. Not financial advice.");

    return d;
}

ContextData ApiClient::parseContextData(const QJsonObject& obj) {
    ContextData d;
    QJsonValue tsVal = obj.value("timestamp");
    if (!isNullOrMissing(tsVal)) {
        d.timestamp = tsVal.toString();
    }
    d.symbol = obj.value("symbol").toString("XAUUSD");
    QJsonObject ctxObj = obj.value("context").toObject();
    d.context.regime = ctxObj.value("regime").toString("UNKNOWN");
    d.context.h4Bias = ctxObj.value("h4_bias").toString("NONE");
    d.context.m15Trigger = ctxObj.value("m15_trigger").toString("NONE");
    QJsonValue mtfVal = ctxObj.value("mtf_agreement");
    if (!isNullOrMissing(mtfVal) && mtfVal.isDouble()) {
        d.context.mtfAgreement = mtfVal.toDouble();
    }
    d.context.volatilityState = ctxObj.value("volatility_state").toString("UNKNOWN");
    return d;
}

HealthData ApiClient::parseHealthData(const QJsonObject& obj) {
    HealthData d;
    d.status = obj.value("status").toString("offline");
    d.bridge = obj.value("bridge").toString("offline");
    d.version = obj.value("version").toString("v1");
    QJsonValue upVal = obj.value("uptime_sec");
    if (!isNullOrMissing(upVal) && upVal.isDouble()) {
        d.uptimeSec = upVal.toInt();
    }
    QJsonValue tierVal = obj.value("coverage_tier");
    if (!isNullOrMissing(tierVal)) {
        d.coverageTier = tierVal.toString();
    }
    return d;
}

CandlesData ApiClient::parseCandlesData(const QJsonObject& obj) {
    CandlesData d;
    d.timeframe = obj.value("timeframe").toString();
    d.symbol = obj.value("symbol").toString();
    d.freshness = obj.value("freshness").toString("UNKNOWN");

    const QJsonArray arr = obj.value("bars").toArray();
    d.bars.reserve(arr.size());
    for (const QJsonValue& v : arr) {
        const QJsonObject bar = v.toObject();
        Candle c;
        c.time = static_cast<qint64>(bar.value("time").toDouble());
        c.open = bar.value("open").toDouble();
        c.high = bar.value("high").toDouble();
        c.low = bar.value("low").toDouble();
        c.close = bar.value("close").toDouble();
        c.tickVolume = static_cast<qint64>(bar.value("tick_volume").toDouble());
        c.spread = bar.value("spread").toInt();
        c.realVolume = static_cast<qint64>(bar.value("real_volume").toDouble());
        d.bars.append(c);
    }
    // Only a non-empty, well-formed series counts as available. A count field
    // that disagrees with the array is treated as unavailable, never padded.
    const int declared = obj.value("count").toInt(-1);
    d.available = !d.bars.isEmpty() &&
                  (declared < 0 || declared == d.bars.size());
    if (!d.available) d.bars.clear();
    return d;
}

ResearchData ApiClient::parseResearchData(const QJsonObject& obj) {
    ResearchData d;
    d.available = obj.value("available").toBool(false);
    d.mode = obj.value("mode").toString();
    d.note = obj.value("note").toString();
    d.experimentCount = obj.value("experiment_count").toInt(0);
    d.failureCount = obj.value("failure_count").toInt(0);
    for (const QJsonValue& v : obj.value("experiments").toArray()) {
        const QJsonObject e = v.toObject();
        ResearchExperiment r;
        r.experimentId = e.value("experiment_id").toString();
        r.hypothesisId = e.value("hypothesis_id").toString();
        r.method = e.value("method").toString();
        r.outcome = e.value("outcome").toString();
        if (e.value("sample_size").isDouble()) r.sampleSize = e.value("sample_size").toDouble();
        if (e.value("result_metric").isDouble()) r.resultMetric = e.value("result_metric").toDouble();
        d.experiments.append(r);
    }
    for (const QJsonValue& v : obj.value("failures").toArray()) {
        const QJsonObject f = v.toObject();
        ResearchFailure r;
        r.failureId = f.value("failure_id").toString();
        r.category = f.value("category").toString();
        r.summary = f.value("summary").toString();
        if (f.value("occurrences").isDouble()) r.occurrences = f.value("occurrences").toDouble();
        r.resolved = f.value("resolved").toBool(false);
        d.failures.append(r);
    }
    return d;
}

GovernanceData ApiClient::parseGovernanceData(const QJsonObject& obj) {
    GovernanceData d;
    d.available = true;
    d.liveTradingAuthorised = obj.value("live_trading_authorised").toBool(false);
    d.pendingCount = obj.value("pending_count").toInt(0);
    auto readReq = [](const QJsonObject& o) {
        ApprovalRequest r;
        r.requestId = o.value("request_id").toString();
        r.kind = o.value("kind").toString();
        r.subjectId = o.value("subject_id").toString();
        r.status = o.value("status").toString();
        r.requestedBy = o.value("requested_by").toString();
        return r;
    };
    for (const QJsonValue& v : obj.value("pending").toArray())
        d.pending.append(readReq(v.toObject()));
    for (const QJsonValue& v : obj.value("history").toArray())
        d.history.append(readReq(v.toObject()));
    return d;
}

AuditData ApiClient::parseAuditData(const QJsonObject& obj) {
    AuditData d;
    d.available = true;
    d.count = obj.value("count").toInt(0);
    d.auditStreamSize = obj.value("audit_stream_size").toInt(0);
    for (const QJsonValue& v : obj.value("audit_records").toArray()) {
        const QJsonObject o = v.toObject();
        AuditRecord r;
        r.sequence = static_cast<qint64>(o.value("sequence").toDouble());
        r.eventId = o.value("event_id").toString();
        r.action = o.value("action").toString();
        r.outcome = o.value("outcome").toString();
        r.serviceState = o.value("service_state").toString();
        r.actor = o.value("actor").toString();
        r.subject = o.value("subject").toString();
        r.details = o.value("details").toString();
        d.records.append(r);
    }
    for (const QJsonValue& v : obj.value("active_incidents").toArray()) {
        const QJsonObject o = v.toObject();
        IncidentRecord r;
        r.incidentId = o.value("incident_id").toString();
        r.severity = o.value("severity").toString();
        r.state = o.value("state").toString();
        r.title = o.value("title").toString();
        d.incidents.append(r);
    }
    return d;
}

SystemStateData ApiClient::parseSystemStateData(const QJsonObject& obj) {
    SystemStateData d;
    d.available = true;
    d.mode = obj.value("mode").toString();
    d.shadowOnly = obj.value("shadow_only").toBool(false);
    d.ready = obj.value("ready").toBool(false);
    d.bridgeState = obj.value("bridge_state").toString();
    d.startupStage = obj.value("startup_stage").toString();
    return d;
}

void ApiClient::handleReply(QNetworkReply* reply,
                             std::function<void(const QByteArray&)> onSuccess,
                             std::function<void(const QString&, const QString&)> onError) {
    QObject::connect(reply, &QNetworkReply::finished, this, [this, reply, onSuccess, onError]() {
        int idx = mPendingReplies.indexOf(reply);
        if (idx >= 0) mPendingReplies.removeAt(idx);

        // Read the body first: a non-2xx reply still carries the structured
        // {error:true, code, message} envelope, and that code is meaningful
        // (e.g. dependency_unavailable vs unknown_timeframe). Parse it before
        // falling back to a generic network_error.
        const QByteArray raw = reply->readAll();
        const QNetworkReply::NetworkError netErr = reply->error();
        const QString netErrString = reply->errorString();
        reply->deleteLater();

        QJsonParseError err;
        QJsonDocument doc = QJsonDocument::fromJson(raw, &err);
        const bool haveJson = (err.error == QJsonParseError::NoError);
        if (haveJson) {
            QJsonObject obj = doc.object();
            const QJsonValue errVal = obj.value("error");
            // Contract requires the string "true"; accept a JSON boolean too so
            // the structured code survives regardless of the emitter's form.
            const bool isError = (errVal.toString() == "true") ||
                                 (errVal.isBool() && errVal.toBool());
            if (isError) {
                QString code = obj.value("code").toString("unknown");
                QString message = obj.value("message").toString("Unknown error");
                onError(message, code);
                return;
            }
        }

        if (netErr != QNetworkReply::NetworkError::NoError) {
            onError(netErrString, "network_error");
            return;
        }

        onSuccess(raw);
    });
}

void ApiClient::scheduleRetry() {
    if (!mRetryTimer.isActive()) {
        mRetryTimer.start(1000);  // first retry after 1s
    }
}

}  // namespace astra
