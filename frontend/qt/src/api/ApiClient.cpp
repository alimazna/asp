#include "ApiClient.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QJsonArray>
#include <QDebug>

namespace astra {

ApiClient::ApiClient(QObject* parent)
    : QObject(parent)
    , mBaseUrl("http://127.0.0.1:8790/api/v1/")
{
    connect(&mRetryTimer, &QTimer::timeout, this, [this]() {
        mRetryTimer.stop();
        mRetryCount++;
        // Exponential backoff: 1s, 2s, 4s, max 8s
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
            ApiEnvelope env;
            parseEnvelope(raw, env);
            AnalysisResponse resp;
            resp.envelope = env;
            resp.data = parseAnalysisData(env.data);
            mCurrentAnalysis = resp;
            emit analysisReceived(resp);
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
        [](const QByteArray& raw) {
            // History handled separately by consumer
            Q_UNUSED(raw);
        },
        [](const QString& msg, const QString& code) {
            Q_UNUSED(msg); Q_UNUSED(code);
        });
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

void ApiClient::handleReply(QNetworkReply* reply,
                             std::function<void(const QByteArray&)> onSuccess,
                             std::function<void(const QString&, const QString&)> onError) {
    QObject::connect(reply, &QNetworkReply::finished, this, [this, reply, onSuccess, onError]() {
        int idx = mPendingReplies.indexOf(reply);
        if (idx >= 0) mPendingReplies.removeAt(idx);

        if (reply->error() != QNetworkReply::NetworkError::NoError) {
            onError(reply->errorString(), "network_error");
            reply->deleteLater();
            return;
        }

        QByteArray raw = reply->readAll();
        reply->deleteLater();

        // Check for error body (flat {error:true, code, message})
        QJsonParseError err;
        QJsonDocument doc = QJsonDocument::fromJson(raw, &err);
        if (err.error == QJsonParseError::NoError) {
            QJsonObject obj = doc.object();
            if (obj.contains("error") && obj.value("error").toString() == "true") {
                QString code = obj.value("code").toString("unknown");
                QString message = obj.value("message").toString("Unknown error");
                onError(message, code);
                return;
            }
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
