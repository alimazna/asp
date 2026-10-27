import 'package:ayzr_app/data/api_result.dart';
import 'package:ayzr_app/state/app_state.dart';
import 'package:flutter_test/flutter_test.dart';

import 'test_helpers.dart';

void main() {
  group('AppState section transitions', () {
    test('loadHealth: idle -> ready on success', () async {
      final state = await stateWith(routeTable({
        '/api/v1/health/v1': (_) => enveloped(healthOk),
      }));
      expect(state.health.phase, LoadPhase.idle);
      await state.loadHealth();
      expect(state.health.phase, LoadPhase.ready);
      expect(state.health.data!.status, 'ok');
      expect(state.isOffline, isFalse);
    });

    test('loadHealth: error on network failure, isOffline true', () async {
      final state = await stateWith(
          (_) async => throw Exception('boom'));
      await state.loadHealth();
      expect(state.health.phase, LoadPhase.error);
      expect(state.isOffline, isTrue);
    });

    test('loadNews: empty list becomes the empty phase, not an error',
        () async {
      final state = await stateWith(routeTable({
        '/api/v1/news': (_) => enveloped(<Object?>[]),
        '/api/v1/news/quota': (_) => enveloped({'quota': {'category': 'ok'}}),
      }));
      await state.loadNews();
      expect(state.news.phase, LoadPhase.empty);
      expect(state.news.failure, isNull);
    });

    test('loadNews surfaces a spent quota and raises aiQuotaExhausted',
        () async {
      final state = await stateWith(routeTable({
        '/api/v1/news': (_) => enveloped(<Object?>[]),
        '/api/v1/news/quota': (_) => enveloped({
              'quota': {
                'category': 'quota_exhausted',
                'retry_after_seconds': null,
                'quota_exhausted': true,
                'rate_limited': false,
                'cooling_down': true,
              }
            }),
      }));
      await state.loadNews();
      expect(state.aiQuotaExhausted, isTrue);
      expect(state.aiRateLimited, isFalse);
      expect(state.quota.data!.retryAfterSeconds, isNull);
    });

    test('loadNews surfaces a temporary rate limit without quota exhaustion',
        () async {
      final state = await stateWith(routeTable({
        '/api/v1/news': (_) => enveloped(<Object?>[]),
        '/api/v1/news/quota': (_) => enveloped({
              'quota': {
                'category': 'rate_limited',
                'rate_limited': true,
                'quota_exhausted': false,
              }
            }),
      }));
      await state.loadNews();
      expect(state.aiRateLimited, isTrue);
      expect(state.aiQuotaExhausted, isFalse);
    });

    test('a failed quota read does not raise a false exhausted banner',
        () async {
      final state = await stateWith(routeTable({
        '/api/v1/news': (_) => enveloped(<Object?>[]),
        '/api/v1/news/quota': (_) => errorBody('server_error', 'boom', 500),
      }));
      await state.loadNews();
      expect(state.aiQuotaExhausted, isFalse);
      expect(state.aiRateLimited, isFalse);
    });

    test('loadNews: unauthorized maps to error with unauthorized failure',
        () async {
      final state = await stateWith(routeTable({
        '/api/v1/news': (_) =>
            errorBody('unauthorized', 'token required', 401),
      }));
      await state.loadNews();
      expect(state.news.phase, LoadPhase.error);
      expect(state.news.failure!.kind, ApiFailureKind.unauthorized);
    });

    test('isStale is true only when freshness exceeds the threshold',
        () async {
      final state = await stateWith(routeTable({
        '/api/v1/analysis/latest': (_) => enveloped(analysisCalibrated),
      }));
      await state.loadAnalysis();
      // freshness 120s < 3600s
      expect(state.isStale, isFalse);
    });

    test('isStale false when freshness is unknown (null), never assumed fresh',
        () async {
      final state = await stateWith(routeTable({
        '/api/v1/analysis/latest': (_) => enveloped(analysisUncalibrated),
      }));
      await state.loadAnalysis();
      expect(state.isStale, isFalse);
      expect(state.analysis.data!.meta.dataFreshnessSec, isNull);
    });

    test('isDegraded true when health is neither ok nor offline', () async {
      final state = await stateWith(routeTable({
        '/api/v1/health/v1': (_) => enveloped({
              'status': 'degraded',
              'bridge': 'offline',
              'version': 'v1',
              'uptime_sec': null,
            }),
      }));
      await state.loadHealth();
      expect(state.isDegraded, isTrue);
    });
  });
}
