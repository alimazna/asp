/// Domain models for the ASTRA analysis surface (`/api/v1/analysis/*`).
///
/// Parsing is deliberately defensive: an absent or wrongly-typed field becomes
/// `null` rather than a fabricated default. RULE C is honored here — the
/// calibrated `probability` is only exposed as a probability when the backend
/// sets `probability_calibrated: true`; otherwise the raw `score` is surfaced
/// separately and the UI labels it a "درجة" (score), never a probability.
library;

double? _asDouble(Object? v) {
  if (v is num) return v.toDouble();
  if (v is String) return double.tryParse(v);
  return null;
}

int? _asInt(Object? v) {
  if (v is int) return v;
  if (v is num) return v.toInt();
  if (v is String) return int.tryParse(v);
  return null;
}

String? _asString(Object? v) {
  if (v is String) return v;
  return null;
}

class MarketContext {
  const MarketContext({
    required this.regime,
    required this.h4Bias,
    required this.m15Trigger,
    required this.mtfAgreement,
    required this.volatilityState,
  });

  final String regime;
  final String h4Bias;
  final String m15Trigger;
  final double? mtfAgreement;
  final String volatilityState;

  static MarketContext fromJson(Map<String, dynamic> j) => MarketContext(
        regime: _asString(j['regime']) ?? 'UNKNOWN',
        h4Bias: _asString(j['h4_bias']) ?? 'UNKNOWN',
        m15Trigger: _asString(j['m15_trigger']) ?? 'UNKNOWN',
        mtfAgreement: _asDouble(j['mtf_agreement']),
        volatilityState: _asString(j['volatility_state']) ?? 'UNKNOWN',
      );
}

class Signal {
  const Signal({
    required this.direction,
    required this.horizon,
    required this.probability,
    required this.probabilityCalibrated,
    required this.score,
    required this.confidenceLo,
    required this.confidenceHi,
    required this.modelVersion,
    required this.featuresContributing,
  });

  final String direction;
  final String? horizon;
  final double? probability;
  final bool probabilityCalibrated;
  final double? score;
  final double? confidenceLo;
  final double? confidenceHi;
  final String? modelVersion;
  final List<String> featuresContributing;

  static Signal fromJson(Map<String, dynamic> j) => Signal(
        direction: _asString(j['direction']) ?? 'UNKNOWN',
        horizon: _asString(j['horizon']),
        probability: _asDouble(j['probability']),
        probabilityCalibrated: j['probability_calibrated'] == true,
        score: _asDouble(j['score']),
        confidenceLo: _asDouble(j['confidence_lo']),
        confidenceHi: _asDouble(j['confidence_hi']),
        modelVersion: _asString(j['model_version']),
        featuresContributing: (j['features_contributing'] is List)
            ? (j['features_contributing'] as List)
                .whereType<String>()
                .toList(growable: false)
            : const <String>[],
      );
}

class Levels {
  const Levels({
    required this.entry,
    required this.stopLoss,
    required this.takeProfit,
    required this.rewardRisk,
    required this.suggestedRiskPct,
    required this.slMethod,
    required this.tpMethod,
  });

  final double? entry;
  final double? stopLoss;
  final double? takeProfit;
  final double? rewardRisk;
  final double? suggestedRiskPct;
  final String? slMethod;
  final String? tpMethod;

  /// True when at least one actionable value level is present.
  bool get hasAnyValue =>
      entry != null || stopLoss != null || takeProfit != null;

  static Levels fromJson(Map<String, dynamic> j) => Levels(
        entry: _asDouble(j['entry']),
        stopLoss: _asDouble(j['stop_loss']),
        takeProfit: _asDouble(j['take_profit']),
        rewardRisk: _asDouble(j['reward_risk']),
        suggestedRiskPct: _asDouble(j['suggested_risk_pct']),
        slMethod: _asString(j['sl_method']),
        tpMethod: _asString(j['tp_method']),
      );
}

class AnalysisMeta {
  const AnalysisMeta({
    required this.coverageTier,
    required this.dataFreshnessSec,
    required this.degraded,
    required this.scoreIsProbability,
    required this.disclaimer,
  });

  final String coverageTier;
  final int? dataFreshnessSec;
  final bool degraded;
  final bool scoreIsProbability;
  final String? disclaimer;

  static AnalysisMeta fromJson(Map<String, dynamic> j) => AnalysisMeta(
        coverageTier: _asString(j['coverage_tier']) ?? 'unknown',
        dataFreshnessSec: _asInt(j['data_freshness_sec']),
        degraded: j['degraded'] == true,
        scoreIsProbability: j['score_is_probability'] == true,
        disclaimer: _asString(j['disclaimer']),
      );
}

class AnalysisLatest {
  const AnalysisLatest({
    required this.timestamp,
    required this.symbol,
    required this.context,
    required this.signal,
    required this.levels,
    required this.meta,
  });

  final String? timestamp;
  final String symbol;
  final MarketContext context;
  final Signal signal;
  final Levels levels;
  final AnalysisMeta meta;

  static AnalysisLatest fromJson(Map<String, dynamic> j) => AnalysisLatest(
        timestamp: _asString(j['timestamp']),
        symbol: _asString(j['symbol']) ?? 'XAUUSD',
        context: MarketContext.fromJson(
            (j['context'] as Map?)?.cast<String, dynamic>() ?? const {}),
        signal: Signal.fromJson(
            (j['signal'] as Map?)?.cast<String, dynamic>() ?? const {}),
        levels: Levels.fromJson(
            (j['levels'] as Map?)?.cast<String, dynamic>() ?? const {}),
        meta: AnalysisMeta.fromJson(
            (j['meta'] as Map?)?.cast<String, dynamic>() ?? const {}),
      );
}
