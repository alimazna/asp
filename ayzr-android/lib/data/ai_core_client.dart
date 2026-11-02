/// Authenticated client for the AYZR Native AI Core (`/api/v1/ai/*`).
///
/// The AI Core is a separate service with its own base URL and its own token,
/// so it gets its own client rather than being folded into [ApiClient]. If the
/// AI service is down, this client fails independently and the rest of the app
/// keeps working — which is the whole point of the split.
///
/// Only these routes are called, all POST with a JSON body:
///   GET  /api/v1/ai/health
///   GET  /api/v1/ai/models
///   POST /api/v1/ai/chat
///   POST /api/v1/ai/analyze/news
///   POST /api/v1/ai/explain/opportunity
///
/// No token is embedded. It is supplied at runtime like the backend token.
library;

import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'package:http/http.dart' as http;

import '../core/app_config.dart';
import 'api_result.dart';
import 'models/ai_core.dart';

class AiCoreClient {
  AiCoreClient({
    required AppConfig config,
    http.Client? httpClient,
    // Local CPU inference is slow; a short timeout would report a working
    // service as offline. This is deliberately longer than the read timeout.
    Duration timeout = const Duration(seconds: 180),
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
        'Content-Type': 'application/json',
        if (_config.hasToken)
          'Authorization': 'Bearer ${_config.clientToken.trim()}',
      };

  Future<ApiResult<AiHealth>> health() async {
    final result = await _request('GET', '/api/v1/ai/health');
    return result.map((d) => AiHealth.fromJson(d as Map<String, dynamic>));
  }

  Future<ApiResult<AiAnswer>> chat({
    required String question,
    String sessionId = 'mobile',
  }) async {
    final result = await _request('POST', '/api/v1/ai/chat', body: {
      'question': question,
      'session_id': sessionId,
    });
    return result.map((d) => AiAnswer.fromJson(d as Map<String, dynamic>));
  }

  Future<ApiResult<AiAnswer>> analyzeNews({
    required String headline,
    String body = '',
    String source = '',
    int publishedAt = 0,
  }) async {
    final result = await _request('POST', '/api/v1/ai/analyze/news', body: {
      'headline': headline,
      'body': body,
      'source': source,
      'published_at': publishedAt,
    });
    return result.map((d) => AiAnswer.fromJson(d as Map<String, dynamic>));
  }

  Future<ApiResult<AiAnswer>> explainOpportunity({
    String opportunityId = '',
    String question = '',
  }) async {
    final result = await _request('POST', '/api/v1/ai/explain/opportunity', body: {
      'opportunity_id': opportunityId,
      'question': question,
    });
    return result.map((d) => AiAnswer.fromJson(d as Map<String, dynamic>));
  }

  /// The AI service returns a flat JSON body (no `{api,schema,data}` envelope),
  /// unlike the ASTRA backend. Failures are typed, never thrown.
  Future<ApiResult<dynamic>> _request(
    String method,
    String path, {
    Map<String, dynamic>? body,
  }) async {
    if (!_config.hasToken) {
      return const ApiResult.failure(ApiFailure(
        kind: ApiFailureKind.unauthorized,
        message: 'AI token not configured',
        statusCode: 401,
      ));
    }
    final Uri uri;
    try {
      uri = _config.endpoint(path);
    } catch (e) {
      return ApiResult.failure(ApiFailure(
        kind: ApiFailureKind.server,
        message: 'invalid base url: $e',
      ));
    }

    http.Response resp;
    try {
      final encoded = body == null ? null : jsonEncode(body);
      resp = await (method == 'GET'
              ? _http.get(uri, headers: _headers)
              : _http.post(uri, headers: _headers, body: encoded))
          .timeout(_timeout);
    } on TimeoutException {
      return const ApiResult.failure(ApiFailure(
        kind: ApiFailureKind.offline,
        message: 'AI request timed out',
      ));
    } on SocketException catch (e) {
      return ApiResult.failure(ApiFailure(
        kind: ApiFailureKind.offline,
        message: 'AI service unreachable: ${e.message}',
      ));
    } on http.ClientException catch (e) {
      return ApiResult.failure(ApiFailure(
        kind: ApiFailureKind.offline,
        message: 'AI connection failed: ${e.message}',
      ));
    } catch (e) {
      return ApiResult.failure(ApiFailure(
        kind: ApiFailureKind.offline,
        message: 'AI connection failed: $e',
      ));
    }

    if (resp.statusCode == 401 || resp.statusCode == 403) {
      return ApiResult.failure(ApiFailure(
        kind: ApiFailureKind.unauthorized,
        message: 'unauthorized',
        statusCode: resp.statusCode,
      ));
    }
    if (resp.statusCode == 429) {
      return const ApiResult.failure(ApiFailure(
        kind: ApiFailureKind.rateLimited,
        message: 'rate limited',
        statusCode: 429,
      ));
    }
    if (resp.statusCode < 200 || resp.statusCode >= 300) {
      return ApiResult.failure(ApiFailure(
        kind: ApiFailureKind.server,
        message: 'HTTP ${resp.statusCode}',
        statusCode: resp.statusCode,
      ));
    }

    try {
      final decoded = jsonDecode(utf8.decode(resp.bodyBytes));
      if (decoded is! Map<String, dynamic>) {
        return const ApiResult.failure(ApiFailure(
          kind: ApiFailureKind.server,
          message: 'unexpected AI response shape',
        ));
      }
      return ApiResult.success(decoded);
    } catch (e) {
      return ApiResult.failure(ApiFailure(
        kind: ApiFailureKind.server,
        message: 'malformed AI JSON: $e',
      ));
    }
  }
}
