import 'dart:convert';

import 'package:ayzr_app/core/app_config.dart';
import 'package:ayzr_app/data/api_client.dart';
import 'package:ayzr_app/state/app_state.dart';
import 'package:ayzr_app/state/settings_store.dart';
import 'package:http/http.dart' as http;
import 'package:http/testing.dart';
import 'package:shared_preferences/shared_preferences.dart';

/// Builds an [ApiClient] whose HTTP layer is fully controlled by [handler], so
/// tests exercise the real parsing/state code paths without any network.
ApiClient fakeClient(
  Future<http.Response> Function(http.Request request) handler, {
  String baseUrl = 'https://backend.test',
  String token = 'client-token-123',
}) {
  return ApiClient(
    config: AppConfig(baseUrl: baseUrl, clientToken: token),
    httpClient: MockClient(handler),
    timeout: const Duration(seconds: 5),
  );
}

/// Envelopes a `data` payload exactly as the backend does.
http.Response enveloped(Object? data, {int status = 200}) => http.Response(
      jsonEncode({'api': 'v1', 'schema': '1.0', 'data': data}),
      status,
      headers: {'content-type': 'application/json; charset=utf-8'},
    );

/// The backend's flat error object (not enveloped).
http.Response errorBody(String code, String message, int status) =>
    http.Response(
      jsonEncode({'error': 'true', 'code': code, 'message': message}),
      status,
      headers: {'content-type': 'application/json; charset=utf-8'},
    );

/// A route table handler: matches on the request path.
Future<http.Response> Function(http.Request) routeTable(
  Map<String, http.Response Function(http.Request req)> routes, {
  http.Response Function(http.Request req)? fallback,
}) {
  return (req) async {
    for (final entry in routes.entries) {
      if (req.url.path == entry.key) return entry.value(req);
    }
    if (fallback != null) return fallback(req);
    return errorBody('not_found', 'unknown route: ${req.url.path}', 404);
  };
}

/// An [AppState] backed by mock preferences and a fake client.
Future<AppState> stateWith(
  Future<http.Response> Function(http.Request) handler, {
  String token = 'client-token-123',
  String baseUrl = 'https://backend.test',
}) async {
  SharedPreferences.setMockInitialValues({});
  final settings = await SettingsStore.open();
  return AppState(
    settings: settings,
    client: fakeClient(handler, token: token, baseUrl: baseUrl),
  );
}

// ---- Realistic backend payloads (shapes taken from the v1 contract) ---------

const healthOk = {
  'status': 'ok',
  'bridge': 'ok',
  'version': 'v1',
  'uptime_sec': 1234,
};

const healthOffline = {
  'status': 'offline',
  'bridge': 'offline',
  'version': 'v1',
  'uptime_sec': null,
};

/// Uncalibrated analysis: score present, probability null (the v1 default).
const analysisUncalibrated = {
  'timestamp': '2026-10-16T09:00:00Z',
  'symbol': 'XAUUSD',
  'context': {
    'regime': 'RANGE',
    'h4_bias': 'UP',
    'm15_trigger': 'NONE',
    'mtf_agreement': null,
    'volatility_state': 'NORMAL',
  },
  'signal': {
    'direction': 'NONE',
    'horizon': null,
    'probability': null,
    'probability_calibrated': false,
    'score': 0.512,
    'confidence_lo': null,
    'confidence_hi': null,
    'model_version': null,
    'features_contributing': <String>[],
  },
  'levels': {
    'entry': null,
    'stop_loss': null,
    'take_profit': null,
    'reward_risk': null,
    'suggested_risk_pct': null,
    'sl_method': null,
    'tp_method': null,
  },
  'meta': {
    'coverage_tier': 'unknown',
    'data_freshness_sec': null,
    'degraded': true,
    'score_is_probability': false,
    'disclaimer': 'Decision support only. Not financial advice.',
  },
};

