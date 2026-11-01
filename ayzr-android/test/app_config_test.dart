import 'package:ayzr_app/core/app_config.dart';
import 'package:flutter_test/flutter_test.dart';

// The user pastes the server URL in whatever shape a connection doc gives it.
// Both `https://host` and `https://host/api/v1` must resolve to the same
// endpoint; a doubled `/api/v1/api/v1/...` was the reported failure.
void main() {
  group('AppConfig.endpoint normalizes the base URL', () {
    test('host-only base keeps the path once', () {
      const c = AppConfig(
        baseUrl: 'https://work-1.example.dev',
        clientToken: 't',
      );
      expect(
        c.endpoint('/api/v1/analysis/latest').toString(),
        'https://work-1.example.dev/api/v1/analysis/latest',
      );
    });

    test('base ending in /api/v1 does not double the prefix', () {
      const c = AppConfig(
        baseUrl: 'https://work-1.example.dev/api/v1',
        clientToken: 't',
      );
      expect(
        c.endpoint('/api/v1/analysis/latest').toString(),
        'https://work-1.example.dev/api/v1/analysis/latest',
      );
    });

    test('base ending in /api/v1/ (trailing slash) is also safe', () {
      const c = AppConfig(
        baseUrl: 'https://work-1.example.dev/api/v1/',
        clientToken: 't',
      );
      expect(
        c.endpoint('/api/v1/health/v1').toString(),
        'https://work-1.example.dev/api/v1/health/v1',
      );
    });

    test('query parameters are preserved on a /api/v1 base', () {
      const c = AppConfig(
        baseUrl: 'https://work-1.example.dev/api/v1',
        clientToken: 't',
      );
      final uri = c.endpoint('/api/v1/news', {'limit': '20', 'gold_only': 'true'});
      expect(uri.path, '/api/v1/news');
      expect(uri.queryParameters, {'limit': '20', 'gold_only': 'true'});
    });

    test('a genuine non-api prefix is retained', () {
      const c = AppConfig(
        baseUrl: 'https://edge.example.dev/astra',
        clientToken: 't',
      );
      expect(
        c.endpoint('/api/v1/health').toString(),
        'https://edge.example.dev/astra/api/v1/health',
      );
    });

    test('normalizedBase strips stray trailing slashes', () {
      const c = AppConfig(baseUrl: 'https://h.dev///', clientToken: 't');
      expect(c.normalizedBase, 'https://h.dev');
    });
  });
}
