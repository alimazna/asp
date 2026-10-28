import 'package:flutter/material.dart';

import '../../core/strings_ar.dart';
import '../../core/theme.dart';
import '../../data/models/news.dart';
import 'info_widgets.dart';

/// Renders a [NewsAnalysis] as the Arabic analysis cards.
///
/// Shared by the news-detail and event-detail screens: the backend emits the
/// same analysis shape for an article, an event expectation and an event result,
/// so the presentation is defined once. The `status` gate lives here: only `ok`
/// may present a proposal; any other status shows the reason instead of levels.
class AnalysisView extends StatelessWidget {
  const AnalysisView({super.key, required this.analysis});

  final NewsAnalysis analysis;

  @override
  Widget build(BuildContext context) {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: _cards(analysis),
    );
  }
}

/// ISO label for an epoch-seconds timestamp; "غير متاح" when absent.
String epochToLocal(int? seconds) {
  if (seconds == null) return Fmt.unavailable;
  return Fmt.isoToLocal(
      DateTime.fromMillisecondsSinceEpoch(seconds * 1000, isUtc: true)
          .toIso8601String());
}

/// Arabic label for the model's gold-relevance value. An unrecognised value is
/// shown as the neutral "غير محدَّد" rather than a friendlier-but-wrong label.
String goldRelevanceLabel(String v) => switch (v) {
      'high' => Ar.relHigh,
      'medium' => Ar.relMedium,
      'low' => Ar.relLow,
      'none' => Ar.relNone,
      _ => Ar.relUnknown,
    };

List<Widget> _cards(NewsAnalysis an) {
  final cards = <Widget>[];
  if (!an.isOk) {
    cards.add(SectionCard(
      title: Ar.analysisUnavailable,
      icon: Icons.info_outline_rounded,
      children: [
        Text(
          an.statusReason.isEmpty ? 'حالة التحليل: ${an.status}' : an.statusReason,
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
                const Text('•  ', style: TextStyle(color: AyzrColors.gold)),
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
  // The model's own classification. Rendered even when it is "unknown", so an
  // unanswered field is visible as unanswered rather than silently absent.
  cards.add(SectionCard(
    title: Ar.aiGoldRelevance,
    icon: Icons.auto_awesome_outlined,
    children: [
      Text(
        goldRelevanceLabel(an.aiGoldRelevance),
        style: const TextStyle(
          color: AyzrColors.textPrimary,
          fontSize: 13,
          fontWeight: FontWeight.w700,
        ),
      ),
      const SizedBox(height: 4),
      const Text(
        Ar.aiGoldRelevanceHint,
        style: TextStyle(
          color: AyzrColors.textMuted,
          fontSize: 11.5,
          height: 1.5,
        ),
      ),
    ],
  ));
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
    cards.add(
        _bulletCard(Ar.limitations, Icons.warning_amber_rounded, an.limitationsAr));
  }
  cards.add(SectionCard(
    title: Ar.analysisStatus,
    icon: Icons.verified_outlined,
    children: [
      InfoRow(label: Ar.analysisStatus, value: an.status),
      InfoRow(label: Ar.astraAgreement, value: an.astraAgreement),
      InfoRow(label: Ar.timeframeContext, value: Fmt.text(an.timeframeContext)),
      InfoRow(label: Ar.updatedAt, value: epochToLocal(an.analyzedAtUtc)),
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
                const Text('•  ', style: TextStyle(color: AyzrColors.gold)),
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