/// Calibrated analysis: probability present and flagged calibrated.
const analysisCalibrated = {
  'timestamp': '2026-10-16T09:00:00Z',
  'symbol': 'XAUUSD',
  'context': {
    'regime': 'TREND_UP',
    'h4_bias': 'UP',
    'm15_trigger': 'LONG',
    'mtf_agreement': 0.8,
    'volatility_state': 'NORMAL',
  },
  'signal': {
    'direction': 'UP',
    'horizon': 'H4',
    'probability': 0.61,
    'probability_calibrated': true,
    'score': 0.58,
    'confidence_lo': 0.55,
    'confidence_hi': 0.67,
    'model_version': 'v3',
    'features_contributing': <String>['trend'],
  },
  'levels': {
    'entry': 2650.5,
    'stop_loss': 2640.0,
    'take_profit': 2675.0,
    'reward_risk': 2.33,
    'suggested_risk_pct': 1.0,
    'sl_method': null,
    'tp_method': null,
  },
  'meta': {
    'coverage_tier': 'medium',
    'data_freshness_sec': 120,
    'degraded': false,
    'score_is_probability': false,
    'disclaimer': 'Decision support only. Not financial advice.',
  },
};

const newsList = {
  'news': [
    {
      'article': {
        'article_id': 'a1',
        'headline': 'الذهب يرتفع مع تراجع الدولار',
        'source': 'Reuters',
        'url': 'https://example.com/a1',
        'category': 'markets',
        'published_at': 1760000000,
        'published_at_iso': '2026-10-16T08:00:00Z',
        'collected_at': 1760000100,
        'gold_relevance': 'high',
        'body_available': false,
        'body_text': null,
      },
      'analysis_status': 'ok',
    },
    {
      'article': {
        'article_id': 'a2',
        'headline': 'بيانات التضخم الأمريكية',
        'source': 'Bloomberg',
        'url': 'https://example.com/a2',
        'category': 'macro',
        'published_at': 1760000000,
        'published_at_iso': '2026-10-16T07:00:00Z',
        'collected_at': 1760000100,
        'gold_relevance': 'medium',
        'body_available': false,
        'body_text': null,
      },
      'analysis_status': 'none',
    },
  ],
};

const newsDetail = {
  'article': {
    'article_id': 'a1',
    'headline': 'الذهب يرتفع مع تراجع الدولار',
    'source': 'Reuters',
    'url': 'https://example.com/a1',
    'category': 'markets',
    'published_at': 1760000000,
    'published_at_iso': '2026-10-16T08:00:00Z',
    'collected_at': 1760000100,
    'gold_relevance': 'high',
    'body_available': false,
    'body_text': null,
  },
  'has_analysis': true,
  'analysis': {
    'article_id': 'a1',
    'status': 'ok',
    'status_reason': '',
    'summary_ar': 'تراجع الدولار يدعم الذهب.',
    'facts': [
      {'text': 'انخفض مؤشر الدولار', 'kind': 'fact'},
    ],
    'market_implications_ar': ['ضغط صاعد على الذهب'],
    'supporting_evidence_ar': ['ضعف الدولار'],
    'opposing_evidence_ar': ['بيانات التضخم'],
    'gold_impact_ar': 'إيجابي',
    'direction': 'bullish',
    'levels': {
      'entry': 2650.0,
      'stop_loss': 2640.0,
      'take_profit': 2675.0,
    },
    'astra_agreement': 'agree',
    'disagreement_note_ar': '',
    'invalidating_conditions_ar': 'إغلاق تحت الدعم',
    'limitations_ar': ['تحليل خبر واحد'],
    'provider_id': 'llm7',
    'model_id': 'gpt-x',
    'analyzed_at': 1760000300,
    'timeframe_context': 'M15,H4',
    'candle_as_of': 1760000000,
  },
};

const newsDetailNoAnalysis = {
  'article': {
    'article_id': 'a2',
    'headline': 'بيانات التضخم الأمريكية',
    'source': 'Bloomberg',
    'url': 'https://example.com/a2',
    'category': 'macro',
    'published_at': 1760000000,
    'published_at_iso': '2026-10-16T07:00:00Z',
    'collected_at': 1760000100,
    'gold_relevance': 'medium',
    'body_available': false,
    'body_text': null,
  },
  'has_analysis': false,
  'analysis': null,
};

