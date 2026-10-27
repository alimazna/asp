import 'package:ayzr_app/data/api_result.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:http/http.dart' as http;

import 'test_helpers.dart';

void main() {
  group('ApiClient happy paths', () {
    test('health() unwraps the envelope and parses fields', () async {
      final client = fakeClient(routeTable({
        '/api/v1/health/v1': (_) => enveloped(healthOk),
      }));
      final r = await client.health();
      expect(r.isSuccess, isTrue);
      expect(r.value!.status, 'ok');
      expect(r.value!.bridge, 'ok');
      expect(r.value!.uptimeSec, 1234);
    });

    test('latestAnalysis() parses the analysis payload', () async {
      final client = fakeClient(routeTable({
        '/api/v1/analysis/latest': (_) => enveloped(analysisCalibrated),
      }));
      final r = await client.latestAnalysis();
      expect(r.isSuccess, isTrue);
      expect(r.value!.signal.direction, 'UP');
      expect(r.value!.signal.probabilityCalibrated, isTrue);
    });

    test('news() sends limit and gold_only and parses the list', () async {
      late Uri seen;
      final client = fakeClient(routeTable({
        '/api/v1/news': (req) {
          seen = req.url;
          return enveloped(newsList['news']);
        },
      }));
      final r = await client.news(limit: 10, goldOnly: true);
      expect(r.isSuccess, isTrue);
      expect(r.value, hasLength(2));
      expect(seen.queryParameters['limit'], '10');
      expect(seen.queryParameters['gold_only'], 'true');
    });

    test('analyze() passes the id and parses the detail', () async {
      late Uri seen;
      final client = fakeClient(routeTable({
        '/api/v1/news/analyze': (req) {
          seen = req.url;
          return enveloped(newsDetail);
        },
      }));
      final r = await client.analyze('a1');
      expect(r.isSuccess, isTrue);
      expect(r.value!.analysis!.isOk, isTrue);
      expect(seen.queryParameters['id'], 'a1');
    });

    test('history() parses the history array', () async {
      final client = fakeClient(routeTable({
        '/api/v1/history': (_) => enveloped(historyList['history']),
      }));
      final r = await client.history();
      expect(r.isSuccess, isTrue);
      expect(r.value, hasLength(1));
      expect(r.value!.first.headline, 'الذهب يرتفع مع تراجع الدولار');
    });
  });

  group('ApiClient auth header', () {
    test('sends a bearer client token on every request', () async {
      String? auth;
      final client = fakeClient(routeTable({
        '/api/v1/health/v1': (req) {
          auth = req.headers['Authorization'];
          return enveloped(healthOk);
        },
      }), token: 'secret-read-token');
      await client.health();
      expect(auth, 'Bearer secret-read-token');
    });

    test('a missing token fails fast as unauthorized without a call',
        () async {
      var called = false;
      final client = fakeClient((_) async {
        called = true;
        return enveloped(healthOk);
      }, token: '');
      final r = await client.health();
      expect(r.isSuccess, isFalse);
      expect(r.failure!.kind, ApiFailureKind.unauthorized);
      expect(called, isFalse);
    });
  });

  group('ApiClient failure mapping', () {
    test('401 maps to unauthorized', () async {
      final client = fakeClient(routeTable({
        '/api/v1/news': (_) =>
            errorBody('unauthorized', 'a valid client bearer token is required', 401),
      }));
      final r = await client.news();
      expect(r.isSuccess, isFalse);
      expect(r.failure!.kind, ApiFailureKind.unauthorized);
      expect(r.failure!.statusCode, 401);
    });

    test('429 maps to rateLimited', () async {
      final client = fakeClient(routeTable({
        '/api/v1/history': (_) =>
            errorBody('rate_limited', 'client rate limit exceeded', 429),
      }));
      final r = await client.history();
      expect(r.isSuccess, isFalse);
      expect(r.failure!.kind, ApiFailureKind.rateLimited);
    });

    test('500 maps to server with the backend message', () async {
      final client = fakeClient(routeTable({
        '/api/v1/health/v1': (_) =>
            errorBody('dependency_unavailable', 'backend component unavailable', 503),
      }));
      final r = await client.health();
      expect(r.isSuccess, isFalse);
      expect(r.failure!.kind, ApiFailureKind.server);
      expect(r.failure!.message, contains('unavailable'));
    });

    test('a socket failure maps to offline', () async {
      final client = fakeClient((_) async {
        throw http.ClientException('connection refused');
      });
      final r = await client.health();
      expect(r.isSuccess, isFalse);
      expect(r.failure!.kind, ApiFailureKind.offline);
      expect(r.failure!.isOffline, isTrue);
    });

    test('malformed JSON maps to server, not a crash', () async {
      final client = fakeClient((_) async => http.Response('not json', 200));
      final r = await client.health();
      expect(r.isSuccess, isFalse);
      expect(r.failure!.kind, ApiFailureKind.server);
    });

    test('a non-envelope body is rejected', () async {
      final client = fakeClient((_) async => http.Response('[1,2,3]', 200));
      final r = await client.health();
      expect(r.isSuccess, isFalse);
      expect(r.failure!.kind, ApiFailureKind.server);
    });
  });
}
