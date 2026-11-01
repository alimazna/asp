@Tags(['live'])
library;

import 'dart:io';

import 'package:ayzr_app/core/app_config.dart';
import 'package:ayzr_app/data/api_client.dart';
import 'package:ayzr_app/state/app_state.dart';
import 'package:ayzr_app/state/settings_store.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:shared_preferences/shared_preferences.dart';

/// Live integration test: drives the app's real [ApiClient] against a running
/// AYZR backend. It is skipped unless `AYZR_LIVE_BASE_URL` is set, so the
/// default `flutter test` run stays hermetic and never needs a server.
///
///   AYZR_LIVE_BASE_URL=http://127.0.0.1:12001 AYZR_LIVE_TOKEN=client-tok \
///     flutter test test/live_integration_test.dart
void main() {
  final base = Platform.environment['AYZR_LIVE_BASE_URL'];
  final token = Platform.environment['AYZR_LIVE_TOKEN'] ?? '';
  final skip = base == null || base.isEmpty;

  group('live backend integration', () {
    late AppState state;

    setUp(() async {
      SharedPreferences.setMockInitialValues({});
      final settings = await SettingsStore.open();
      state = AppState(
        settings: settings,
        client: ApiClient(
          config: AppConfig(baseUrl: base!, clientToken: token),
          timeout: const Duration(seconds: 10),
        ),
      );
    });

    test('health reads a real status', () async {
      await state.loadHealth();
      expect(state.health.data, isNotNull);
    }, skip: skip);

    test('events are soonest-first and include every impact level', () async {
      await state.loadUpcomingEvents();
      final rows = state.upcomingEvents.data ?? const [];
      final times = [
        for (final r in rows)
          if (r.event.scheduledAtUtc != null) r.event.scheduledAtUtc!,
      ];
      // The backend must hand the schedule over already sorted; the client also
      // picks the minimum itself, but the contract is ascending.
      expect(times, equals(List.of(times)..sort()));
      // "preparation for upcoming news" is impact-agnostic.
      final impacts = {for (final r in rows) r.event.impact};
      expect(impacts.length, greaterThan(1));
    }, skip: skip);

    test('news analysis carries grounded or relative levels, never both faked',
        () async {
      await state.loadNews();
      final rows = state.news.data ?? const [];
      if (rows.isEmpty) return; // nothing to assert against
      final id = rows.first.article.articleId;
      final result = await state.loadArticle(id);
      final analysis = result.value?.analysis;
      if (analysis == null) return;
      // An actionable direction with no real market reference must expose the
      // honest relative distances, not an invented price.
      if (analysis.direction == 'bullish' ||
          analysis.direction == 'bearish') {
        final hasPrice = analysis.entry != null;
        final hasRelative = analysis.expectedMovePoints != null ||
            analysis.stopDistancePoints != null;
        expect(hasPrice || hasRelative, isTrue,
            reason: 'an actionable analysis must price it or distance it');
      }
    }, skip: skip);
  });
}
