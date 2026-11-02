/// Domain models for the AYZR Native AI Core surface (`/api/v1/ai/*`).
///
/// The AI service is a separate deployment from the ASTRA backend, so it has
/// its own base URL and its own token. A status other than `ok` means **no
/// answer may be shown**; the UI renders the reason instead.
///
/// The `answer_ar` field is only ever populated when the service actually
/// produced a validated Arabic answer grounded in retrieved data. There is no
/// client-side fallback text that could be mistaken for one.
library;

String _s(Object? v, [String fallback = '']) => v is String ? v : fallback;

int _i(Object? v) {
  if (v is int) return v;
  if (v is num) return v.toInt();
  if (v is String) return int.tryParse(v) ?? 0;
  return 0;
}

List<String> _strList(Object? v) => v is List
    ? v.whereType<String>().toList(growable: false)
    : const <String>[];

/// One piece of evidence the AI used, with its source and timestamp.
class AiEvidence {
  const AiEvidence({
    required this.kind,
    required this.labelAr,
    required this.source,
    required this.timestamp,
    required this.values,
  });

  final String kind;
  final String labelAr;
  final String source;
  final int timestamp;
  final Map<String, dynamic> values;

  factory AiEvidence.fromJson(Map<String, dynamic> j) => AiEvidence(
        kind: _s(j['kind']),
        labelAr: _s(j['label']),
        source: _s(j['source']),
        timestamp: _i(j['timestamp']),
        values: j['values'] is Map
            ? Map<String, dynamic>.from(j['values'] as Map)
            : const {},
      );
}

/// The AI service's answer. `status` is the gate: anything but `ok` carries a
/// reason and no answer text.
class AiAnswer {
  const AiAnswer({
    required this.status,
    required this.answerAr,
    required this.direction,
    required this.agent,
    required this.provider,
    required this.model,
    required this.evidence,
    required this.sources,
    required this.warningsAr,
    required this.reasonAr,
    required this.latencyMs,
  });

  /// ok | insufficient_data | unavailable | invalid_output
  final String status;
  final String answerAr;

  /// bullish | bearish | sideways | no_trade, or empty when not applicable.
  final String direction;
  final String agent;
  final String provider;
  final String model;
  final List<AiEvidence> evidence;
  final List<String> sources;
  final List<String> warningsAr;
  final String reasonAr;
  final int latencyMs;

  bool get isOk => status == 'ok' && answerAr.trim().isNotEmpty;
  bool get isInsufficient => status == 'insufficient_data';
  bool get isUnavailable => status == 'unavailable';
  bool get isInvalid => status == 'invalid_output';

  factory AiAnswer.fromJson(Map<String, dynamic> j) => AiAnswer(
        status: _s(j['status'], 'unavailable'),
        answerAr: _s(j['answer_ar']),
        direction: _s(j['direction']),
        agent: _s(j['agent']),
        provider: _s(j['provider']),
        model: _s(j['model']),
        evidence: j['evidence'] is List
            ? (j['evidence'] as List)
                .whereType<Map>()
                .map((e) => AiEvidence.fromJson(Map<String, dynamic>.from(e)))
                .toList(growable: false)
            : const [],
        sources: _strList(j['sources']),
        warningsAr: _strList(j['warnings_ar']),
        reasonAr: _s(j['reason_ar']),
        latencyMs: _i(j['latency_ms']),
      );
}

/// Health of the AI service, used to show an honest availability banner.
class AiHealth {
  const AiHealth({
    required this.ready,
    required this.provider,
    required this.model,
    required this.reachable,
    required this.startupError,
  });

  final bool ready;
  final String provider;
  final String model;
  final bool reachable;
  final String startupError;

  factory AiHealth.fromJson(Map<String, dynamic> j) {
    final ph = j['provider_health'] is Map
        ? Map<String, dynamic>.from(j['provider_health'] as Map)
        : const <String, dynamic>{};
    return AiHealth(
      ready: j['ready'] == true,
      provider: _s(ph['provider']),
      model: _s(ph['model']),
      reachable: ph['reachable'] == true,
      startupError: _s(j['startup_error']),
    );
  }
}
