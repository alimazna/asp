import 'package:flutter/material.dart';

import '../../core/strings_ar.dart';
import '../../core/theme.dart';
import '../../data/models/analysis.dart';
import '../../state/app_state.dart';
import '../widgets/info_widgets.dart';
import '../widgets/state_panel.dart';

/// التحليل — the full decision-support view: market context, signal, suggested
/// levels and metadata. Levels are shown only as *suggestions* and every absent
/// field is rendered "غير متاح". RULE C governs the probability/score display.
class AnalysisScreen extends StatelessWidget {
  const AnalysisScreen({super.key, required this.state});

  final AppState state;

  @override
  Widget build(BuildContext context) {
    return RefreshIndicator(
      onRefresh: state.loadAnalysis,
      child: SectionBuilder<AnalysisLatest>(
        section: state.analysis,
        onRetry: state.loadAnalysis,
        builder: (context, a) => ListView(
          padding: const EdgeInsets.all(16),
          children: [
            if (a.meta.degraded) NoticeBanner.degraded(),
            if (state.isStale) NoticeBanner.stale(),
            _contextCard(a),
            _signalCard(a),
            _levelsCard(a),
            _metaCard(a),
          ],
        ),
      ),
    );
  }

  Widget _contextCard(AnalysisLatest a) => SectionCard(
        title: Ar.contextSection,
        icon: Icons.travel_explore_rounded,
        children: [
          InfoRow(label: Ar.regime, value: a.context.regime),
          InfoRow(label: Ar.h4Bias, value: a.context.h4Bias),
          InfoRow(label: Ar.m15Trigger, value: a.context.m15Trigger),
          InfoRow(
            label: Ar.mtfAgreement,
            value: Fmt.number(a.context.mtfAgreement, decimals: 2),
          ),
          InfoRow(label: Ar.volatility, value: a.context.volatilityState),
        ],
      );

  Widget _signalCard(AnalysisLatest a) {
    final s = a.signal;
    return SectionCard(
      title: Ar.signalSection,
      icon: Icons.insights_rounded,
      children: [
        Padding(
          padding: const EdgeInsets.only(bottom: 8),
          child: Align(
            alignment: AlignmentDirectional.centerStart,
            child: DirectionChip(direction: s.direction),
          ),
        ),
        // RULE C: a calibrated probability is shown as a probability; otherwise
        // the raw value is shown as a score with an explicit caveat.
        if (s.probabilityCalibrated && s.probability != null)
          InfoRow(
            label: Ar.probability,
            value: Fmt.pct(s.probability),
            color: AyzrColors.goldSoft,
          )
        else
          InfoRow(label: Ar.score, value: Fmt.number(s.score, decimals: 3)),
        InfoRow(
          label: Ar.mtfAgreement,
          value: Fmt.number(a.context.mtfAgreement, decimals: 2),
        ),
        if (!s.probabilityCalibrated)
          const Padding(
            padding: EdgeInsets.only(top: 6),
            child: Text(
              Ar.scoreNotProbability,
              style: TextStyle(
                color: AyzrColors.textMuted,
                fontSize: 12,
                height: 1.4,
              ),
            ),
          ),
      ],
    );
  }

  Widget _levelsCard(AnalysisLatest a) {
    final l = a.levels;
    return SectionCard(
      title: Ar.levelsSection,
      icon: Icons.straighten_rounded,
      children: l.hasAnyValue
          ? [
              InfoRow(label: Ar.entry, value: Fmt.number(l.entry)),
              InfoRow(label: Ar.stopLoss, value: Fmt.number(l.stopLoss)),
              InfoRow(label: Ar.takeProfit, value: Fmt.number(l.takeProfit)),
              InfoRow(
                label: Ar.rewardRisk,
                value: Fmt.number(l.rewardRisk, decimals: 2),
              ),
              InfoRow(
                label: Ar.suggestedRisk,
                value: Fmt.pctFraction(l.suggestedRiskPct),
              ),
            ]
          : [
              const Padding(
                padding: EdgeInsets.symmetric(vertical: 8),
                child: Text(
                  'لا توجد مستويات مقترحة حالياً',
                  style: TextStyle(color: AyzrColors.textMuted, fontSize: 13),
                ),
              ),
            ],
    );
  }

  Widget _metaCard(AnalysisLatest a) => SectionCard(
        title: Ar.metaSection,
        icon: Icons.info_outline_rounded,
        children: [
          InfoRow(label: Ar.coverageTier, value: a.meta.coverageTier),
          InfoRow(
            label: Ar.dataFreshness,
            value: a.meta.dataFreshnessSec == null
                ? Fmt.unavailable
                : '${a.meta.dataFreshnessSec} ثانية',
          ),
          InfoRow(
            label: Ar.updatedAt,
            value: Fmt.isoToLocal(a.timestamp),
          ),
          if (a.meta.disclaimer != null)
            InfoRow(label: Ar.disclaimerLabel, value: a.meta.disclaimer!),
        ],
      );
}