/// A detail carrying the news-to-open-trade linkage: one supportive trade and
/// one contradicting trade.
const newsDetailWithImpacts = {
  'article': {
    'article_id': 'a3',
    'headline': 'تراجع الدولار يدعم الذهب',
    'source': 'Reuters',
    'url': 'https://example.com/a3',
    'category': 'macro',
    'published_at': 1760000000,
    'published_at_iso': '2026-10-16T08:00:00Z',
    'collected_at': 1760000100,
    'gold_relevance': 'high',
    'body_available': true,
    'body_text': 'نص الخبر',
  },
  'has_analysis': true,
  'analysis': {
    'article_id': 'a3',
    'status': 'ok',
    'direction': 'bullish',
    'levels': {'entry': 2650.0, 'stop_loss': 2640.0, 'take_profit': 2680.0},
  },
  'open_trade_impacts': [
    {
      'trade_id': 'SHORT-1',
      'source_label_ar': 'قصير المدى',
      'direction_label_ar': 'شراء',
      'relation': 'supportive',
      'relation_label_ar': 'متوافق مع الخبر',
      'explanation_ar': 'اتجاه الخبر يتفق مع اتجاه الصفقة.',
    },
    {
      'trade_id': 'LONG-1',
      'source_label_ar': 'بعيد المدى',
      'direction_label_ar': 'بيع',
      'relation': 'contradicting',
      'relation_label_ar': 'متعارض مع الخبر',
      'explanation_ar': 'اتجاه الخبر يخالف اتجاه الصفقة.',
    },
  ],
};

const historyList = {
  'history': [
    {
      'headline': 'الذهب يرتفع مع تراجع الدولار',
      'source': 'Reuters',
      'analysis': {
        'article_id': 'a1',
        'status': 'ok',
        'status_reason': '',
        'summary_ar': 'تراجع الدولار يدعم الذهب.',
        'facts': <Map<String, Object?>>[],
        'market_implications_ar': <String>[],
        'supporting_evidence_ar': <String>[],
        'opposing_evidence_ar': <String>[],
        'gold_impact_ar': '',
        'direction': 'bullish',
        'levels': {'entry': null, 'stop_loss': null, 'take_profit': null},
        'astra_agreement': 'agree',
        'disagreement_note_ar': '',
        'invalidating_conditions_ar': '',
        'limitations_ar': <String>[],
        'provider_id': 'llm7',
        'model_id': 'gpt-x',
        'analyzed_at': 1760000300,
        'timeframe_context': 'M15,H4',
        'candle_as_of': 1760000000,
      },
    },
  ],
};

/// `GET /api/v1/news/results` — the news-results log: an analysis joined to its
/// article's full source metadata. The second row has `article_available` false
/// (the article row was not retained) and must render as unavailable.
const newsResults = {
  'results': [
    {
      'article': {
        'article_id': 'a1',
        'headline': 'الذهب يرتفع مع تراجع الدولار',
        'source': 'Reuters',
        'url': 'https://example.com/a1',
        'category': 'monetary_policy',
        'published_at': 1760000000,
        'published_at_iso': '2026-10-16T08:00:00Z',
        'gold_relevance': 'high',
        'body_available': false,
        'body_text': null,
      },
      'article_available': true,
      'analysis': {
        'article_id': 'a1',
        'status': 'ok',
        'summary_ar': 'تراجع الدولار يدعم الذهب.',
        'direction': 'bullish',
        'gold_channel': 'usd_rates',
        'impact_horizon': 'short',
        'levels': {'entry': 2650.0, 'stop_loss': 2640.0, 'take_profit': 2675.0},
        'astra_agreement': 'agree',
        'analyzed_at': 1760000300,
      },
    },
    {
      'article': <String, Object?>{},
      'article_available': false,
      'analysis': {
        'article_id': 'gone',
        'status': 'ok',
        'direction': 'no_trade',
        'gold_channel': 'unknown',
        'impact_horizon': 'unknown',
        'levels': {'entry': null, 'stop_loss': null, 'take_profit': null},
        'analyzed_at': 1760000400,
      },
    },
  ],
};

