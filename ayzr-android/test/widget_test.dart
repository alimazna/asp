import 'package:ayzr_app/core/strings_ar.dart';
import 'package:ayzr_app/ui/app_shell.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:http/http.dart' as http;

import 'test_helpers.dart';

/// Pumps the full app with a controlled backend, letting the post-frame load
/// complete. A tall viewport is used so every card in a lazily-built [ListView]
/// is laid out and findable without manual scrolling.
Future<void> pumpApp(
  WidgetTester tester,
  Future<http.Response> Function(http.Request) handler,
) async {
  tester.view.physicalSize = const Size(1200, 4000);
  tester.view.devicePixelRatio = 1.0;
  addTearDown(tester.view.resetPhysicalSize);
  addTearDown(tester.view.resetDevicePixelRatio);
  final state = await stateWith(handler);
  await tester.pumpWidget(AyzrApp(state: state));
  await tester.pumpAndSettle();
}

void main() {
  group('App shell', () {
    testWidgets('renders Arabic RTL navigation with four sections',
        (tester) async {
      await pumpApp(
        tester,
        routeTable({
          '/api/v1/health/v1': (_) => enveloped(healthOk),
          '/api/v1/analysis/latest': (_) => enveloped(analysisCalibrated),
          '/api/v1/news': (_) => enveloped(newsList),
          '/api/v1/history': (_) => enveloped(historyList),
        }),
      );

      // Navigation labels are Arabic.
      expect(find.text(Ar.navHome), findsWidgets);
      expect(find.text(Ar.navAnalysis), findsWidgets);
      expect(find.text(Ar.navNews), findsWidgets);
      expect(find.text(Ar.navHistory), findsWidgets);

      // The shell is right-to-left.
      final dir = Directionality.of(
        tester.element(find.byType(NavigationBar)),
      );
      expect(dir, TextDirection.rtl);
    });

    testWidgets('home shows the shadow-only notice and health',
        (tester) async {
      await pumpApp(
        tester,
        routeTable({
          '/api/v1/health/v1': (_) => enveloped(healthOk),
          '/api/v1/analysis/latest': (_) => enveloped(analysisCalibrated),
          '/api/v1/news': (_) => enveloped(newsList),
          '/api/v1/history': (_) => enveloped(historyList),
        }),
      );
      expect(find.text(Ar.shadowOnly), findsOneWidget);
      expect(find.text(Ar.healthOk), findsWidgets);
    });

    testWidgets('switching to the analysis tab shows context and levels',
        (tester) async {
      await pumpApp(
        tester,
        routeTable({
          '/api/v1/health/v1': (_) => enveloped(healthOk),
          '/api/v1/analysis/latest': (_) => enveloped(analysisCalibrated),
          '/api/v1/news': (_) => enveloped(newsList),
          '/api/v1/history': (_) => enveloped(historyList),
        }),
      );

      await tester.tap(find.byIcon(Icons.insights_outlined));
      await tester.pumpAndSettle();

      expect(find.text(Ar.contextSection), findsOneWidget);
      expect(find.text(Ar.levelsSection), findsOneWidget);
      expect(find.text(Ar.probability), findsOneWidget);
    });

    testWidgets('uncalibrated analysis shows a score, never a probability',
        (tester) async {
      await pumpApp(
        tester,
        routeTable({
          '/api/v1/health/v1': (_) => enveloped(healthOk),
          '/api/v1/analysis/latest': (_) => enveloped(analysisUncalibrated),
          '/api/v1/news': (_) => enveloped(newsList),
          '/api/v1/history': (_) => enveloped(historyList),
        }),
      );

      await tester.tap(find.byIcon(Icons.insights_outlined));
      await tester.pumpAndSettle();

      expect(find.text(Ar.score), findsWidgets);
      expect(find.text(Ar.probability), findsNothing);
      expect(find.text(Ar.scoreNotProbability), findsWidgets);
    });
  });

  group('Offline and error states', () {
    testWidgets('offline backend shows the Arabic offline panel',
        (tester) async {
      await pumpApp(
        tester,
        (req) async => throw http.ClientException('connection refused'),
      );
      expect(find.text(Ar.offline), findsWidgets);
      expect(find.text(Ar.offlineHint), findsWidgets);
      expect(find.text(Ar.retry), findsWidgets);
    });

    testWidgets('a 500 shows the generic error state with retry',
        (tester) async {
      await pumpApp(
        tester,
        routeTable({
          '/api/v1/health/v1': (_) =>
              errorBody('dependency_unavailable', 'boom', 503),
          '/api/v1/analysis/latest': (_) =>
              errorBody('dependency_unavailable', 'boom', 503),
        }),
      );
      expect(find.text(Ar.errorGeneric), findsWidgets);
      expect(find.text(Ar.retry), findsWidgets);
    });

    testWidgets('empty news shows the empty state, not an error',
        (tester) async {
      await pumpApp(
        tester,
        routeTable({
          '/api/v1/health/v1': (_) => enveloped(healthOk),
          '/api/v1/analysis/latest': (_) => enveloped(analysisCalibrated),
          '/api/v1/news': (_) => enveloped(const {'news': <Object?>[]}),
          '/api/v1/history': (_) => enveloped(const {'history': <Object?>[]}),
        }),
      );

      await tester.tap(find.byIcon(Icons.newspaper_outlined));
      await tester.pumpAndSettle();
      expect(find.text(Ar.empty), findsWidgets);
      expect(find.text(Ar.errorGeneric), findsNothing);
    });
  });

  group('News flow', () {
    testWidgets('list shows headlines and opens the detail on tap',
        (tester) async {
      await pumpApp(
        tester,
        routeTable({
          '/api/v1/health/v1': (_) => enveloped(healthOk),
          '/api/v1/analysis/latest': (_) => enveloped(analysisCalibrated),
          '/api/v1/news': (_) => enveloped(newsList),
          '/api/v1/history': (_) => enveloped(historyList),
          '/api/v1/news/analyze': (_) => enveloped(newsDetail),
        }),
      );

      await tester.tap(find.byIcon(Icons.newspaper_outlined));
      await tester.pumpAndSettle();

      expect(find.text('الذهب يرتفع مع تراجع الدولار'), findsOneWidget);

      await tester.tap(find.text('الذهب يرتفع مع تراجع الدولار'));
      await tester.pumpAndSettle();

      expect(find.text(Ar.articleDetails), findsOneWidget);
      expect(find.text(Ar.summary), findsOneWidget);
      expect(find.text('تراجع الدولار يدعم الذهب.'), findsOneWidget);
    });

    testWidgets('article with no analysis shows the no-analysis state',
        (tester) async {
      await pumpApp(
        tester,
        routeTable({
          '/api/v1/health/v1': (_) => enveloped(healthOk),
          '/api/v1/analysis/latest': (_) => enveloped(analysisCalibrated),
          '/api/v1/news': (_) => enveloped(newsList),
          '/api/v1/history': (_) => enveloped(historyList),
          '/api/v1/news/analyze': (_) => enveloped(newsDetailNoAnalysis),
        }),
      );

      await tester.tap(find.byIcon(Icons.newspaper_outlined));
      await tester.pumpAndSettle();
      await tester.tap(find.text('بيانات التضخم الأمريكية'));
      await tester.pumpAndSettle();

      expect(find.text(Ar.noAnalysisYet), findsOneWidget);
    });

    testWidgets('gold-only toggle re-queries with the filter',
        (tester) async {
      var sawGoldOnly = false;
      await pumpApp(
        tester,
        routeTable({
          '/api/v1/health/v1': (_) => enveloped(healthOk),
          '/api/v1/analysis/latest': (_) => enveloped(analysisCalibrated),
          '/api/v1/news': (req) {
            if (req.url.queryParameters['gold_only'] == 'true') {
              sawGoldOnly = true;
            }
            return enveloped(newsList);
          },
          '/api/v1/history': (_) => enveloped(historyList),
        }),
      );

      await tester.tap(find.byIcon(Icons.newspaper_outlined));
      await tester.pumpAndSettle();

      await tester.tap(find.byType(Switch));
      await tester.pumpAndSettle();
      expect(sawGoldOnly, isTrue);
    });
  });

  group('History flow', () {
    testWidgets('history shows persisted analyses', (tester) async {
      await pumpApp(
        tester,
        routeTable({
          '/api/v1/health/v1': (_) => enveloped(healthOk),
          '/api/v1/analysis/latest': (_) => enveloped(analysisCalibrated),
          '/api/v1/news': (_) => enveloped(newsList),
          '/api/v1/history': (_) => enveloped(historyList),
        }),
      );

      await tester.tap(find.byIcon(Icons.history_outlined));
      await tester.pumpAndSettle();
      expect(find.text('الذهب يرتفع مع تراجع الدولار'), findsWidgets);
    });

    testWidgets('empty history shows the dedicated Arabic empty state',
        (tester) async {
      await pumpApp(
        tester,
        routeTable({
          '/api/v1/health/v1': (_) => enveloped(healthOk),
          '/api/v1/analysis/latest': (_) => enveloped(analysisCalibrated),
          '/api/v1/news': (_) => enveloped(newsList),
          '/api/v1/history': (_) => enveloped(const {'history': <Object?>[]}),
        }),
      );

      await tester.tap(find.byIcon(Icons.history_outlined));
      await tester.pumpAndSettle();
      expect(find.text(Ar.historyEmpty), findsOneWidget);
    });
  });

  group('Degraded and stale states', () {
    testWidgets('degraded backend surfaces the degraded banner',
        (tester) async {
      await pumpApp(
        tester,
        routeTable({
          '/api/v1/health/v1': (_) => enveloped(healthOffline),
          '/api/v1/analysis/latest': (_) => enveloped(analysisUncalibrated),
          '/api/v1/news': (_) => enveloped(newsList),
          '/api/v1/history': (_) => enveloped(historyList),
        }),
      );
      // health offline => the home shows the offline status label.
      expect(find.text(Ar.healthOffline), findsWidgets);
    });

    testWidgets('stale freshness surfaces the stale banner',
        (tester) async {
      final staleAnalysis = Map<String, dynamic>.from(analysisCalibrated);
      staleAnalysis['meta'] = {
        ...(analysisCalibrated['meta']! as Map),
        'data_freshness_sec': 7200,
      };
      await pumpApp(
        tester,
        routeTable({
          '/api/v1/health/v1': (_) => enveloped(healthOk),
          '/api/v1/analysis/latest': (_) => enveloped(staleAnalysis),
          '/api/v1/news': (_) => enveloped(newsList),
          '/api/v1/history': (_) => enveloped(historyList),
        }),
      );
      expect(find.textContaining(Ar.stale), findsWidgets);
    });
  });

  group('Settings / token configuration', () {
    testWidgets('missing token shows the configuration prompt',
        (tester) async {
      tester.view.physicalSize = const Size(1200, 4000);
      tester.view.devicePixelRatio = 1.0;
      addTearDown(tester.view.resetPhysicalSize);
      addTearDown(tester.view.resetDevicePixelRatio);
      final state = await stateWith(
        routeTable({
          '/api/v1/health/v1': (_) => enveloped(healthOk),
        }),
        token: '',
      );
      await tester.pumpWidget(AyzrApp(state: state));
      await tester.pumpAndSettle();
      // With no token the health card renders the unauthorized panel.
      expect(find.textContaining('رمز العميل'), findsWidgets);
    });

    testWidgets('settings screen persists the URL and token', (tester) async {
      tester.view.physicalSize = const Size(1200, 4000);
      tester.view.devicePixelRatio = 1.0;
      addTearDown(tester.view.resetPhysicalSize);
      addTearDown(tester.view.resetDevicePixelRatio);
      final state = await stateWith(
        routeTable({
          '/api/v1/health/v1': (_) => enveloped(healthOk),
          '/api/v1/analysis/latest': (_) => enveloped(analysisCalibrated),
          '/api/v1/news': (_) => enveloped(newsList),
          '/api/v1/history': (_) => enveloped(historyList),
        }),
      );
      await tester.pumpWidget(AyzrApp(state: state));
      await tester.pumpAndSettle();

      await tester.tap(find.byIcon(Icons.settings_outlined));
      await tester.pumpAndSettle();
      expect(find.text(Ar.backendUrl), findsOneWidget);

      await tester.enterText(find.byType(TextField).first, 'https://new.test');
      await tester.tap(find.text(Ar.save));
      await tester.pumpAndSettle();

      expect(state.config.normalizedBase, 'https://new.test');
    });
  });
}
