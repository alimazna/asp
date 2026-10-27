import 'package:flutter/material.dart';

import '../../core/strings_ar.dart';
import '../../core/theme.dart';
import '../../data/models/health.dart';
import '../../state/app_state.dart';
import '../widgets/info_widgets.dart';
import '../widgets/state_panel.dart';

/// الرئيسية — a calm overview: system health, market state and the latest
/// signal summary. Every value that the backend cannot source is shown as
/// "غير متاح"; the shadow-only disclaimer is always visible.
class HomeScreen extends StatelessWidget {
  const HomeScreen({super.key, required this.state});

  final AppState state;

  @override
  Widget build(BuildContext context) {
    return RefreshIndicator(
      onRefresh: state.refreshAll,
      child: ListView(
        padding: const EdgeInsets.all(16),
        children: [
          NoticeBanner.shadow(),
          if (state.isDegraded) NoticeBanner.degraded(),
          if (state.isStale) NoticeBanner.stale(),
          _HealthCard(section: state.health),
          const SizedBox(height: 4),
          _MarketStateCard(state: state),
          const SizedBox(height: 4),
          _SignalSummaryCard(state: state),
        ],
      ),
    );
  }
}

class _HealthCard extends StatelessWidget {
  const _HealthCard({required this.section});

  final Section<HealthV1> section;

  @override
  Widget build(BuildContext context) {
    return SectionCard(
      title: Ar.systemHealth,
      icon: Icons.monitor_heart_outlined,
      children: [
        SectionBuilder<HealthV1>(
          section: section,
          onRetry: null,
          builder: (context, h) {
            return Column(
              children: [
                InfoRow(
                  label: Ar.systemHealth,
                  value: _statusAr(h.status),
                  color: _statusColor(h.status),
                ),
                InfoRow(
                  label: Ar.bridgeLabel,
                  value: _statusAr(h.bridge),
                  color: _statusColor(h.bridge),
                ),
              ],
            );
          },
        ),
      ],
    );
  }

  static String _statusAr(String s) {
    switch (s) {
      case 'ok':
        return Ar.healthOk;
      case 'degraded':
        return Ar.healthDegraded;
      case 'offline':
        return Ar.healthOffline;
      default:
        return Ar.unavailable;
    }
  }

  static Color _statusColor(String s) {
    switch (s) {
      case 'ok':
        return AyzrColors.up;
      case 'degraded':
        return AyzrColors.warning;
      case 'offline':
        return AyzrColors.down;
      default:
        return AyzrColors.textMuted;
    }
  }
}

class _MarketStateCard extends StatelessWidget {
  const _MarketStateCard({required this.state});

  final AppState state;

  @override
  Widget build(BuildContext context) {
    return SectionCard(
      title: Ar.marketState,
      icon: Icons.public_rounded,
      children: [
        SectionBuilder(
          section: state.analysis,
          onRetry: state.loadAnalysis,
          builder: (context, a) {
            return Column(
              children: [
                InfoRow(label: Ar.regime, value: a.context.regime),
                InfoRow(label: Ar.h4Bias, value: a.context.h4Bias),
                InfoRow(label: Ar.volatility, value: a.context.volatilityState),
                InfoRow(
                  label: Ar.asOf,
                  value: Fmt.isoToLocal(a.timestamp),
                ),
              ],
            );
          },
        ),
      ],
    );
  }
}

class _SignalSummaryCard extends StatelessWidget {
  const _SignalSummaryCard({required this.state});

  final AppState state;

  @override
  Widget build(BuildContext context) {
    return SectionCard(
      title: Ar.latestSignal,
      icon: Icons.insights_rounded,
      children: [
        SectionBuilder(
          section: state.analysis,
          onRetry: state.loadAnalysis,
          builder: (context, a) {
            final s = a.signal;
            return Column(
              crossAxisAlignment: CrossAxisAlignment.stretch,
              children: [
                Align(
                  alignment: AlignmentDirectional.centerStart,
                  child: DirectionChip(direction: s.direction),
                ),
                const SizedBox(height: 10),
                // RULE C: show a probability only when calibrated; otherwise the
                // raw score, explicitly labelled as a score.
                if (s.probabilityCalibrated && s.probability != null)
                  InfoRow(
                    label: Ar.probability,
                    value: Fmt.pct(s.probability),
                    color: AyzrColors.goldSoft,
                  )
                else
                  InfoRow(
                    label: Ar.score,
                    value: Fmt.number(s.score, decimals: 3),
                  ),
                if (!s.probabilityCalibrated)
                  const Padding(
                    padding: EdgeInsets.only(top: 4),
                    child: Text(
                      Ar.scoreNotProbability,
                      style: TextStyle(
                        color: AyzrColors.textMuted,
                        fontSize: 11.5,
                      ),
                    ),
                  ),
              ],
            );
          },
        ),
      ],
    );
  }
}