// ---- Macro-event payloads (shapes taken from the v1 /events contract) ------

/// An upcoming gold-relevant event whose expectation turn exists but whose
/// result has not run: `has_result` false, `released_at` null.
const eventsUpcoming = {
  'events': [
    {
      'event': {
        'event_key': 'faireconomy|USD|US Non-Farm Payrolls|1793170800',
        'title': 'US Non-Farm Payrolls',
        'country': 'USD',
        'impact': 'high',
        'scheduled_at': 1793170800,
        'scheduled_at_iso': '2026-10-28T07:00:00Z',
        'forecast': '150K',
        'previous': '120K',
        'source': 'faireconomy',
        'stage': 'prepared',
        'gold_relevant': true,
        'first_seen': 1793100000,
        'updated_at': 1793104200,
        'prepared_at': 1793104200,
        'prepare_status': 'ok',
        'released_at': null,
        'release_status': '',
      },
      'stage': 'prepared',
      'has_expectation': true,
      'has_result': false,
    },
    {
      'event': {
        'event_key': 'faireconomy|USD|US FOMC Statement|1793174400',
        'title': 'US FOMC Statement',
        'country': 'USD',
        'impact': 'high',
        'scheduled_at': 1793174400,
        'scheduled_at_iso': '2026-10-28T08:00:00Z',
        // The feed published no consensus: both stay null, never "0".
        'forecast': null,
        'previous': null,
        'source': 'faireconomy',
        'stage': 'scheduled',
        'gold_relevant': true,
        'first_seen': 1793100000,
        'updated_at': 1793100000,
        'prepared_at': null,
        'prepare_status': '',
        'released_at': null,
        'release_status': '',
      },
      'stage': 'scheduled',
      'has_expectation': false,
      'has_result': false,
    },
  ],
};

/// A released event carrying both turns.
const eventDetail = {
  'event': {
    'event_key': 'faireconomy|USD|US CPI m/m|1793104200',
    'title': 'US CPI m/m',
    'country': 'USD',
    'impact': 'high',
    'scheduled_at': 1793104200,
    'scheduled_at_iso': '2026-10-27T12:30:00Z',
    'forecast': '0.3%',
    'previous': '0.2%',
    'source': 'faireconomy',
    'stage': 'released',
    'gold_relevant': true,
    'first_seen': 1793000000,
    'updated_at': 1793104500,
    'prepared_at': 1793102400,
    'prepare_status': 'ok',
    'released_at': 1793104500,
    'release_status': 'ok',
  },
  'has_expectation': true,
  'expectation': {
    'article_id': 'faireconomy|USD|US CPI m/m|1793104200',
    'status': 'ok',
    'status_reason': '',
    'summary_ar': 'يتوقع السوق تسارع التضخم قليلاً.',
    'facts': [
      {'text': 'التوقع 0.3% مقابل 0.2% سابقاً', 'kind': 'fact'},
    ],
    'market_implications_ar': ['تضخم أعلى قد يرفع الدولار ويضغط على الذهب'],
    'supporting_evidence_ar': ['اتجاه التضخم صاعد'],
    'opposing_evidence_ar': <String>[],
    'gold_impact_ar': 'سلبي محتمل',
    'direction': 'bearish',
    'levels': {'entry': null, 'stop_loss': null, 'take_profit': null},
    'astra_agreement': 'unknown',
    'disagreement_note_ar': '',
    'invalidating_conditions_ar': 'تضخم أقل من المتوقع',
    'limitations_ar': <String>[],
    'provider_id': 'mock',
    'model_id': 'mock-model',
    'analyzed_at': 1793102400,
    'timeframe_context': 'M15,H4',
    'candle_as_of': null,
  },
  'has_result': true,
  'result': {
    'article_id': 'faireconomy|USD|US CPI m/m|1793104200',
    'status': 'ok',
    'status_reason': '',
    'summary_ar': 'جاء التضخم مطابقاً للتوقعات.',
    'facts': [
      {'text': 'التضخم 0.3%', 'kind': 'fact'},
    ],
    'market_implications_ar': <String>[],
    'supporting_evidence_ar': <String>[],
    'opposing_evidence_ar': <String>[],
    'gold_impact_ar': '',
    'direction': 'no_trade',
    'levels': {'entry': null, 'stop_loss': null, 'take_profit': null},
    'astra_agreement': 'unknown',
    'disagreement_note_ar': '',
    'invalidating_conditions_ar': '',
    'limitations_ar': ['النتيجة مطابقة للتوقع'],
    'provider_id': 'mock',
    'model_id': 'mock-model',
    'analyzed_at': 1793104500,
    'timeframe_context': 'M15,H4',
    'candle_as_of': null,
  },
};

