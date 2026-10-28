import 'package:ayzr_app/data/models/analysis.dart';
import 'package:ayzr_app/data/models/news.dart';
import 'package:flutter_test/flutter_test.dart';

import 'test_helpers.dart';

void main() {
  _aiGoldTests();

  group('AnalysisLatest parsing', () {
    test('parses the uncalibrated v1 shape and keeps nulls null', () {
      final a = AnalysisLatest.fromJson(
          Map<String, dynamic>.from(analysisUncalibrated));

      expect(a.symbol, 'XAUUSD');
      expect(a.timestamp, '2026-10-16T09:00:00Z');
      expect(a.signal.direction, 'NONE');
      expect(a.signal.score, closeTo(0.512, 1e-9));
      // RULE C: probability must stay null when not calibrated.
      expect(a.signal.probability, isNull);
      expect(a.signal.probabilityCalibrated, isFalse);
      expect(a.context.mtfAgreement, isNull);
      expect(a.levels.entry, isNull);
      expect(a.levels.hasAnyValue, isFalse);
      expect(a.meta.dataFreshnessSec, isNull);
      expect(a.meta.degraded, isTrue);
    });

    test('parses the calibrated shape with a real probability', () {
      final a = AnalysisLatest.fromJson(
          Map<String, dynamic>.from(analysisCalibrated));

      expect(a.signal.probabilityCalibrated, isTrue);
      expect(a.signal.probability, closeTo(0.61, 1e-9));
      expect(a.context.mtfAgreement, closeTo(0.8, 1e-9));
      expect(a.levels.entry, closeTo(2650.5, 1e-9));
      expect(a.levels.rewardRisk, closeTo(2.33, 1e-9));
      expect(a.levels.hasAnyValue, isTrue);
      expect(a.meta.coverageTier, 'medium');
      expect(a.meta.degraded, isFalse);
    });

    test('a missing context object degrades to UNKNOWN, never fabricated', () {
      final a = AnalysisLatest.fromJson({
        'symbol': 'XAUUSD',
        'signal': <String, dynamic>{},
      });
      expect(a.context.regime, 'UNKNOWN');
      expect(a.context.h4Bias, 'UNKNOWN');
      expect(a.signal.direction, 'UNKNOWN');
      expect(a.signal.probability, isNull);
      expect(a.levels.hasAnyValue, isFalse);
    });

    test('probability is never read from score (RULE C)', () {
      final a = AnalysisLatest.fromJson({
        'signal': {
          'direction': 'UP',
          'score': 0.9,
          'probability_calibrated': false,
          'probability': null,
        },
      });
      expect(a.signal.probability, isNull);
      expect(a.signal.score, closeTo(0.9, 1e-9));
    });
  });

  group('News models', () {
    test('parses a news list item with its analysis status', () {
      final items = (newsList['news']! as List)
          .map((e) => NewsListItem.fromJson(
              Map<String, dynamic>.from(e as Map)))
          .toList();

      expect(items, hasLength(2));
      expect(items.first.article.headline, 'الذهب يرتفع مع تراجع الدولار');
      expect(items.first.isAnalyzed, isTrue);
      expect(items[1].isAnalyzed, isFalse);
      expect(items.first.article.publishedAtIso, '2026-10-16T08:00:00Z');
    });

    test('parses a detail with analysis and gates on status', () {
      final d = NewsDetail.fromJson(Map<String, dynamic>.from(newsDetail));
      expect(d.hasAnalysis, isTrue);
      expect(d.analysis, isNotNull);
      expect(d.analysis!.isOk, isTrue);
      expect(d.analysis!.summaryAr, 'تراجع الدولار يدعم الذهب.');
      expect(d.analysis!.facts, hasLength(1));
      expect(d.analysis!.entry, closeTo(2650.0, 1e-9));
      expect(d.analysis!.limitationsAr, hasLength(1));
    });

    test('detail without analysis reports hasAnalysis false', () {
      final d = NewsDetail.fromJson(
          Map<String, dynamic>.from(newsDetailNoAnalysis));
      expect(d.hasAnalysis, isFalse);
      expect(d.analysis, isNull);
    });

    test('a non-ok analysis status is not isOk (no proposal shown)', () {
      final an = NewsAnalysis.fromJson({
        'status': 'insufficient_data',
        'status_reason': 'evidence contradictory',
      });
      expect(an.isOk, isFalse);
      expect(an.statusReason, 'evidence contradictory');
    });

    test('parses a history entry joined with its article', () {
      final entries = (historyList['history']! as List)
          .map((e) =>
              HistoryEntry.fromJson(Map<String, dynamic>.from(e as Map)))
          .toList();
      expect(entries, hasLength(1));
      expect(entries.first.source, 'Reuters');
      expect(entries.first.analysis.direction, 'bullish');
    });
  });

  group('LlmQuota parsing', () {
    test('a spent daily quota keeps its reset time and is not ok', () {
      final q = LlmQuota.fromJson({
        'category': 'quota_exhausted',
        'retry_after_seconds': 68400,
        'cooling_down': true,
        'cooldown_until': 1792114200,
        'quota_exhausted': true,
        'rate_limited': false,
      });
      expect(q.isOk, isFalse);
      expect(q.quotaExhausted, isTrue);
      expect(q.rateLimited, isFalse);
      expect(q.coolingDown, isTrue);
      expect(q.retryAfterSeconds, 68400);
    });

    test('an unknown reset time stays null, never a fabricated countdown', () {
      final q = LlmQuota.fromJson({
        'category': 'quota_exhausted',
        'retry_after_seconds': null,
        'cooling_down': true,
        'cooldown_until': 0,
        'quota_exhausted': true,
        'rate_limited': false,
      });
      expect(q.retryAfterSeconds, isNull);
      expect(q.quotaExhausted, isTrue);
    });

    test('a temporary rate limit is distinct from exhaustion', () {
      final q = LlmQuota.fromJson({
        'category': 'rate_limited',
        'retry_after_seconds': 20,
        'quota_exhausted': false,
        'rate_limited': true,
      });
      expect(q.rateLimited, isTrue);
      expect(q.quotaExhausted, isFalse);
      expect(q.isOk, isFalse);
    });

    test('an ok snapshot reports ok', () {
      final q = LlmQuota.fromJson({'category': 'ok'});
      expect(q.isOk, isTrue);
      expect(q.quotaExhausted, isFalse);
    });

    test('an absent chain block defaults to available, never "all down"', () {
      // An older backend (or a pinned single provider) sends no chain fields.
      // The app must not then claim every provider is down.
      final q = LlmQuota.fromJson({'category': 'ok'});
      expect(q.allProvidersDown, isFalse);
      expect(q.anyProviderAvailable, isTrue);
      expect(q.chainSummary, 'ok');
      expect(q.providers, isEmpty);
    });

    test('parses the whole-chain verdict and per-provider rows', () {
      final q = LlmQuota.fromJson({
        'category': 'quota_exhausted',
        'quota_exhausted': true,
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
          {
            'provider': 'zai',
            'last_outcome': 'quota_exhausted',
            'unavailable': true,
            'quota_exhausted': true,
          },
        ],
      });
      expect(q.allProvidersDown, isTrue);
      expect(q.allProvidersQuotaExhausted, isTrue);
      expect(q.providersConfigured, 3);
      expect(q.providers.length, 2);
      expect(q.providers.first.provider, 'gemini');
      expect(q.providers.first.quotaExhausted, isTrue);
    });

    test('one provider down is degraded, not all-down', () {
      final q = LlmQuota.fromJson({
        'category': 'ok',
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
          {
            'provider': 'zai',
            'last_outcome': 'ok',
            'unavailable': false,
            'quota_exhausted': false,
          },
        ],
      });
      expect(q.isOk, isTrue);
      expect(q.allProvidersDown, isFalse);
      expect(q.providersUnavailable, 1);
    });
  });
}

