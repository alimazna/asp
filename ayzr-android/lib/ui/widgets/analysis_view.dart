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

/// Arabic label for the model's gold-transmission channel. Closed vocabulary:
/// an unrecognised value is shown as "غير محدَّد", never a friendlier guess.
String goldChannelLabel(String v) => switch (v) {
      'monetary_policy' => Ar.channelMonetaryPolicy,
      'inflation' => Ar.channelInflation,
      'usd_rates' => Ar.channelUsdRates,
      'safe_haven' => Ar.channelSafeHaven,
      'physical_demand' => Ar.channelPhysicalDemand,
      'positioning' => Ar.channelPositioning,
      'other' => Ar.channelOther,
      _ => Ar.channelUnknown,
    };

/// Arabic label for the model's impact horizon. Closed vocabulary, same rule.
String impactHorizonLabel(String v) => switch (v) {
      'immediate' => Ar.horizonImmediate,
      'short' => Ar.horizonShort,
      'medium' => Ar.horizonMedium,
      'long' => Ar.horizonLong,
      _ => Ar.horizonUnknown,
    };

/// Arabic label for the tone of the coverage. Closed vocabulary: an
/// unrecognised value is "غير محدَّدة", never a friendlier guess.
String toneLabel(String v) => switch (v) {
      'very_negative' => Ar.toneVeryNegative,
      'negative' => Ar.toneNegative,
      'neutral' => Ar.toneNeutral,
      'positive' => Ar.tonePositive,
      'very_positive' => Ar.toneVeryPositive,
      _ => Ar.toneUnknown,
    };

/// Colour for the tone chip. Tone is a property of the writing, not a trading
/// signal, so the palette is muted rather than the up/down market colours: the
/// reader must not read "negative tone" as "sell".
Color toneColor(String v) => switch (v) {
      'very_negative' => AyzrColors.down,
      'negative' => AyzrColors.down.withOpacity(0.7),
      'positive' => AyzrColors.up.withOpacity(0.7),
      'very_positive' => AyzrColors.up,
      _ => AyzrColors.textMuted,
    };

String toneStrengthLabel(String v) => switch (v) {
      'weak' => Ar.strengthWeak,
      'moderate' => Ar.strengthModerate,
      'strong' => Ar.strengthStrong,
      _ => Ar.strengthUnknown,
    };

String sourceRegisterLabel(String v) => switch (v) {
      'official' => Ar.registerOfficial,
      'wire' => Ar.registerWire,
      'analytical' => Ar.registerAnalytical,
      'opinion' => Ar.registerOpinion,
      'other' => Ar.registerOther,
      _ => Ar.registerUnknown,
    };

String sensationalismLabel(String v) => switch (v) {
      'none' => Ar.sensationalismNone,
      'low' => Ar.sensationalismLow,
      'medium' => Ar.sensationalismMedium,
      'high' => Ar.sensationalismHigh,
      _ => Ar.sensationalismUnknown,
    };

String noveltyLabel(String v) => switch (v) {
      'new' => Ar.noveltyNew,
      'partly_priced' => Ar.noveltyPartlyPriced,
      'already_priced' => Ar.noveltyAlreadyPriced,
      _ => Ar.noveltyUnknown,
    };

