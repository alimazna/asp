// Activity-log and AI-provider status models.
//
// These mirror GET /api/v1/activity and GET /api/v1/providers. The activity log
// is the honest record of what the backend actually did, so a screen built on it
// shows a real success or failure rather than an optimistic assumption.

/// One recorded backend operation.
class ActivityEntry {
  const ActivityEntry({
    required this.id,
    required this.at,
    required this.kind,
    required this.outcome,
    required this.reason,
    required this.source,
    required this.detail,
    required this.itemCount,
    required this.durationMs,
  });

  final int id;
  final DateTime? at;
  final String kind; // news_read | candle_read | llm_analyze | news_ingest | history_read
  final String outcome; // ok | empty | rejected | unavailable | error
  final String reason;
  final String source; // provider label, or "store" / "sender"
  final String detail;
  final int itemCount;
  final int durationMs; // -1 when not measured

  bool get isFailure => outcome == 'error' || outcome == 'unavailable';
  bool get isRejected => outcome == 'rejected';
  bool get hasDuration => durationMs >= 0;

  factory ActivityEntry.fromJson(Map<String, dynamic> j) {
    final raw = j['at'];
    return ActivityEntry(
      id: _int(j['id']),
      at: raw is int && raw > 0
          ? DateTime.fromMillisecondsSinceEpoch(raw * 1000, isUtc: true)
          : null,
      kind: _str(j['kind']),
      outcome: _str(j['outcome']),
      reason: _str(j['reason']),
      source: _str(j['source']),
      detail: _str(j['detail']),
      itemCount: _int(j['item_count']),
      durationMs: j['duration_ms'] is int ? j['duration_ms'] as int : -1,
    );
  }
}

/// Per-outcome tally over a rolling window.
class ActivityTally {
  const ActivityTally({
    required this.ok,
    required this.empty,
    required this.rejected,
    required this.unavailable,
    required this.error,
    required this.total,
  });

  final int ok;
  final int empty;
  final int rejected;
  final int unavailable;
  final int error;
  final int total;

  factory ActivityTally.fromJson(Map<String, dynamic> j) => ActivityTally(
        ok: _int(j['ok']),
        empty: _int(j['empty']),
        rejected: _int(j['rejected']),
        unavailable: _int(j['unavailable']),
        error: _int(j['error']),
        total: _int(j['total']),
      );
}

class ActivityPage {
  const ActivityPage({required this.tally, required this.entries});
  final ActivityTally tally;
  final List<ActivityEntry> entries;
}

/// One provider in the AI failover chain.
class ProviderStatus {
  const ProviderStatus({
    required this.provider,
    required this.attempts,
    required this.successes,
    required this.failures,
    required this.skippedAfterQuota,
    required this.lastOutcome,
  });

  final String provider;
  final int attempts;
  final int successes;
  final int failures;
  final int skippedAfterQuota;
  final String lastOutcome; // ok | quota_exhausted | auth_failure | ... | none

  bool get isWorking => successes > 0;
  bool get isUntried => attempts == 0;

  factory ProviderStatus.fromJson(Map<String, dynamic> j) => ProviderStatus(
        provider: _str(j['provider']),
        attempts: _int(j['attempts']),
        successes: _int(j['successes']),
        failures: _int(j['failures']),
        skippedAfterQuota: _int(j['skipped_after_quota']),
        lastOutcome: _str(j['last_outcome']),
      );
}

class ProviderPage {
  const ProviderPage({
    required this.activeProvider,
    required this.configured,
    required this.providers,
  });
  final String activeProvider;
  final int configured;
  final List<ProviderStatus> providers;
}

int _int(dynamic v) => v is int ? v : (v is num ? v.toInt() : 0);
String _str(dynamic v) => v is String ? v : '';