// ---- Ecosystem payloads (shapes taken from the /ayzr/ecosystem routes) -----

/// A session snapshot with one open, one closed and one unavailable zone.
const ecoSessionsPayload = {
  'asOfUtc': 1793104200,
  'londonNewYorkOverlap': true,
  'openSessionIds': ['london'],
  'unavailableZones': ['Australia/Sydney'],
  'sessions': [
    {
      'id': 'london',
      'nameAr': 'جلسة لندن',
      'zoneId': 'Europe/London',
      'phase': 'OPEN',
      'phaseAr': 'مفتوحة',
      'localTime': '12:30',
      'utcOffsetSeconds': 3600,
      'inDst': true,
      'nextOpenUtc': 1793128800,
      'nextCloseUtc': 1793150400,
      'secondsToChange': 1800,
      'error': '',
    },
    {
      'id': 'tokyo',
      'nameAr': 'جلسة طوكيو',
      'zoneId': 'Asia/Tokyo',
      'phase': 'CLOSED',
      'phaseAr': 'مغلقة',
      'localTime': '21:30',
      'utcOffsetSeconds': 32400,
      'inDst': false,
      'nextOpenUtc': null,
      'nextCloseUtc': null,
      'secondsToChange': null,
      'error': '',
    },
    {
      'id': 'sydney',
      'nameAr': 'جلسة سيدني',
      'zoneId': 'Australia/Sydney',
      'phase': 'CLOSED',
      'phaseAr': 'غير متاح',
      'localTime': null,
      'utcOffsetSeconds': null,
      'inDst': false,
      'nextOpenUtc': null,
      'nextCloseUtc': null,
      'secondsToChange': null,
      'error': 'no tzdata for zone',
    },
  ],
};

/// The engine registry: five engines, one UNAVAILABLE.
const ecoEnginesPayload = {
  'contractSchemaVersion': 1,
  'engines': [
    {
      'engineId': 'trend-following-M15',
      'version': '1.0.0',
      'strategyFamily': 'TREND_FOLLOWING',
      'strategyFamilyAr': 'تتبّع الاتجاه',
      'timeframe': 'M15',
      'health': 'HEALTHY',
      'healthAr': 'سليم',
      'noteAr': '',
    },
    {
      'engineId': 'microstructure-M15',
      'version': '1.0.0',
      'strategyFamily': 'MICROSTRUCTURE',
      'strategyFamilyAr': 'بنية السوق',
      'timeframe': 'M15',
      'health': 'UNAVAILABLE',
      'healthAr': 'غير متاح',
      'noteAr': 'لا يتوفر مصدر موثوق لبيانات بنية السوق بعد.',
    },
  ],
};

