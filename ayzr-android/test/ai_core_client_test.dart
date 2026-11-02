import 'dart:convert';

import 'package:ayzr_app/core/app_config.dart';
import 'package:ayzr_app/data/ai_core_client.dart';
import 'package:ayzr_app/data/api_result.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:http/http.dart' as http;
import 'package:http/testing.dart';

AiCoreClient aiClient(
  Future<http.Response> Function(http.Request request) handler, {
  String baseUrl = 'https://ai.test',
  String token = 'ai-token-123',
}) =>
    AiCoreClient(
      config: AppConfig(baseUrl: baseUrl, clientToken: token),
      httpClient: MockClient(handler),
      timeout: const Duration(seconds: 5),
    );

http.Response json(Object body, {int status = 200}) => http.Response(
      jsonEncode(body),
      status,
      headers: {'content-type': 'application/json; charset=utf-8'},
    );

void main() {
  group('AppConfig AI fields', () {
    test('aiConfigured requires both url and token', () {
      expect(
        const AppConfig(baseUrl: 'b', clientToken: 't', aiBaseUrl: '', aiToken: '')
            .aiConfigured,
        isFalse,
      );
      expect(
        const AppConfig(baseUrl: 'b', clientToken: 't', aiBaseUrl: 'https://ai', aiToken: '')
            .aiConfigured,
        isFalse,
      );
      expect(
        const AppConfig(baseUrl: 'b', clientToken: 't', aiBaseUrl: 'https://ai', aiToken: 'x')
            .aiConfigured,
        isTrue,
      );
    });

    test('forAi points at the AI service with the AI token', () {
      const c = AppConfig(
        baseUrl: 'https://backend',
        clientToken: 'backend-token',
        aiBaseUrl: 'https://ai.example',
        aiToken: 'ai-token',
      );
      expect(c.forAi.baseUrl, 'https://ai.example');
      expect(c.forAi.clientToken, 'ai-token');
      // The backend token must not leak into the AI client.
      expect(c.forAi.clientToken, isNot('backend-token'));
    });

    test('copyWith preserves AI fields', () {
      const c = AppConfig(
        baseUrl: 'b',
        clientToken: 't',
        aiBaseUrl: 'https://ai',
        aiToken: 'x',
      );
      final n = c.copyWith(baseUrl: 'b2');
      expect(n.aiBaseUrl, 'https://ai');
      expect(n.aiToken, 'x');
    });
  });

  group('AiCoreClient', () {
    test('health() parses provider state', () async {
      final client = aiClient((req) async => json({
            'service': 'ayzr-native-ai-core',
            'ready': true,
            'startup_error': '',
            'provider_health': {
              'provider': 'local',
              'model': 'qwen2.5-1.5b',
              'reachable': true,
            },
          }));
      final r = await client.health();
      expect(r.isSuccess, isTrue);
      expect(r.value!.ready, isTrue);
      expect(r.value!.provider, 'local');
      expect(r.value!.reachable, isTrue);
    });

    test('health() reports not ready when the model runtime is down', () async {
      final client = aiClient((req) async => json({
            'ready': false,
            'provider_health': {'reachable': false},
          }));
      final r = await client.health();
      expect(r.isSuccess, isTrue);
      expect(r.value!.ready, isFalse);
      expect(r.value!.reachable, isFalse);
    });

    test('chat() posts the question and parses a grounded answer', () async {
      late http.Request seen;
      final client = aiClient((req) async {
        seen = req;
        return json({
          'status': 'ok',
          'answer_ar': 'سعر الذهب 3911',
          'direction': '',
          'agent': 'market_analyst',
          'provider': 'local',
          'model': 'qwen2.5-1.5b',
          'evidence': [
            {
              'kind': 'candles',
              'label': 'أحدث سعر',
              'source': '/api/v1/candles',
              'timestamp': 1793625300,
              'values': {'close': 3911},
            }
          ],
          'sources': ['/api/v1/candles'],
          'warnings_ar': <String>[],
          'reason_ar': '',
          'latency_ms': 25000,
        });
      });
      final r = await client.chat(question: 'ما سعر الذهب؟');
      expect(r.isSuccess, isTrue);
      expect(r.value!.isOk, isTrue);
      expect(r.value!.answerAr, 'سعر الذهب 3911');
      expect(r.value!.agent, 'market_analyst');
      expect(r.value!.evidence, hasLength(1));
      expect(r.value!.evidence.first.kind, 'candles');
      expect(seen.method, 'POST');
      final body = jsonDecode(seen.body) as Map<String, dynamic>;
      expect(body['question'], 'ما سعر الذهب؟');
    });

    test('chat() surfaces insufficient_data with the reason', () async {
      final client = aiClient((req) async => json({
            'status': 'insufficient_data',
            'answer_ar': '',
            'reason_ar': 'لا تتوفر بيانات شموع حديثة',
            'evidence': <Map<String, dynamic>>[],
          }));
      final r = await client.chat(question: 'ما سعر الذهب؟');
      expect(r.isSuccess, isTrue);
      expect(r.value!.isOk, isFalse);
      expect(r.value!.isInsufficient, isTrue);
      expect(r.value!.reasonAr, contains('شموع'));
    });

    test('chat() surfaces unavailable when the runtime is down', () async {
      final client = aiClient((req) async => json({
            'status': 'unavailable',
            'reason_ar': 'خدمة النموذج المحلي غير متاحة حالياً.',
          }));
      final r = await client.chat(question: 'ما سعر الذهب؟');
      expect(r.isSuccess, isTrue);
      expect(r.value!.isUnavailable, isTrue);
    });

    test('chat() surfaces invalid_output without showing an answer', () async {
      final client = aiClient((req) async => json({
            'status': 'invalid_output',
            'answer_ar': '',
            'reason_ar': 'مخرجات النموذج لم تجتز التحقق',
            'warnings_ar': ['summary_ar missing'],
          }));
      final r = await client.chat(question: 'حلّل');
      expect(r.value!.isInvalid, isTrue);
      expect(r.value!.answerAr, isEmpty);
      expect(r.value!.warningsAr, contains('summary_ar missing'));
    });

    test('analyzeNews() sends the article fields', () async {
      late http.Request seen;
      final client = aiClient((req) async {
        seen = req;
        return json({'status': 'ok', 'answer_ar': 'ملخص'});
      });
      await client.analyzeNews(
        headline: 'Fed signals cut',
        body: 'text',
        source: 'Reuters',
        publishedAt: 1793625000,
      );
      final body = jsonDecode(seen.body) as Map<String, dynamic>;
      expect(body['headline'], 'Fed signals cut');
      expect(body['source'], 'Reuters');
      expect(body['published_at'], 1793625000);
    });

    test('a missing token is an unauthorized failure, not a crash', () async {
      final client = aiClient((req) async => json({}), token: '');
      final r = await client.chat(question: 'x');
      expect(r.isSuccess, isFalse);
      expect(r.failure!.isUnauthorized, isTrue);
    });

    test('401 maps to unauthorized', () async {
      final client = aiClient((req) async => json({'detail': 'unauthorized'}, status: 401));
      final r = await client.chat(question: 'x');
      expect(r.failure!.kind, ApiFailureKind.unauthorized);
    });

    test('429 maps to rateLimited', () async {
      final client = aiClient((req) async => json({'detail': 'rate limited'}, status: 429));
      final r = await client.chat(question: 'x');
      expect(r.failure!.kind, ApiFailureKind.rateLimited);
    });

    test('503 (ai core unavailable) maps to a server failure', () async {
      final client = aiClient((req) async => json({'detail': 'ai core unavailable'}, status: 503));
      final r = await client.chat(question: 'x');
      expect(r.isSuccess, isFalse);
      expect(r.failure!.kind, ApiFailureKind.server);
    });

    test('malformed JSON is a server failure', () async {
      final client = aiClient((req) async => http.Response('not json', 200));
      final r = await client.chat(question: 'x');
      expect(r.isSuccess, isFalse);
      expect(r.failure!.kind, ApiFailureKind.server);
    });

    test('the AI token never appears in the request body', () async {
      late http.Request seen;
      final client = aiClient((req) async {
        seen = req;
        return json({'status': 'ok', 'answer_ar': 'x'});
      }, token: 'super-secret-ai-token');
      await client.chat(question: 'x');
      expect(seen.body.contains('super-secret-ai-token'), isFalse);
      expect(seen.headers['Authorization'], 'Bearer super-secret-ai-token');
    });

    test('explainOpportunity() posts and parses', () async {
      final client = aiClient((req) async => json({
            'status': 'ok',
            'answer_ar': 'شرح الفرصة',
            'agent': 'trade_explainer',
          }));
      final r = await client.explainOpportunity();
      expect(r.value!.answerAr, 'شرح الفرصة');
      expect(r.value!.agent, 'trade_explainer');
    });
  });
}
