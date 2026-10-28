import 'package:ayzr_app/data/models/events.dart';
import 'package:flutter_test/flutter_test.dart';

import 'test_helpers.dart';

void main() {
  group('ScheduledEvent.fromJson', () {
    test('parses a full event and keeps a published consensus', () {
      final e = ScheduledEvent.fromJson({
        'event_key': 'k1',
        'title': 'US CPI m/m',
        'country': 'USD',
        'impact': 'high',
        'scheduled_at': 1793104200,
        'scheduled_at_iso': '2026-10-27T12:30:00Z',
        'forecast': '0.3%',
        'previous': '0.2%',
        'source': 'faireconomy',
        'stage': 'released',
        'gold_relevant': true,
        'first_seen': 1793000000,
        'updated_at': 1793104500,
        'prepared_at': 1793102400,
        'prepare_status': 'ok',
        'released_at': 1793104500,
        'release_status': 'ok',
      });
      expect(e.eventKey, 'k1');
      expect(e.forecast, '0.3%');
      expect(e.previous, '0.2%');
      expect(e.goldRelevant, isTrue);
      expect(e.isReleased, isTrue);
      expect(e.preparedAtUtc, 1793102400);
    });

    test('an unpublished consensus stays null, never "0" or ""', () {
      final e = ScheduledEvent.fromJson({
        'event_key': 'k2',
        'title': 'US FOMC Statement',
        'country': 'USD',
        'impact': 'high',
        'scheduled_at': 1793174400,
        'scheduled_at_iso': '2026-10-28T08:00:00Z',
        'forecast': null,
        'previous': null,
        'source': 'faireconomy',
        'stage': 'scheduled',
        'gold_relevant': true,
        'first_seen': 1793100000,
        'updated_at': 1793100000,
        'prepared_at': null,
        'prepare_status': '',
        'released_at': null,
        'release_status': '',
      });
      expect(e.forecast, isNull);
      expect(e.previous, isNull);
      expect(e.preparedAtUtc, isNull);
      expect(e.releasedAtUtc, isNull);
    });

    test('a missing impact falls back to the neutral unknown', () {
      final e = ScheduledEvent.fromJson({'event_key': 'k3', 'title': 'x'});
      expect(e.impact, 'unknown');
      expect(e.stage, 'scheduled');
      expect(e.goldRelevant, isFalse);
    });
  });

  group('EventListItem.fromJson', () {
    test('distinguishes has_expectation from has_result', () {
      final raw = (eventsUpcoming['events']! as List).first
          as Map<String, dynamic>;
      final item = EventListItem.fromJson(raw);
      expect(item.hasExpectation, isTrue);
      expect(item.hasResult, isFalse);
      expect(item.stage, 'prepared');
    });
  });

  group('EventDetail.fromJson', () {
    test('exposes both turns independently', () {
      final d = EventDetail.fromJson(eventDetail);
      expect(d.hasExpectation, isTrue);
      expect(d.expectationJson, isNotNull);
      expect(d.hasResult, isTrue);
      expect(d.resultJson, isNotNull);
    });

    test('a not-yet-run turn is null, distinct from a failed analysis', () {
      final d = EventDetail.fromJson({
        'event': {'event_key': 'k', 'title': 't'},
        'has_expectation': false,
        'expectation': null,
        'has_result': false,
        'result': null,
      });
      expect(d.hasExpectation, isFalse);
      expect(d.expectationJson, isNull);
      expect(d.hasResult, isFalse);
      expect(d.resultJson, isNull);
    });
  });

  group('events endpoint', () {
    test('unwraps {events:[...]} and parses every row', () async {
      final state = await stateWith(routeTable({
        '/api/v1/events': (_) => enveloped(eventsUpcoming),
      }));
      final r = await state.client.events();
      expect(r.isSuccess, isTrue);
      expect(r.value!.length, 2);
      expect(r.value!.first.event.title, 'US Non-Farm Payrolls');
    });

    test('sends the gold_only and scope query parameters', () async {
      late Uri seen;
      final state = await stateWith(routeTable({
        '/api/v1/events': (req) {
          seen = req.url;
          return enveloped(eventsUpcoming);
        },
      }));
      await state.client.events(scope: 'past', goldOnly: false, limit: 10);
      expect(seen.queryParameters['scope'], 'past');
      expect(seen.queryParameters['gold_only'], 'false');
      expect(seen.queryParameters['limit'], '10');
    });

    test('an empty list is a successful empty read, not an error', () async {
      final state = await stateWith(routeTable({
        '/api/v1/events': (_) => enveloped({'events': <Object>[]}),
      }));
      final r = await state.client.events();
      expect(r.isSuccess, isTrue);
      expect(r.value, isEmpty);
    });
  });

  group('AppState.loadEvents', () {
    test('an empty feed becomes the empty phase, not ready', () async {
      final state = await stateWith(routeTable({
        '/api/v1/events': (_) => enveloped({'events': <Object>[]}),
      }));
      await state.loadEvents();
      expect(state.events.phase.name, 'empty');
    });

    test('a backend failure becomes the error phase', () async {
      final state = await stateWith(routeTable({
        '/api/v1/events': (_) =>
            errorBody('dependency_unavailable', 'store down', 503),
      }));
      await state.loadEvents();
      expect(state.events.phase.name, 'error');
      expect(state.events.failure, isNotNull);
    });

    test('scope is remembered across a reload', () async {
      final state = await stateWith(routeTable({
        '/api/v1/events': (_) => enveloped(eventsUpcoming),
      }));
      await state.loadEvents(scope: 'past');
      expect(state.eventScope, 'past');
      await state.loadEvents();
      expect(state.eventScope, 'past');
    });
  });

  group('AppState.loadEventDetail', () {
    test('returns both turns for a released event', () async {
      final state = await stateWith(routeTable({
        '/api/v1/events/detail': (_) => enveloped(eventDetail),
      }));
      final r = await state.loadEventDetail('faireconomy|USD|US CPI m/m|1793104200');
      expect(r.isSuccess, isTrue);
      expect(r.value!.hasExpectation, isTrue);
      expect(r.value!.hasResult, isTrue);
    });

    test('a missing event surfaces the not_found failure', () async {
      final state = await stateWith(routeTable({
        '/api/v1/events/detail': (_) =>
            errorBody('not_found', 'no stored event', 404),
      }));
      final r = await state.loadEventDetail('missing');
      expect(r.isSuccess, isFalse);
      expect(r.failure!.statusCode, 404);
    });
  });
}
