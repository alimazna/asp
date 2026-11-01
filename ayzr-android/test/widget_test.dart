import 'package:ayzr_app/core/strings_ar.dart';
import 'package:ayzr_app/data/models/news.dart';
import 'package:ayzr_app/ui/app_shell.dart';
import 'package:ayzr_app/ui/widgets/analysis_view.dart';
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
  _toneWidgetTests();
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

  group('AI provider health banner', () {
    // The whole point of the aggregate: when no credential can answer, the app
    // must say so plainly instead of showing the generic "provider busy, retry
    // shortly" message that no retry can ever clear.
    testWidgets('all providers exhausted shows the dedicated stop banner',
        (tester) async {
      await pumpApp(
        tester,
        routeTable({
          '/api/v1/health/v1': (_) => enveloped(healthOk),
          '/api/v1/analysis/latest': (_) => enveloped(analysisCalibrated),
          '/api/v1/news': (_) => enveloped(newsList),
          '/api/v1/history': (_) => enveloped(historyList),
          '/api/v1/news/quota': (_) => enveloped({
                'category': 'quota_exhausted',
                'quota_exhausted': true,
                'rate_limited': false,
                'cooling_down': true,
                'cooldown_until': 0,
                'chain_summary': 'all_quota_exhausted',
                'providers_configured': 3,
                'providers_unavailable': 3,
                'any_provider_available': false,
                'all_providers_unavailable': true,
                'all_providers_quota_exhausted': true,
                'providers': [
                  {
                    'provider': 'gemini',
                    'last_outcome': 'quota_exhausted',
                    'unavailable': true,
                    'quota_exhausted': true,
                  },
                ],
              }),
        }),
      );

      expect(find.text(Ar.allExhaustedTitle), findsWidgets);
      expect(find.text(Ar.quotaExhaustedTitle), findsNothing);
    });

    // Regression: a rejected key was previously rendered as "provider busy,
    // retry in a moment", which is false — retrying never fixes a dead key.
    testWidgets('all keys rejected reads as a stop, not a temporary busy',
        (tester) async {
      await pumpApp(
        tester,
        routeTable({
          '/api/v1/health/v1': (_) => enveloped(healthOk),
          '/api/v1/analysis/latest': (_) => enveloped(analysisCalibrated),
          '/api/v1/news': (_) => enveloped(newsList),
          '/api/v1/history': (_) => enveloped(historyList),
          '/api/v1/news/quota': (_) => enveloped({
                'category': 'auth_failure',
                'quota_exhausted': false,
                'rate_limited': false,
                'cooling_down': false,
                'chain_summary': 'all_unavailable',
                'providers_configured': 2,
                'providers_unavailable': 2,
                'any_provider_available': false,
                'all_providers_unavailable': true,
                'all_providers_quota_exhausted': false,
                'providers': [
                  {
                    'provider': 'gemini',
                    'last_outcome': 'auth_failure',
                    'unavailable': true,
                    'quota_exhausted': false,
                  },
                  {
                    'provider': 'zai',
                    'last_outcome': 'auth_failure',
                    'unavailable': true,
                    'quota_exhausted': false,
                  },
                ],
              }),
        }),
      );

      expect(find.text(Ar.allDownTitle), findsWidgets);
      expect(find.text(Ar.rateLimitedTitle), findsNothing);
      expect(find.textContaining(Ar.allDownReasonAuth), findsWidgets);
    });

    testWidgets('a healthy chain shows no banner', (tester) async {
      await pumpApp(
        tester,
        routeTable({
          '/api/v1/health/v1': (_) => enveloped(healthOk),
          '/api/v1/analysis/latest': (_) => enveloped(analysisCalibrated),
          '/api/v1/news': (_) => enveloped(newsList),
          '/api/v1/history': (_) => enveloped(historyList),
          '/api/v1/news/quota': (_) => enveloped({
                'category': 'ok',
                'quota_exhausted': false,
                'rate_limited': false,
                'cooling_down': false,
                'chain_summary': 'ok',
                'providers_configured': 3,
                'providers_unavailable': 0,
                'any_provider_available': true,
                'all_providers_unavailable': false,
                'all_providers_quota_exhausted': false,
                'providers': [],
              }),
        }),
      );

      expect(find.text(Ar.quotaExhaustedTitle), findsNothing);
      expect(find.text(Ar.allDownTitle), findsNothing);
      expect(find.text(Ar.allExhaustedTitle), findsNothing);
      expect(find.text(Ar.degradedTitle), findsNothing);
    });

    testWidgets('one provider down shows the degraded notice', (tester) async {
      await pumpApp(
        tester,
        routeTable({
          '/api/v1/health/v1': (_) => enveloped(healthOk),
          '/api/v1/analysis/latest': (_) => enveloped(analysisCalibrated),
          '/api/v1/news': (_) => enveloped(newsList),
          '/api/v1/history': (_) => enveloped(historyList),
          '/api/v1/news/quota': (_) => enveloped({
                'category': 'ok',
                'quota_exhausted': false,
                'rate_limited': false,
                'cooling_down': false,
                'chain_summary': 'degraded',
                'providers_configured': 3,
                'providers_unavailable': 1,
                'any_provider_available': true,
                'all_providers_unavailable': false,
                'all_providers_quota_exhausted': false,
                'providers': [
                  {
                    'provider': 'gemini',
                    'last_outcome': 'auth_failure',
                    'unavailable': true,
                    'quota_exhausted': false,
                  },
                ],
              }),
        }),
      );

      expect(find.text(Ar.degradedTitle), findsWidgets);
      expect(find.textContaining('gemini'), findsWidgets);
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

  group('News results log', () {
    testWidgets('opens from the app bar and shows source metadata',
        (tester) async {
      await pumpApp(
        tester,
        routeTable({
          '/api/v1/health/v1': (_) => enveloped(healthOk),
          '/api/v1/analysis/latest': (_) => enveloped(analysisCalibrated),
          '/api/v1/news': (_) => enveloped(newsList),
          '/api/v1/history': (_) => enveloped(historyList),
          '/api/v1/news/results': (_) => enveloped(newsResults),
        }),
      );

      await tester.tap(find.byIcon(Icons.fact_check_outlined));
      await tester.pumpAndSettle();

      // The headline and its source come from the joined article row.
      expect(find.text('الذهب يرتفع مع تراجع الدولار'), findsWidgets);
      expect(find.text('Reuters'), findsWidgets);
      // The channel classifier is rendered as its Arabic label.
      expect(find.text(Ar.channelUsdRates), findsWidgets);
      // A missing article row renders as unavailable, not a fabricated headline.
      expect(find.text(Ar.unavailable), findsWidgets);
    });

    testWidgets('empty results log shows the dedicated Arabic empty state',
        (tester) async {
      await pumpApp(
        tester,
        routeTable({
          '/api/v1/health/v1': (_) => enveloped(healthOk),
          '/api/v1/analysis/latest': (_) => enveloped(analysisCalibrated),
          '/api/v1/news': (_) => enveloped(newsList),
          '/api/v1/history': (_) => enveloped(historyList),
          '/api/v1/news/results':
              (_) => enveloped(const {'results': <Object?>[]}),
        }),
      );

      await tester.tap(find.byIcon(Icons.fact_check_outlined));
      await tester.pumpAndSettle();
      expect(find.text(Ar.newsResultsEmpty), findsOneWidget);
    });

    testWidgets('a filter chip sends that direction to the server',
        (tester) async {
      final seen = <String?>[];
      await pumpApp(
        tester,
        (req) async {
          if (req.url.path == '/api/v1/news/results') {
            seen.add(req.url.queryParameters['direction']);
            return enveloped(newsResults);
          }
          const routes = {
            '/api/v1/health/v1': healthOk,
            '/api/v1/analysis/latest': analysisCalibrated,
            '/api/v1/news': newsList,
            '/api/v1/history': historyList,
          };
          final payload = routes[req.url.path];
          if (payload != null) return enveloped(payload);
          return errorBody('not_found', 'unknown route', 404);
        },
      );

      await tester.tap(find.byIcon(Icons.fact_check_outlined));
      await tester.pumpAndSettle();
      // First load carries no filter: "all" must not send a direction token.
      expect(seen, [null]);

      await tester.tap(find.text(Ar.dirBearish));
      await tester.pumpAndSettle();
      expect(seen.last, 'bearish');

      // Returning to "all" drops the filter again rather than sending an empty
      // token the server would treat as a bogus value.
      await tester.tap(find.text(Ar.filterAll));
      await tester.pumpAndSettle();
      expect(seen.last, isNull);
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

// --- Tone / framing card ---------------------------------------------------
// The tone section is the "understanding" layer: it must render the Arabic
// label for a classified tone, show the quoted wording that carries it, and
// stay absent entirely when the model classified nothing (a card of five
// "غير محدَّدة" rows would be noise, not understanding).

Widget _analysisHost(NewsAnalysis a) => MaterialApp(
      home: Directionality(
        textDirection: TextDirection.rtl,
        child: Scaffold(
          body: SingleChildScrollView(child: AnalysisView(analysis: a)),
        ),
      ),
    );

NewsAnalysis _toneAnalysis(Map<String, dynamic> extra) =>
    NewsAnalysis.fromJson({
      'article_id': 'x',
      'status': 'ok',
      'direction': 'no_trade',
      ...extra,
    });

void _toneWidgetTests() {
  group('AnalysisView tone card', () {
    testWidgets('renders the Arabic tone label and the quoted wording',
        (tester) async {
      await tester.pumpWidget(_analysisHost(_toneAnalysis({
        'tone': 'very_negative',
        'tone_strength': 'strong',
        'tone_evidence_ar': 'انهيار حاد في الأسعار',
        'hidden_context_ar': 'الخبر يخدم مصالح المنتجين.',
      })));
      await tester.pumpAndSettle();
      expect(find.text(Ar.toneSection), findsOneWidget);
      expect(find.text(Ar.toneVeryNegative), findsOneWidget);
      expect(find.text(Ar.strengthStrong), findsOneWidget);
      expect(find.text('انهيار حاد في الأسعار'), findsOneWidget);
      expect(find.text(Ar.hiddenContextLabel), findsOneWidget);
      expect(find.text('الخبر يخدم مصالح المنتجين.'), findsOneWidget);
      expect(find.text(Ar.toneCaveat), findsOneWidget);
    });

    testWidgets('omits the card entirely when nothing was classified',
        (tester) async {
      await tester.pumpWidget(_analysisHost(_toneAnalysis({})));
      await tester.pumpAndSettle();
      expect(find.text(Ar.toneSection), findsNothing);
      expect(find.text(Ar.toneCaveat), findsNothing);
    });

    testWidgets('an unrecognised tone renders as unknown, not a guess',
        (tester) async {
      await tester.pumpWidget(_analysisHost(_toneAnalysis({
        'tone': 'extremely_bullish',
      })));
      await tester.pumpAndSettle();
      expect(find.text(Ar.toneSection), findsOneWidget);
      expect(find.text(Ar.toneUnknown), findsOneWidget);
      expect(find.text(Ar.toneVeryPositive), findsNothing);
    });
  });
}
