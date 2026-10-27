import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'package:http/http.dart' as http;

import '../core/app_config.dart';
import 'api_result.dart';
import 'models/analysis.dart';
import 'models/health.dart';
import 'models/news.dart';

/// Authenticated client for the AYZR mobile read surface.
///
/// Only the documented, client-token routes are called:
///   GET /api/v1/health/v1
///   GET /api/v1/analysis/latest
///   GET /api/v1/news?limit=&gold_only=
///   GET /api/v1/news/analyze?id=
///   GET /api/v1/history?limit=
///
/// Ingestion, admin and sync routes are intentionally absent: this app is
/// read-only and must never be able to write candles or purge data.
class ApiClient {
  ApiClient({
    required AppConfig config,
    http.Client? httpClient,
    Duration timeout = const Duration(seconds: 15),
  })  : _config = config,
        _http = httpClient ?? http.Client(),
        _timeout = timeout;

  AppConfig _config;
  final http.Client _http;
  final Duration _timeout;

  AppConfig get config => _config;

  void updateConfig(AppConfig next) => _config = next;

  void dispose() => _http.close();

  Map<String, String> get _headers => {
        'Accept': 'application/json',
        if (_config.hasToken)
          'Authorization': 'Bearer ${_config.clientToken.trim()}',
      };

  /// Performs a GET, unwraps the `{api,schema,data}` envelope and returns the
  /// `data` node. Errors are mapped to typed [ApiFailure]s, never thrown.
  Future<ApiResult<dynamic>> _getData(
    String path, {
    Map<String, String>? query,
  }) async {
    if (!_config.hasToken) {
      return const ApiResult.failure(ApiFailure(
        kind: ApiFailureKind.unauthorized,
        message: 'client token not configured',
        statusCode: 401,
      ));
    }
    final Uri uri;
    try {
      uri = _config.endpoint(path, query);
    } catch (e) {
      return ApiResult.failure(ApiFailure(
        kind: ApiFailureKind.server,
        message: 'invalid base url: $e',
      ));
    }

    http.Response resp;
    try {
      resp = await _http.get(uri, headers: _headers).timeout(_timeout);
    } on TimeoutException {
      return const ApiResult.failure(ApiFailure(
        kind: ApiFailureKind.offline,
        message: 'request timed out',
      ));
    } on SocketException catch (e) {
      return ApiResult.failure(ApiFailure(
        kind: ApiFailureKind.offline,
        message: 'network unreachable: ${e.message}',
      ));
    } on http.ClientException catch (e) {
      return ApiResult.failure(ApiFailure(
        kind: ApiFailureKind.offline,
        message: 'connection failed: ${e.message}',
      ));
    } catch (e) {
      return ApiResult.failure(ApiFailure(
        kind: ApiFailureKind.offline,
        message: 'connection failed: $e',
      ));
    }

    if (resp.statusCode == 401 || resp.statusCode == 403) {
      return ApiResult.failure(ApiFailure(
        kind: ApiFailureKind.unauthorized,
        message: _errorMessage(resp) ?? 'unauthorized',
        statusCode: resp.statusCode,
      ));
    }
    if (resp.statusCode == 429) {
      return ApiResult.failure(ApiFailure(
        kind: ApiFailureKind.rateLimited,
        message: _errorMessage(resp) ?? 'rate limited',
        statusCode: 429,
      ));
    }
    if (resp.statusCode < 200 || resp.statusCode >= 300) {
      return ApiResult.failure(ApiFailure(
        kind: ApiFailureKind.server,
        message: _errorMessage(resp) ?? 'HTTP ${resp.statusCode}',
        statusCode: resp.statusCode,
      ));
    }

    try {
      final decoded = jsonDecode(utf8.decode(resp.bodyBytes));
      if (decoded is! Map<String, dynamic>) {
        return const ApiResult.failure(ApiFailure(
          kind: ApiFailureKind.server,
          message: 'unexpected response shape',
        ));
      }
      return ApiResult.success(decoded['data']);
    } catch (e) {
      return ApiResult.failure(ApiFailure(
        kind: ApiFailureKind.server,
        message: 'malformed JSON: $e',
      ));
    }
  }

  /// The backend's flat error object is `{error,code,message}` (not enveloped).
  String? _errorMessage(http.Response resp) {
    try {
      final decoded = jsonDecode(utf8.decode(resp.bodyBytes));
      if (decoded is Map && decoded['message'] is String) {
        return decoded['message'] as String;
      }
    } catch (_) {}
    return null;
  }

  Future<ApiResult<HealthV1>> health() async {
    final r = await _getData('/api/v1/health/v1');
    return r.map((d) => HealthV1.fromJson((d as Map).cast<String, dynamic>()));
  }

  Future<ApiResult<AnalysisLatest>> latestAnalysis() async {
    final r = await _getData('/api/v1/analysis/latest');
    return r.map(
        (d) => AnalysisLatest.fromJson((d as Map).cast<String, dynamic>()));
  }

  Future<ApiResult<List<NewsListItem>>> news({
    int limit = 50,
    bool goldOnly = false,
  }) async {
    final r = await _getData('/api/v1/news', query: {
      'limit': '$limit',
      if (goldOnly) 'gold_only': 'true',
    });
    return r.map((d) => (d as List)
        .whereType<Map>()
        .map((m) => NewsListItem.fromJson(m.cast<String, dynamic>()))
        .toList(growable: false));
  }

  Future<ApiResult<NewsDetail>> analyze(String articleId) async {
    final r = await _getData('/api/v1/news/analyze', query: {'id': articleId});
    return r.map(
        (d) => NewsDetail.fromJson((d as Map).cast<String, dynamic>()));
  }

  Future<ApiResult<List<HistoryEntry>>> history({int limit = 50}) async {
    final r = await _getData('/api/v1/history', query: {'limit': '$limit'});
    return r.map((d) => (d as List)
        .whereType<Map>()
        .map((m) => HistoryEntry.fromJson(m.cast<String, dynamic>()))
        .toList(growable: false));
  }
}