String uncertaintyLabel(String v) => switch (v) {
      'low' => Ar.uncertaintyLow,
      'medium' => Ar.uncertaintyMedium,
      'high' => Ar.uncertaintyHigh,
      _ => Ar.uncertaintyUnknown,
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
      // Absolute levels are shown only when the backend actually grounded them
      // against the real market. When the model could not know the price it
      // must not have invented one, so the fields are null and we render the
      // honest "not available" instead of a number.
      InfoRow(label: Ar.entry, value: Fmt.number(an.entry)),
      InfoRow(label: Ar.stopLoss, value: Fmt.number(an.stopLoss)),
      InfoRow(label: Ar.takeProfit, value: Fmt.number(an.takeProfit)),
      // Relative, price-free guidance: a distance in points is honest where a
      // price would be fabricated. Shown when the model supplied one.
      if (an.expectedMovePoints != null)
        InfoRow(
          label: Ar.expectedMove,
          value: Ar.pointsValue(an.expectedMovePoints!),
        ),
      if (an.stopDistancePoints != null)
        InfoRow(
          label: Ar.stopDistance,
          value: Ar.pointsValue(an.stopDistancePoints!),
        ),
      if (an.relativeLevelsNoteAr.isNotEmpty) ...[
        const SizedBox(height: 6),
        Text(
          an.relativeLevelsNoteAr,
          style: const TextStyle(
            color: AyzrColors.textMuted,
            fontSize: 12,
            height: 1.6,
          ),
        ),
      ],
      // When neither an absolute nor a relative level exists, say so plainly
      // rather than leaving the reader to guess why the numbers are missing.
      if (an.entry == null &&
          an.stopLoss == null &&
          an.takeProfit == null &&
          an.expectedMovePoints == null &&
          an.stopDistancePoints == null)
        const Padding(
          padding: EdgeInsets.only(top: 6),
          child: Text(
            Ar.levelsUnknown,
            style: TextStyle(
              color: AyzrColors.textMuted,
              fontSize: 12,
              height: 1.6,
            ),
          ),
        ),
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
  // The transmission channel and impact horizon: two closed-vocabulary
  // classifiers, rendered together so the reader sees "how" and "when" in one
  // place. Both are always shown, even as "غير محدَّد", so an unanswered field
  // is visible as unanswered rather than silently absent.
  cards.add(SectionCard(
    title: Ar.goldChannel,
    icon: Icons.route_outlined,
    children: [
      Text(
        goldChannelLabel(an.aiGoldChannel),
        style: const TextStyle(
          color: AyzrColors.textPrimary,
          fontSize: 13,
          fontWeight: FontWeight.w700,
        ),
      ),
      const SizedBox(height: 4),
      const Text(
        Ar.goldChannelHint,
        style: TextStyle(
          color: AyzrColors.textMuted,
          fontSize: 11.5,
          height: 1.5,
        ),
      ),
      const Divider(height: 22),
      Text(
        impactHorizonLabel(an.aiImpactHorizon),
        style: const TextStyle(
          color: AyzrColors.textPrimary,
          fontSize: 13,
          fontWeight: FontWeight.w700,
        ),
      ),
      const SizedBox(height: 4),
      const Text(
        Ar.impactHorizonHint,
        style: TextStyle(
          color: AyzrColors.textMuted,
          fontSize: 11.5,
          height: 1.5,
        ),
      ),
    ],
  ));
  // Tone, framing and subtext: how the story is TOLD. Rendered only when the
  // model actually classified something -- a card of five "غير محدَّد" rows
  // would be noise. The tone chip is the headline; the classifiers follow; the
  // subtext prose last. `toneCaveat` states plainly that none of this changes
  // the facts or the direction, so a strongly worded article is not read as a
  // stronger signal.
  final hasTone = an.tone != 'unknown' ||
      an.toneStrength != 'unknown' ||
      an.sourceRegister != 'unknown' ||
      an.sensationalism != 'unknown' ||
      an.novelty != 'unknown' ||
      an.uncertainty != 'unknown' ||
      an.hiddenContextAr.isNotEmpty ||
      an.framingBiasAr.isNotEmpty;
  if (hasTone) {
    cards.add(SectionCard(
      title: Ar.toneSection,
      icon: Icons.record_voice_over_outlined,
      children: [
        if (an.tone != 'unknown') ...[
          Row(
            children: [
              _ToneChip(
                label: toneLabel(an.tone),
                color: toneColor(an.tone),
              ),
              const SizedBox(width: 8),
              if (an.toneStrength != 'unknown')
                Text(
                  toneStrengthLabel(an.toneStrength),
                  style: const TextStyle(
                    color: AyzrColors.textMuted,
                    fontSize: 12,
                  ),
                ),
            ],
          ),
          const SizedBox(height: 4),
          const Text(
            Ar.toneHint,
            style: TextStyle(
              color: AyzrColors.textMuted,
              fontSize: 11.5,
              height: 1.5,
            ),
          ),
          if (an.toneEvidenceAr.isNotEmpty) ...[
            const SizedBox(height: 10),
            const Text(
              Ar.toneEvidenceLabel,
              style: TextStyle(
                color: AyzrColors.textMuted,
                fontSize: 11.5,
              ),
            ),
            const SizedBox(height: 4),
            Container(
              padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 8),
              decoration: BoxDecoration(
                color: AyzrColors.navyRaised,
                borderRadius: BorderRadius.circular(8),
                border: const Border(
                  right: BorderSide(color: AyzrColors.gold, width: 2.5),
                ),
              ),
              child: Text(
                an.toneEvidenceAr,
                style: const TextStyle(
                  color: AyzrColors.textPrimary,
                  fontSize: 12.5,
                  height: 1.6,
                ),
              ),
            ),
          ],
        ],
        if (an.sourceRegister != 'unknown' ||
            an.sensationalism != 'unknown') ...[
          const Divider(height: 22),
          if (an.sourceRegister != 'unknown')
            InfoRow(
              label: Ar.sourceRegisterLabel,
              value: sourceRegisterLabel(an.sourceRegister),
            ),
          if (an.sensationalism != 'unknown')
            InfoRow(
              label: Ar.sensationalismLabel,
              value: sensationalismLabel(an.sensationalism),
            ),
        ],
        if (an.novelty != 'unknown' || an.uncertainty != 'unknown') ...[
          const Divider(height: 22),
          if (an.novelty != 'unknown')
            InfoRow(
              label: Ar.noveltyLabel,
              value: noveltyLabel(an.novelty),
            ),
          if (an.uncertainty != 'unknown')
            InfoRow(
              label: Ar.uncertaintyLabel,
              value: uncertaintyLabel(an.uncertainty),
            ),
        ],
        if (an.hiddenContextAr.isNotEmpty) ...[
          const Divider(height: 22),
          _SubtextBlock(
            title: Ar.hiddenContextLabel,
            hint: Ar.hiddenContextHint,
            body: an.hiddenContextAr,
            icon: Icons.visibility_outlined,
          ),
        ],
        if (an.framingBiasAr.isNotEmpty) ...[
          const SizedBox(height: 14),
          _SubtextBlock(
            title: Ar.framingBiasLabel,
            hint: Ar.framingBiasHint,
            body: an.framingBiasAr,
            icon: Icons.alt_route_outlined,
          ),
        ],
        const Divider(height: 22),
        Text(
          Ar.toneCaveat,
          style: TextStyle(
            color: AyzrColors.warning.withOpacity(0.9),
            fontSize: 11,
            height: 1.5,
          ),
        ),
      ],
    ));
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

/// A small pill showing the tone label in its own muted colour. Tone is a
/// property of the writing, not a trading signal, so it never uses the
/// primary/secondary accents reserved for prices and directions.
class _ToneChip extends StatelessWidget {
  const _ToneChip({required this.label, required this.color});

  final String label;
  final Color color;

  @override
  Widget build(BuildContext context) {
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 4),
      decoration: BoxDecoration(
        color: color.withOpacity(0.14),
        borderRadius: BorderRadius.circular(12),
        border: Border.all(color: color.withOpacity(0.45)),
      ),
      child: Text(
        label,
        style: TextStyle(
          color: color,
          fontSize: 12,
          fontWeight: FontWeight.w700,
        ),
      ),
    );
  }
}

/// A titled block of subtext prose (hidden context / framing bias). Rendered as
/// its own labelled paragraph so it reads as interpretation, never as a fact
/// extracted from the article.
class _SubtextBlock extends StatelessWidget {
  const _SubtextBlock({
    required this.title,
    required this.hint,
    required this.body,
    required this.icon,
  });

  final String title;
  final String hint;
  final String body;
  final IconData icon;

  @override
  Widget build(BuildContext context) {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Row(
          children: [
            Icon(icon, size: 15, color: AyzrColors.goldSoft),
            const SizedBox(width: 6),
            Text(
              title,
              style: const TextStyle(
                color: AyzrColors.goldSoft,
                fontSize: 12.5,
                fontWeight: FontWeight.w700,
              ),
            ),
          ],
        ),
        const SizedBox(height: 4),
        Text(
          hint,
          style: const TextStyle(
            color: AyzrColors.textMuted,
            fontSize: 11,
            height: 1.5,
          ),
        ),
        const SizedBox(height: 6),
        Text(
          body,
          style: const TextStyle(
            color: AyzrColors.textPrimary,
            fontSize: 13,
            height: 1.65,
          ),
        ),
      ],
    );
  }
}