// --- AI gold-impact classification ------------------------------------------

void _aiGoldTests() {
  group('NewsAnalysis.aiGoldRelevance', () {
    test('parses the AI classification when the backend asserts one', () {
      final a = NewsAnalysis.fromJson({
        'article_id': 'x',
        'status': 'ok',
        'direction': 'no_trade',
        'ai_gold_relevance': 'high',
      });
      expect(a.aiGoldRelevance, 'high');
    });

    test('an absent field is "unknown", never a guessed relevance', () {
      final a = NewsAnalysis.fromJson({
        'article_id': 'x',
        'status': 'ok',
        'direction': 'no_trade',
      });
      expect(a.aiGoldRelevance, 'unknown');
    });

    test('the AI value is independent of the feed routing tag', () {
      final detail = NewsDetail.fromJson({
        'article': {
          'article_id': 'x',
          'headline': 'h',
          'gold_relevance': 'medium', // feed's routing tag
        },
        'has_analysis': true,
        'analysis': {
          'article_id': 'x',
          'status': 'ok',
          'direction': 'no_trade',
          'ai_gold_relevance': 'none', // the model's own judgement
        },
      });
      expect(detail.article.goldRelevance, 'medium');
      expect(detail.analysis!.aiGoldRelevance, 'none');
    });
  });
}
