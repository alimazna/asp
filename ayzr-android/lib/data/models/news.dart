/// Domain models for the AYZR news-intelligence surface (`/api/v1/news*`).
///
/// The backend emits Arabic analysis fields (`summary_ar`, `gold_impact_ar`,
/// ...) plus a `status` gate. A status other than `ok` means **no proposal may
/// be shown**; the UI renders the reason instead of levels.
library;

double? _d(Object? v) {
  if (v is num) return v.toDouble();
  if (v is String) return double.tryParse(v);
  return null;
}

int? _i(Object? v) {
  if (v is int) return v;
  if (v is num) return v.toInt();
  if (v is String) return int.tryParse(v);
  return null;
}

String _s(Object? v, [String fallback = '']) =>
    v is String ? v : fallback;

List<String> _strList(Object? v) => v is List
    ? v.whereType<String>().toList(growable: false)
    : const <String>[];

class NewsArticle {
  const NewsArticle({
    required this.articleId,
    required this.headline,
    required this.source,
    required this.url,
    required this.category,
    required this.publishedAtUtc,
    required this.publishedAtIso,
    required this.goldRelevance,
    required this.bodyAvailable,
    required this.bodyText,
  });

  final String articleId;
  final String headline;
  final String source;
  final String url;
  final String category;
  final int? publishedAtUtc;
  final String? publishedAtIso;
  final String goldRelevance;
  final bool bodyAvailable;
  final String? bodyText;

  static NewsArticle fromJson(Map<String, dynamic> j) => NewsArticle(
        articleId: _s(j['article_id']),
        headline: _s(j['headline']),
        source: _s(j['source']),
        url: _s(j['url']),
        category: _s(j['category'], 'other'),
        publishedAtUtc: _i(j['published_at']),
        publishedAtIso: j['published_at_iso'] is String
            ? j['published_at_iso'] as String
            : null,
        goldRelevance: _s(j['gold_relevance'], 'unknown'),
        bodyAvailable: j['body_available'] == true,
        bodyText: j['body_text'] is String ? j['body_text'] as String : null,
      );
}

class NewsFact {
  const NewsFact({required this.text, required this.kind});

  final String text;
  final String kind;

  static NewsFact fromJson(Map<String, dynamic> j) =>
      NewsFact(text: _s(j['text']), kind: _s(j['kind']));
}

class NewsAnalysis {
  const NewsAnalysis({
    required this.articleId,
    required this.status,
    required this.statusReason,
    required this.summaryAr,
    required this.facts,
    required this.marketImplicationsAr,
    required this.supportingEvidenceAr,
    required this.opposingEvidenceAr,
    required this.goldImpactAr,
    required this.direction,
    required this.entry,
    required this.stopLoss,
    required this.takeProfit,
    required this.astraAgreement,
    required this.disagreementNoteAr,
    required this.invalidatingConditionsAr,
    required this.limitationsAr,
    required this.providerId,
    required this.modelId,
    required this.analyzedAtUtc,
    required this.timeframeContext,
    required this.candleAsOfUtc,
  });

  final String articleId;
  final String status;
  final String statusReason;
  final String summaryAr;
  final List<NewsFact> facts;
  final List<String> marketImplicationsAr;
  final List<String> supportingEvidenceAr;
  final List<String> opposingEvidenceAr;
  final String goldImpactAr;
  final String direction;
  final double? entry;
  final double? stopLoss;
  final double? takeProfit;
  final String astraAgreement;
  final String disagreementNoteAr;
  final String invalidatingConditionsAr;
  final List<String> limitationsAr;
  final String providerId;
  final String modelId;
  final int? analyzedAtUtc;
  final String timeframeContext;
  final int? candleAsOfUtc;

  /// The status gate: only `ok` may present a directional proposal/levels.
  bool get isOk => status == 'ok';

  static NewsAnalysis fromJson(Map<String, dynamic> j) {
    final levels = (j['levels'] as Map?)?.cast<String, dynamic>() ?? const {};
    return NewsAnalysis(
      articleId: _s(j['article_id']),
      status: _s(j['status'], 'unavailable'),
      statusReason: _s(j['status_reason']),
      summaryAr: _s(j['summary_ar']),
      facts: (j['facts'] is List)
          ? (j['facts'] as List)
              .whereType<Map>()
              .map((m) => NewsFact.fromJson(m.cast<String, dynamic>()))
              .toList(growable: false)
          : const <NewsFact>[],
      marketImplicationsAr: _strList(j['market_implications_ar']),
      supportingEvidenceAr: _strList(j['supporting_evidence_ar']),
      opposingEvidenceAr: _strList(j['opposing_evidence_ar']),
      goldImpactAr: _s(j['gold_impact_ar']),
      direction: _s(j['direction'], 'no_trade'),
      entry: _d(levels['entry']),
      stopLoss: _d(levels['stop_loss']),
      takeProfit: _d(levels['take_profit']),
      astraAgreement: _s(j['astra_agreement'], 'unknown'),
      disagreementNoteAr: _s(j['disagreement_note_ar']),
      invalidatingConditionsAr: _s(j['invalidating_conditions_ar']),
      limitationsAr: _strList(j['limitations_ar']),
      providerId: _s(j['provider_id']),
      modelId: _s(j['model_id']),
      analyzedAtUtc: _i(j['analyzed_at']),
      timeframeContext: _s(j['timeframe_context']),
      candleAsOfUtc: _i(j['candle_as_of']),
    );
  }
}

/// One row of `GET /api/v1/news`.
class NewsListItem {
  const NewsListItem({required this.article, required this.analysisStatus});

  final NewsArticle article;
  final String analysisStatus;

  bool get isAnalyzed => analysisStatus == 'ok' || analysisStatus == 'analyzed';

  static NewsListItem fromJson(Map<String, dynamic> j) => NewsListItem(
        article: NewsArticle.fromJson(
            (j['article'] as Map?)?.cast<String, dynamic>() ?? const {}),
        analysisStatus: _s(j['analysis_status'], 'none'),
      );
}

/// The full payload of `GET /api/v1/news/analyze?id=`.
class NewsDetail {
  const NewsDetail({
    required this.article,
    required this.hasAnalysis,
    required this.analysis,
  });

  final NewsArticle article;
  final bool hasAnalysis;
  final NewsAnalysis? analysis;

  static NewsDetail fromJson(Map<String, dynamic> j) => NewsDetail(
        article: NewsArticle.fromJson(
            (j['article'] as Map?)?.cast<String, dynamic>() ?? const {}),
        hasAnalysis: j['has_analysis'] == true,
        analysis: (j['analysis'] is Map)
            ? NewsAnalysis.fromJson(
                (j['analysis'] as Map).cast<String, dynamic>())
            : null,
      );
}

/// One row of `GET /api/v1/history`.
class HistoryEntry {
  const HistoryEntry({
    required this.headline,
    required this.source,
    required this.analysis,
  });

  final String headline;
  final String source;
  final NewsAnalysis analysis;

  static HistoryEntry fromJson(Map<String, dynamic> j) => HistoryEntry(
        headline: _s(j['headline']),
        source: _s(j['source']),
        analysis: NewsAnalysis.fromJson(
            (j['analysis'] as Map?)?.cast<String, dynamic>() ?? const {}),
      );
}
