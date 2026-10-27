import 'package:ayzr_app/state/app_state.dart';
import 'package:flutter_test/flutter_test.dart';

import 'test_helpers.dart';

void main() {
  group('activity() parses the envelope', () {
    test('reads tally and entries, keeping outcome and duration', () async {
      final client = fakeClient(routeTable({
        '/api/v1/activity': (_) => enveloped({
              'tally': {
                'ok': 5,
                'empty': 1,
                'rejected': 2,
                'unavailable': 1,
                'error': 1,
                'total': 10,
              },
              'entries': [
                {
                  'id': 42,
                  'at': 1729000000,
                  'kind': 'llm_analyze',
                  'outcome': 'ok',
                  'reason': '',
                  'source': 'gemini',
                  'detail': 'تحليل إخباري',
                  'item_count': 1,
                  'duration_ms': 1520,
                },
                {
                  'id': 41,
                  'at': 1728999000,
                  'kind': 'candle_read',
                  'outcome': 'empty',
                  'reason': 'no rows',
                  'source': 'store',
                  'detail': '',
                  'item_count': 0,
                  'duration_ms': -1,
                },
              ],
            }),
      }));
      final r = await client.activity();
      expect(r.isSuccess, isTrue);
      final page = r.value!;
      expect(page.tally.total, 10);
      expect(page.tally.rejected, 2);
      expect(page.entries.length, 2);

      final first = page.entries.first;
      expect(first.kind, 'llm_analyze');
      expect(first.outcome, 'ok');
      expect(first.isFailure, isFalse);
      expect(first.hasDuration, isTrue);
      expect(first.durationMs, 1520);
      expect(first.at, isNotNull);

      final second = page.entries.last;
      expect(second.outcome, 'empty');
      expect(second.hasDuration, isFalse, reason: '-1 means not measured');
      expect(second.reason, 'no rows');
    });

    test('a missing tally becomes a zero tally, not a crash', () async {
      final client = fakeClient(routeTable({
        '/api/v1/activity': (_) => enveloped({'entries': <Object>[]}),
      }));
      final r = await client.activity();
      expect(r.isSuccess, isTrue);
      expect(r.value!.tally.total, 0);
      expect(r.value!.entries, isEmpty);
    });

    test('a transport failure is surfaced, not swallowed', () async {
      final client = fakeClient(routeTable({
        '/api/v1/activity': (_) => errorBody('unavailable', 'offline', 503),
      }));
      final r = await client.activity();
      expect(r.isSuccess, isFalse);
    });
  });

  group('providers() parses the chain', () {
    test('reads the active provider and per-provider counters', () async {
      final client = fakeClient(routeTable({
        '/api/v1/providers': (_) => enveloped({
              'active_provider': 'gemini',
              'configured': 4,
              'providers': [
                {
                  'provider': 'llm7',
                  'attempts': 3,
                  'successes': 0,
                  'failures': 3,
                  'skipped_after_quota': 0,
                  'last_outcome': 'quota_exhausted',
                },
                {
                  'provider': 'gemini',
                  'attempts': 2,
                  'successes': 2,
                  'failures': 0,
                  'skipped_after_quota': 0,
                  'last_outcome': 'ok',
                },
                {
                  'provider': 'zai',
                  'attempts': 0,
                  'successes': 0,
                  'failures': 0,
                  'skipped_after_quota': 0,
                  'last_outcome': 'none',
                },
              ],
            }),
      }));
      final r = await client.providers();
      expect(r.isSuccess, isTrue);
      final page = r.value!;
      expect(page.activeProvider, 'gemini');
      expect(page.configured, 4);
      expect(page.providers.length, 3);
      expect(page.providers[0].isWorking, isFalse);
      expect(page.providers[1].isWorking, isTrue);
      expect(page.providers[2].isUntried, isTrue);
    });
  });

  group('AppState.loadActivity', () {
    test('an empty log is an empty section, not an error', () async {
      final state = await stateWith(routeTable({
        '/api/v1/activity': (_) => enveloped({'entries': <Object>[]}),
      }));
      await state.loadActivity();
      expect(state.activity.phase, LoadPhase.empty);
    });

    test('a failure is an error section that can be retried', () async {
      final state = await stateWith(routeTable({
        '/api/v1/activity': (_) => errorBody('unavailable', 'offline', 503),
      }));
      await state.loadActivity();
      expect(state.activity.phase, LoadPhase.error);
    });

    test('a populated log is ready', () async {
      final state = await stateWith(routeTable({
        '/api/v1/activity': (_) => enveloped({
              'tally': {'ok': 1, 'total': 1},
              'entries': [
                {
                  'id': 1,
                  'at': 1729000000,
                  'kind': 'news_read',
                  'outcome': 'ok',
                  'source': 'store',
                  'detail': 'قائمة الأخبار',
                  'item_count': 7,
                  'duration_ms': 12,
                },
              ],
            }),
      }));
      await state.loadActivity();
      expect(state.activity.phase, LoadPhase.ready);
      expect(state.activity.data!.entries.single.itemCount, 7);
    });
  });
}
