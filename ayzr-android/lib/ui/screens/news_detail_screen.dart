import 'package:flutter/material.dart';

import '../../core/strings_ar.dart';
import '../../core/theme.dart';
import '../../data/models/news.dart';
import '../../state/app_state.dart';
import '../widgets/info_widgets.dart';
import '../widgets/state_panel.dart';

/// تفاصيل الخبر — loads `GET /api/v1/news/analyze?id=` on demand and renders the
/// Arabic analysis. The analysis `status` is the gate: only `ok` may present a
/// proposal; any other status shows the reason instead of levels.
class NewsDetailScreen extends StatefulWidget {
  const NewsDetailScreen({
    super.key,
    required this.state,
    required this.articleId,
    required this.preview,
  });

  final AppState state;
  final String articleId;
  final NewsArticle preview;

  @override
  State<NewsDetailScreen> createState() => _NewsDetailScreenState();
}

class _NewsDetailScreenState extends State<NewsDetailScreen> {
  Section<NewsDetail> _section = const Section<NewsDetail>();

  @override
  void initState() {
    super.initState();
    _load();
  }

  Future<void> _load() async {
    setState(() => _section = _section.loading());
    final r = await widget.state.loadArticle(widget.articleId);
    if (!mounted) return;
    setState(() {
      _section = r.isSuccess
          ? Section<NewsDetail>.ready(r.value!)
          : Section<NewsDetail>.failed(r.failure!);
    });
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(title: const Text(Ar.articleDetails)),
      body: SectionBuilder<NewsDetail>(
        section: _section,
        onRetry: _load,
        builder: (context, detail) => ListView(
          padding: const EdgeInsets.all(16),
          children: [
            _articleCard(detail.article),
            if (!detail.hasAnalysis || detail.analysis == null)
              const Padding(
                padding: EdgeInsets.symmetric(vertical: 24),
                child: StatePanel(
                  icon: Icons.article_outlined,
                  title: Ar.noAnalysisYet,
                  color: AyzrColors.textMuted,
                ),
              )
            else
              ..._analysisCards(detail.analysis!),
          ],
        ),
      ),
    );
  }

  Widget _articleCard(NewsArticle a) => SectionCard(
        title: Ar.headline,
        icon: Icons.newspaper_rounded,
        children: [
          Text(
            a.headline.isEmpty ? Ar.unavailable : a.headline,
            style: const TextStyle(
              color: AyzrColors.textPrimary,
              fontSize: 15,
              fontWeight: FontWeight.w700,
              height: 1.5,
            ),
          ),
          const SizedBox(height: 10),
          InfoRow(label: Ar.source, value: Fmt.text(a.source)),
          InfoRow(
            label: Ar.updatedAt,
            value: Fmt.isoToLocal(a.publishedAtIso),
          ),
          if (a.bodyAvailable && a.bodyText != null) ...[
            const Divider(height: 22),
            Text(
              a.bodyText!,
              style: const TextStyle(
                color: AyzrColors.textPrimary,
                fontSize: 13,
                height: 1.6,
              ),
            ),
          ],
        ],
      );

  List<Widget> _analysisCards(NewsAnalysis an) {
    final cards = <Widget>[];
    if (!an.isOk) {
      cards.add(SectionCard(
        title: Ar.analysisUnavailable,
        icon: Icons.info_outline_rounded,
        children: [
          Text(
            an.statusReason.isEmpty
                ? 'حالة التحليل: ${an.status}'
                : an.statusReason,
            style: const TextStyle(
              color: AyzrColors.warning,
              fontSize: 13,
              height: 1.5,
            ),
          ),
        ],
      ));
      return cards;
    }

    if (an.summaryAr.isNotEmpty) {
      cards.add(SectionCard(
        title: Ar.summary,
        icon: Icons.summarize_outlined,
        children: [
          Text(
            an.summaryAr,
            style: const TextStyle(
              color: AyzrColors.textPrimary,
              fontSize: 13.5,
              height: 1.6,
            ),
          ),
        ],
      ));
    }

    if (an.facts.isNotEmpty) {
      cards.add(SectionCard(
        title: Ar.facts,
        icon: Icons.fact_check_outlined,
        children: [
          for (final f in an.facts)
            Padding(
              padding: const EdgeInsets.symmetric(vertical: 4),
              child: Row(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  const Text('•  ',
                      style: TextStyle(color: AyzrColors.gold)),
                  Expanded(
                    child: Text(
                      f.text,
                      style: const TextStyle(
                        color: AyzrColors.textPrimary,
                        fontSize: 13,
                        height: 1.5,
                      ),
                    ),
                  ),
                ],
              ),
            ),
        ],
      ));
    }

    cards.add(SectionCard(
      title: Ar.signalSection,
      icon: Icons.insights_rounded,
      children: [
        Align(
          alignment: AlignmentDirectional.centerStart,
          child: DirectionChip(direction: an.direction),
        ),
        const SizedBox(height: 8),
        InfoRow(label: Ar.entry, value: Fmt.number(an.entry)),
        InfoRow(label: Ar.stopLoss, value: Fmt.number(an.stopLoss)),
        InfoRow(label: Ar.takeProfit, value: Fmt.number(an.takeProfit)),
      ],
    ));

    if (an.marketImplicationsAr.isNotEmpty) {
      cards.add(_bulletCard(
          Ar.marketImplications, Icons.trending_up_rounded, an.marketImplicationsAr));
    }
    if (an.supportingEvidenceAr.isNotEmpty) {
      cards.add(_bulletCard(
          Ar.supportingEvidence, Icons.check_circle_outline, an.supportingEvidenceAr));
    }
    if (an.opposingEvidenceAr.isNotEmpty) {
      cards.add(_bulletCard(
          Ar.opposingEvidence, Icons.cancel_outlined, an.opposingEvidenceAr));
    }
    if (an.goldImpactAr.isNotEmpty) {
      cards.add(SectionCard(
        title: Ar.goldImpact,
        icon: Icons.circle_outlined,
        children: [
          Text(
            an.goldImpactAr,
            style: const TextStyle(
              color: AyzrColors.textPrimary,
              fontSize: 13,
              height: 1.6,
            ),
          ),
        ],
      ));
    }
    if (an.invalidatingConditionsAr.isNotEmpty) {
      cards.add(SectionCard(
        title: Ar.invalidation,
        icon: Icons.gavel_rounded,
        children: [
          Text(
            an.invalidatingConditionsAr,
            style: const TextStyle(
              color: AyzrColors.textPrimary,
              fontSize: 13,
              height: 1.6,
            ),
          ),
        ],
      ));
    }
    if (an.limitationsAr.isNotEmpty) {
      cards.add(_bulletCard(Ar.limitations, Icons.warning_amber_rounded, an.limitationsAr));
    }
    cards.add(SectionCard(
      title: Ar.analysisStatus,
      icon: Icons.verified_outlined,
      children: [
        InfoRow(label: Ar.analysisStatus, value: an.status),
        InfoRow(label: Ar.astraAgreement, value: an.astraAgreement),
        InfoRow(label: Ar.timeframeContext, value: Fmt.text(an.timeframeContext)),
        InfoRow(label: Ar.updatedAt, value: _epoch(an.analyzedAtUtc)),
      ],
    ));
    return cards;
  }

  Widget _bulletCard(String title, IconData icon, List<String> items) =>
      SectionCard(
        title: title,
        icon: icon,
        children: [
          for (final s in items)
            Padding(
              padding: const EdgeInsets.symmetric(vertical: 4),
              child: Row(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  const Text('•  ',
                      style: TextStyle(color: AyzrColors.gold)),
                  Expanded(
                    child: Text(
                      s,
                      style: const TextStyle(
                        color: AyzrColors.textPrimary,
                        fontSize: 13,
                        height: 1.5,
                      ),
                    ),
                  ),
                ],
              ),
            ),
        ],
      );

  String _epoch(int? seconds) {
    if (seconds == null) return Fmt.unavailable;
    return Fmt.isoToLocal(
        DateTime.fromMillisecondsSinceEpoch(seconds * 1000, isUtc: true)
            .toIso8601String());
  }
}
