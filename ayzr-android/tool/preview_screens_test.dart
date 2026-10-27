// Golden screenshots of the real AYZR widget tree.
//
// These render the actual screens (not mockups) against a controlled backend
// payload, at a phone viewport, with the Cairo Arabic font loaded, so the
// captured PNGs show exactly what the app draws. Run with:
//
//   flutter test --update-goldens tool/preview_screens_test.dart
//
// The PNGs land in tool/previews/ and are the source of the preview images.
// This file lives outside test/ on purpose: golden PNGs are renderer-specific,
// so it must not run as part of a plain `flutter test`.
import 'dart:io';

import 'package:ayzr_app/core/strings_ar.dart';
import 'package:ayzr_app/state/app_state.dart';
import 'package:ayzr_app/ui/app_shell.dart';
import 'package:ayzr_app/ui/screens/news_screen.dart';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:http/http.dart' as http;

import '../test/test_helpers.dart';

/// Loads the bundled Cairo font so Arabic renders as text, not tofu boxes.
Future<void> loadCairo() async {
  final bytes = File('assets/fonts/Cairo-Variable.ttf').readAsBytesSync();
  final loader = FontLoader('Cairo')
    ..addFont(Future.value(ByteData.sublistView(bytes)));
  await loader.load();
}

Future<void> pumpPhone(
  WidgetTester tester,
  Future<http.Response> Function(http.Request) handler, {
  Size size = const Size(390, 844),
}) async {
  tester.view.physicalSize = size;
  tester.view.devicePixelRatio = 1.0;
  addTearDown(tester.view.resetPhysicalSize);
  addTearDown(tester.view.resetDevicePixelRatio);
  final state = await stateWith(handler);
  await tester.pumpWidget(AyzrApp(state: state));
  await tester.pumpAndSettle();
}

Future<http.Response> Function(http.Request) liveBackend() => routeTable({
      '/api/v1/health/v1': (_) => enveloped(healthOk),
      '/api/v1/analysis/latest': (_) => enveloped(analysisCalibrated),
      '/api/v1/news': (_) => enveloped(newsList['news']),
      '/api/v1/history': (_) => enveloped(historyList['history']),
      '/api/v1/news/analyze': (_) => enveloped(newsDetail),
    });

void main() {
  setUpAll(loadCairo);

  testWidgets('golden home', (tester) async {
    await pumpPhone(tester, liveBackend());
    await expectLater(
      find.byType(MaterialApp),
      matchesGoldenFile('previews/01_home.png'),
    );
  });

  testWidgets('golden analysis', (tester) async {
    await pumpPhone(tester, liveBackend());
    await tester.tap(find.byIcon(Icons.insights_outlined));
    await tester.pumpAndSettle();
    await expectLater(
      find.byType(MaterialApp),
      matchesGoldenFile('previews/02_analysis.png'),
    );
  });

  testWidgets('golden news', (tester) async {
    await pumpPhone(tester, liveBackend());
    await tester.tap(find.byIcon(Icons.newspaper_outlined));
    await tester.pumpAndSettle();
    await expectLater(
      find.byType(MaterialApp),
      matchesGoldenFile('previews/03_news.png'),
    );
  });

  testWidgets('golden news detail', (tester) async {
    await pumpPhone(tester, liveBackend());
    await tester.tap(find.byIcon(Icons.newspaper_outlined));
    await tester.pumpAndSettle();
    // IndexedStack keeps all four screens alive, so scope the tap to the news
    // list's own card rather than any matching text elsewhere in the tree.
    final card = find.descendant(
      of: find.byType(NewsScreen),
      matching: find.byType(Card),
    );
    await tester.tap(card.first);
    await tester.pumpAndSettle();
    await expectLater(
      find.byType(MaterialApp),
      matchesGoldenFile('previews/04_news_detail.png'),
    );
  });

  testWidgets('golden history', (tester) async {
    await pumpPhone(tester, liveBackend());
    await tester.tap(find.byIcon(Icons.history_outlined));
    await tester.pumpAndSettle();
    await expectLater(
      find.byType(MaterialApp),
      matchesGoldenFile('previews/05_history.png'),
    );
  });

  testWidgets('golden empty state (no data)', (tester) async {
    await pumpPhone(
      tester,
      routeTable({
        '/api/v1/health/v1': (_) => enveloped(healthOk),
        '/api/v1/analysis/latest':
            (_) => errorBody('not_found', 'no analysis yet', 404),
        '/api/v1/news': (_) => enveloped(<Map<String, Object?>>[]),
        '/api/v1/history': (_) => enveloped(<Map<String, Object?>>[]),
      }),
    );
    await expectLater(
      find.byType(MaterialApp),
      matchesGoldenFile('previews/06_empty.png'),
    );
  });
}
