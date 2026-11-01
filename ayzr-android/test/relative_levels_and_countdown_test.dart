import 'package:ayzr_app/core/strings_ar.dart';
import 'package:ayzr_app/data/models/news.dart';
import 'package:ayzr_app/state/app_state.dart';
import 'package:ayzr_app/ui/widgets/countdown.dart';
import 'package:ayzr_app/ui/widgets/upcoming_news_section.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

import 'test_helpers.dart';

/// Relative, price-free levels: when the model cannot know the absolute price
/// it states the expected move and the protective stop as DISTANCES in points.
/// The client must read them from `levels` and never invent a price.
void main() {
  group('NewsAnalysis relative levels', () {
    test('parses expected-move and stop-distance when the backend gives them',
        () {
      final a = NewsAnalysis.fromJson({
        'article_id': 'x',
        'status': 'ok',
        'direction': 'bullish',
        'levels': {
          'entry': null,
          'stop_loss': null,
          'take_profit': null,
          'expected_move_points': 300,
          'stop_distance_points': 120,
          'relative_levels_note_ar': 'قد يتحرك نحو 300 نقطة.',
        },
      });
      expect(a.entry, isNull); // no fabricated price
      expect(a.expectedMovePoints, 300);
      expect(a.stopDistancePoints, 120);
      expect(a.relativeLevelsNoteAr, isNotEmpty);
    });

    test('absent distances stay null, never a fabricated zero', () {
      final a = NewsAnalysis.fromJson({
        'article_id': 'x',
        'status': 'ok',
        'direction': 'no_trade',
        'levels': {'entry': null, 'stop_loss': null, 'take_profit': null},
      });
      expect(a.expectedMovePoints, isNull);
      expect(a.stopDistancePoints, isNull);
      expect(a.relativeLevelsNoteAr, isEmpty);
    });

    test('the Arabic point label names the unit, not a bare number', () {
      expect(Ar.pointsValue(300), contains('300'));
      expect(Ar.pointsValue(300), contains(Ar.pointsSuffix));
    });
  });

  group('AppState.nearestUpcomingEvent', () {
    test('selects the earliest scheduled event, not simply the first row',
        () async {
      // The list is deliberately out of order: the client must pick the minimum
      // itself rather than trusting the server's ordering.
      final state = await stateWith(routeTable({
        '/api/v1/events': (req) => enveloped({
              'events': [
                _eventRow('later', 1793174400),
                _eventRow('soonest', 1793170800),
                _eventRow('middle', 1793172000),
              ],
            }),
      }));
      await state.loadUpcomingEvents();
      expect(state.nearestUpcomingEvent!.event.eventKey, 'soonest');
    });

    test('an empty upcoming list yields null, not a fabricated event', () async {
      final state = await stateWith(routeTable({
        '/api/v1/events': (req) => enveloped({'events': <Object>[]}),
      }));
      await state.loadUpcomingEvents();
      expect(state.nearestUpcomingEvent, isNull);
      expect(state.upcomingEvents.phase, LoadPhase.empty);
    });

    test('an event with no scheduled time is skipped, not treated as zero',
        () async {
      final state = await stateWith(routeTable({
        '/api/v1/events': (req) => enveloped({
              'events': [
                _eventRow('no-time', null),
                _eventRow('timed', 1793170800),
              ],
            }),
      }));
      await state.loadUpcomingEvents();
      expect(state.nearestUpcomingEvent!.event.eventKey, 'timed');
    });

    test('a failed read is an error section, not a false empty', () async {
      final state = await stateWith(routeTable({
        '/api/v1/events': (req) => errorBody('boom', 'failed', 500),
      }));
      await state.loadUpcomingEvents();
      expect(state.upcomingEvents.phase, LoadPhase.error);
      expect(state.nearestUpcomingEvent, isNull);
    });
  });

  _upcomingSectionTests();

  group('EventCountdown formatting', () {
    testWidgets('a past target says "انقضى", a future one counts down',
        (tester) async {
      // Imported lazily so the widget test stays focused on the label contract.
      final past = DateTime.now()
          .toUtc()
          .subtract(const Duration(minutes: 5))
          .millisecondsSinceEpoch ~/
          1000;
      final future = DateTime.now()
          .toUtc()
          .add(const Duration(hours: 2, minutes: 30))
          .millisecondsSinceEpoch ~/
          1000;

      await tester.pumpWidget(_host(past));
      expect(find.textContaining(Ar.eventCountdownPassed), findsOneWidget);

      await tester.pumpWidget(_host(future));
      expect(find.textContaining(Ar.eventIn), findsOneWidget);
    });
  });
}

Widget _host(int? target) {
  return MaterialApp(
    home: Scaffold(
      body: EventCountdown(targetUtcSeconds: target),
    ),
  );
}

/// The dedicated preparation section: it must surface the nearest scheduled
/// release regardless of impact level, carry a countdown, and offer the
/// "تحضير للخبر القادم" button that opens the event's preparation view.
void _upcomingSectionTests() {
  testWidgets('shows the nearest low-impact event and its preparation button',
      (tester) async {
    // Timestamps are relative to now so the countdown is genuinely ahead
    // whatever the wall clock says when the suite runs.
    final now = DateTime.now().toUtc().millisecondsSinceEpoch ~/ 1000;
    final state = await stateWith(routeTable({
      '/api/v1/events': (req) => enveloped({
            'events': [
              _eventRow('far-high', now + 7200),
              _eventRow('near-low', now + 1800), // nearest, impact "low"
            ],
          }),
    }));
    await state.loadUpcomingEvents();

    await tester.pumpWidget(MaterialApp(
      home: Scaffold(body: UpcomingNewsSection(state: state)),
    ));
    await tester.pump();

    // The nearest event is the low-impact one, and it is NOT hidden.
    expect(find.text('Event near-low'), findsOneWidget);
    expect(find.text('Event far-high'), findsNothing);
    // The preparation button is present.
    expect(find.text(Ar.eventPrepareNext), findsOneWidget);
    // A countdown is shown (its label carries the "بعد" prefix).
    expect(find.textContaining(Ar.eventIn), findsOneWidget);
  });

  testWidgets('no upcoming event shows the honest empty message',
      (tester) async {
    final state = await stateWith(routeTable({
      '/api/v1/events': (req) => enveloped({'events': <Object>[]}),
    }));
    await state.loadUpcomingEvents();
    await tester.pumpWidget(MaterialApp(
      home: Scaffold(body: UpcomingNewsSection(state: state)),
    ));
    await tester.pump();
    expect(find.text(Ar.eventNoUpcoming), findsOneWidget);
    expect(find.text(Ar.eventPrepareNext), findsNothing);
  });
}

Map<String, dynamic> _eventRow(String key, int? scheduledAt) => {
      'event': {
        'event_key': key,
        'title': 'Event $key',
        'country': 'USD',
        'impact': 'low',
        'scheduled_at': scheduledAt,
        'scheduled_at_iso': scheduledAt == null
            ? null
            : DateTime.fromMillisecondsSinceEpoch(scheduledAt * 1000, isUtc: true)
                .toIso8601String(),
        'forecast': null,
        'previous': null,
        'source': 'faireconomy',
        'stage': 'scheduled',
        'gold_relevant': false,
        'first_seen': 1793100000,
        'updated_at': 1793100000,
        'prepared_at': null,
        'prepare_status': '',
        'released_at': null,
        'release_status': '',
      },
      'stage': 'scheduled',
      'has_expectation': false,
      'has_result': false,
    };
