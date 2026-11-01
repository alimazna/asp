/// Domain models for the AYZR macro-event surface (`/api/v1/events*`).
///
/// A scheduled event carries a consensus published by a real calendar, and two
/// independent analysis turns: an EXPECTATION written before release and a
/// RESULT written after. `hasExpectation`/`hasResult` are distinct from an
/// analysis whose own `status` is not `ok` — "the turn has not run yet" must
/// never be shown as "ran and found nothing".
library;

int? _i(Object? v) {
  if (v is int) return v;
  if (v is num) return v.toInt();
  if (v is String) return int.tryParse(v);
  return null;
}

String _s(Object? v, [String fallback = '']) => v is String ? v : fallback;

/// One scheduled macro event (`event` object inside `/api/v1/events`).
///
/// `forecast`/`previous` are null when the feed published none, never `"0"`.
class ScheduledEvent {
  const ScheduledEvent({
    required this.eventKey,
    required this.title,
    required this.country,
    required this.impact,
    required this.scheduledAtUtc,
    required this.scheduledAtIso,
    required this.forecast,
    required this.previous,
    required this.source,
    required this.stage,
    required this.goldRelevant,
    required this.firstSeenUtc,
    required this.updatedAtUtc,
    required this.preparedAtUtc,
    required this.prepareStatus,
    required this.releasedAtUtc,
    required this.releaseStatus,
  });

  final String eventKey;
  final String title;
  final String country;
  final String impact;
  final int? scheduledAtUtc;
  final String? scheduledAtIso;
  final String? forecast;
  final String? previous;
  final String source;
  final String stage;
  final bool goldRelevant;
  final int? firstSeenUtc;
  final int? updatedAtUtc;
  final int? preparedAtUtc;
  final String prepareStatus;
  final int? releasedAtUtc;
  final String releaseStatus;

  bool get isReleased => stage == 'released';

  static ScheduledEvent fromJson(Map<String, dynamic> j) => ScheduledEvent(
        eventKey: _s(j['event_key']),
        title: _s(j['title']),
        country: _s(j['country']),
        impact: _s(j['impact'], 'unknown'),
        scheduledAtUtc: _i(j['scheduled_at']),
        scheduledAtIso:
            j['scheduled_at_iso'] is String ? j['scheduled_at_iso'] as String : null,
        forecast: j['forecast'] is String ? j['forecast'] as String : null,
        previous: j['previous'] is String ? j['previous'] as String : null,
        source: _s(j['source']),
        stage: _s(j['stage'], 'scheduled'),
        goldRelevant: j['gold_relevant'] == true,
        firstSeenUtc: _i(j['first_seen']),
        updatedAtUtc: _i(j['updated_at']),
        preparedAtUtc: _i(j['prepared_at']),
        prepareStatus: _s(j['prepare_status']),
        releasedAtUtc: _i(j['released_at']),
        releaseStatus: _s(j['release_status']),
      );
}

/// One row of `GET /api/v1/events`.
class EventListItem {
  const EventListItem({
    required this.event,
    required this.stage,
    required this.hasExpectation,
    required this.hasResult,
  });

  final ScheduledEvent event;
  final String stage;
  final bool hasExpectation;
  final bool hasResult;

  static EventListItem fromJson(Map<String, dynamic> j) => EventListItem(
        event: ScheduledEvent.fromJson(
            (j['event'] as Map?)?.cast<String, dynamic>() ?? const {}),
        stage: _s(j['stage'], 'scheduled'),
        hasExpectation: j['has_expectation'] == true,
        hasResult: j['has_result'] == true,
      );
}

/// The full payload of `GET /api/v1/events/detail?key=`.
///
/// Both `expectation` and `result` are independently optional. A null turn means
/// "not run yet"; a present turn carries its own `status` gate.
class EventDetail {
  const EventDetail({
    required this.event,
    required this.hasExpectation,
    required this.expectationJson,
    required this.hasResult,
    required this.resultJson,
  });

  final ScheduledEvent event;
  final bool hasExpectation;
  final Map<String, dynamic>? expectationJson;
  final bool hasResult;
  final Map<String, dynamic>? resultJson;

  static EventDetail fromJson(Map<String, dynamic> j) => EventDetail(
        event: ScheduledEvent.fromJson(
            (j['event'] as Map?)?.cast<String, dynamic>() ?? const {}),
        hasExpectation: j['has_expectation'] == true,
        expectationJson: (j['expectation'] is Map)
            ? (j['expectation'] as Map).cast<String, dynamic>()
            : null,
        hasResult: j['has_result'] == true,
        resultJson: (j['result'] is Map)
            ? (j['result'] as Map).cast<String, dynamic>()
            : null,
      );
}
