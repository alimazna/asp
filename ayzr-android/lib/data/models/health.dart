/// Health model for `GET /api/v1/health/v1` and `/api/v1/health`.
library;

int? _i(Object? v) => v is int ? v : (v is num ? v.toInt() : null);

class HealthV1 {
  const HealthV1({
    required this.status,
    required this.bridge,
    required this.version,
    required this.uptimeSec,
  });

  /// ok | degraded | offline
  final String status;

  /// ok | degraded | offline
  final String bridge;
  final String version;
  final int? uptimeSec;

  bool get isOk => status == 'ok';
  bool get isOffline => status == 'offline';

  static HealthV1 fromJson(Map<String, dynamic> j) => HealthV1(
        status: j['status'] is String ? j['status'] as String : 'offline',
        bridge: j['bridge'] is String ? j['bridge'] as String : 'offline',
        version: j['version'] is String ? j['version'] as String : 'v1',
        uptimeSec: _i(j['uptime_sec']),
      );
}