/// A cycle with one PUBLISH_WITH_CAUTION opportunity and a conflict.
const ecoOpportunitiesPayload = {
  'asOfUtc': 1793104200,
  'correlationId': 'cycle-1793104200',
  'engineFailures': <String>[],
  'dataQualityWarnings': ['جودة البيانات غير كافية للنشر دون تحفّظ'],
  'opportunities': [
    {
      'opportunityId': 'op-XAUUSD-M15-LONG-1793102400',
      'symbol': 'XAUUSD',
      'timeframe': 'M15',
      'direction': 'LONG',
      'directionAr': 'شراء',
      'anchorBarUtc': 1793102400,
      'createdAt': 1793104200,
      'updatedAt': 1793104200,
      'expiresAt': null,
      'state': 'PUBLISH_WITH_CAUTION',
      'stateAr': 'نشر مع تحفّظ',
      'rationaleAr': 'توافق مرجّح مع تعارض مسجّل.',
      'entry': {'price': 2650.5, 'source': 'trend-following-M15', 'noteAr': ''},
      'stopLoss': {'price': 2644.0, 'source': 'trend-following-M15', 'noteAr': ''},
      'takeProfits': [
        {'price': 2660.0, 'source': 'trend-following-M15', 'noteAr': ''},
      ],
      'levelSourceEngine': 'trend-following-M15',
      'agreeingEngines': ['trend-following-M15', 'breakout-M15'],
      'disagreeingEngines': ['mean-reversion-M15'],
      'conflictNotesAr': ['محرّك الارتداد يعارض الاتجاه'],
      'riskFlags': ['conflict'],
      'astraCorePresent': true,
      'hasConflict': true,
      'contributingEvents': [
        {
          'eventId': 'evt-1',
          'opportunityId': 'op-XAUUSD-M15-LONG-1793102400',
          'engineId': 'trend-following-M15',
          'engineVersion': '1.0.0',
          'strategyFamily': 'TREND_FOLLOWING',
          'strategyFamilyAr': 'تتبّع الاتجاه',
          'eventType': 'setup',
          'symbol': 'XAUUSD',
          'direction': 'LONG',
          'directionAr': 'شراء',
          'timeframe': 'M15',
          'createdAt': 1793104200,
          'updatedAt': 1793104200,
          'expiresAt': null,
          'dataTimestamp': 1793102400,
          'inputSnapshotReference': 'snap-1',
          'dataSource': 'store',
          'dataQuality': 'VALID',
          'status': 'PUBLISH',
          'statusAr': 'منشور',
          'entry': {'price': 2650.5, 'source': 'trend-following-M15', 'noteAr': ''},
          'stopLoss': {'price': 2644.0, 'source': 'trend-following-M15', 'noteAr': ''},
          'takeProfits': <Map<String, Object?>>[],
          'reasonCodes': ['ema_stack'],
          'explanationAr': 'ترتيب المتوسطات يدعم الاتجاه.',
          'supportingEvidence': [
            {'code': 'ema_stack', 'textAr': 'المتوسطات مرتّبة صعوداً', 'value': 3.0},
          ],
          'contradictingEvidence': <Map<String, Object?>>[],
          'riskFlags': <String>[],
          'confidence': {
            'calibrated': false,
            'probability': null,
            'calibrationRef': '',
            'score': 0.72,
            'scoreKind': 'heuristic',
          },
          'correlationId': 'cycle-1793104200',
          'immutableCoreOutput': false,
        },
      ],
    },
  ],
};

/// A ledger page with two append-only rows.
const ecoLedgerPayload = {
  'count': 2,
  'entries': [
    {
      'seq': 1,
      'atUtc': 1793104200,
      'type': 'CYCLE',
      'typeAr': 'دورة تحليل',
      'correlationId': 'cycle-1793104200',
      'opportunityId': '',
      'engineId': '',
      'reasonCode': 'cycle_complete',
      'detailAr': 'اكتملت الدورة',
      'payloadJson': '{}',
    },
    {
      'seq': 2,
      'atUtc': 1793104200,
      'type': 'DECISION',
      'typeAr': 'قرار',
      'correlationId': 'cycle-1793104200',
      'opportunityId': 'op-XAUUSD-M15-LONG-1793102400',
      'engineId': 'orchestrator',
      'reasonCode': 'publish_with_caution',
      'detailAr': 'نشر مع تحفّظ بسبب تعارض',
      'payloadJson': '{}',
    },
  ],
};
