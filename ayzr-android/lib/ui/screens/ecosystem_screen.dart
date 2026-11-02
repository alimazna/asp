import 'package:flutter/material.dart';

import '../../core/strings_ar.dart';
import '../../core/theme.dart';
import '../../data/models/ecosystem.dart';
import '../../state/app_state.dart';
import '../widgets/state_panel.dart';

/// The AYZR ecosystem screen: sessions, engine registry, the current
/// orchestration cycle, and the append-only decision ledger, in four tabs.
///
/// Everything is read straight from the four ecosystem routes. Empty states are
/// explicit Arabic messages; nothing is fabricated when a section has no data.
class EcosystemScreen extends StatefulWidget {
  const EcosystemScreen({super.key, required this.state});

  final AppState state;

  @override
  State<EcosystemScreen> createState() => _EcosystemScreenState();
}

class _EcosystemScreenState extends State<EcosystemScreen>
    with SingleTickerProviderStateMixin {
  late final TabController _tabs = TabController(length: 4, vsync: this);

  @override
  void initState() {
    super.initState();
    WidgetsBinding.instance.addPostFrameCallback((_) => widget.state.loadEcosystem());
  }

  @override
  void dispose() {
    _tabs.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return ListenableBuilder(
      listenable: widget.state,
      builder: (context, _) {
        final s = widget.state;
        return Column(
          children: [
            TabBar(
              controller: _tabs,
              isScrollable: true,
              tabAlignment: TabAlignment.start,
              tabs: const [
                Tab(text: Ar.ecoSessions),
                Tab(text: Ar.ecoEngines),
                Tab(text: Ar.ecoOpportunities),
                Tab(text: Ar.ecoLedger),
              ],
            ),
            const _ShadowBanner(),
            Expanded(
              child: TabBarView(
                controller: _tabs,
                children: [
                  _SessionsTab(state: s),
                  _EnginesTab(state: s),
                  _OpportunitiesTab(state: s),
                  _LedgerTab(state: s),
                ],
              ),
            ),
          ],
        );
      },
    );
  }
}

class _ShadowBanner extends StatelessWidget {
  const _ShadowBanner();

  @override
  Widget build(BuildContext context) {
    return Container(
      width: double.infinity,
      color: AyzrColors.surfaceHigh,
      padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 8),
      child: const Row(
        children: [
          Icon(Icons.visibility_outlined, size: 16, color: AyzrColors.goldSoft),
          SizedBox(width: 8),
          Expanded(
            child: Text(
              Ar.ecoShadow,
              style: TextStyle(color: AyzrColors.textMuted, fontSize: 12),
            ),
          ),
        ],
      ),
    );
  }
}

// ---------------------------------------------------------------------------
// Sessions
// ---------------------------------------------------------------------------
class _SessionsTab extends StatelessWidget {
  const _SessionsTab({required this.state});

  final AppState state;

  @override
  Widget build(BuildContext context) {
    final sec = state.sessions;
    if (sec.phase == LoadPhase.loading || sec.phase == LoadPhase.idle) {
      return StatePanel.loading(title: Ar.ecoSessions);
    }
    if (sec.failure != null) {
      return StatePanel.forFailure(sec.failure!, onRetry: state.loadSessions);
    }
    final snap = sec.data;
    if (snap == null || snap.sessions.isEmpty) {
      return StatePanel.empty(
        title: Ar.ecoNoSessions,
        detail: snap != null && snap.unavailableZones.isNotEmpty
            ? 'مناطق زمنية غير متاحة: ${snap.unavailableZones.join('، ')}'
            : null,
      );
    }
    return ListView(
      padding: const EdgeInsets.all(12),
      children: [
        if (snap.londonNewYorkOverlap)
          const _InfoChip(
            icon: Icons.bolt_rounded,
            label: Ar.ecoOverlap,
            color: AyzrColors.gold,
          ),
        const SizedBox(height: 8),
        ...snap.sessions.map((s) => _SessionCard(session: s)),
      ],
    );
  }
}

class _SessionCard extends StatelessWidget {
  const _SessionCard({required this.session});

  final SessionState session;

  @override
  Widget build(BuildContext context) {
    final open = session.isOpen;
    final tint = session.unavailable
        ? AyzrColors.warning
        : open
            ? AyzrColors.up
            : AyzrColors.textMuted;
    return Card(
      margin: const EdgeInsets.only(bottom: 10),
      child: Padding(
        padding: const EdgeInsets.all(14),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Row(
              children: [
                Container(
                  width: 8,
                  height: 8,
                  decoration: BoxDecoration(color: tint, shape: BoxShape.circle),
                ),
                const SizedBox(width: 8),
                Expanded(
                  child: Text(
                    session.nameAr.isEmpty ? session.id : session.nameAr,
                    style: const TextStyle(
                      fontSize: 16,
                      fontWeight: FontWeight.w600,
                      color: AyzrColors.textPrimary,
                    ),
                  ),
                ),
                _StatusPill(label: session.phaseAr, color: tint),
              ],
            ),
            const SizedBox(height: 8),
            Text(
              session.zoneId,
              style: const TextStyle(color: AyzrColors.textMuted, fontSize: 12),
            ),
            if (session.unavailable) ...[
              const SizedBox(height: 6),
              Text(
                'تعذّر قراءة المنطقة الزمنية: ${session.error}',
                style: const TextStyle(color: AyzrColors.warning, fontSize: 12),
              ),
            ] else if (session.localTime != null) ...[
              const SizedBox(height: 6),
              Row(
                children: [
                const Text('${Ar.ecoLocalTime}: ',
                    style: TextStyle(color: AyzrColors.textMuted, fontSize: 12)),
                  Text(
                    session.localTime!,
                    style: const TextStyle(
                      color: AyzrColors.textPrimary,
                      fontSize: 13,
                      fontFeatures: [FontFeature.tabularFigures()],
                    ),
                  ),
                  if (session.inDst) ...[
                    const SizedBox(width: 8),
                    const _StatusPill(label: Ar.ecoDst, color: AyzrColors.goldSoft),
                  ],
                ],
              ),
            ],
          ],
        ),
      ),
    );
  }
}

// ---------------------------------------------------------------------------
// Engines
// ---------------------------------------------------------------------------
class _EnginesTab extends StatelessWidget {
  const _EnginesTab({required this.state});

  final AppState state;

  @override
  Widget build(BuildContext context) {
    final sec = state.ecosystemEngines;
    if (sec.phase == LoadPhase.loading || sec.phase == LoadPhase.idle) {
      return StatePanel.loading(title: Ar.ecoEngines);
    }
    if (sec.failure != null) {
      return StatePanel.forFailure(sec.failure!, onRetry: state.loadEcosystemEngines);
    }
    final reg = sec.data;
    if (reg == null || reg.engines.isEmpty) {
      return StatePanel.empty(title: Ar.ecoNoEngines);
    }
    return ListView(
      padding: const EdgeInsets.all(12),
      children: [
        Text(
          'إصدار العقد: ${reg.schemaVersion}',
          style: const TextStyle(color: AyzrColors.textMuted, fontSize: 12),
        ),
        const SizedBox(height: 8),
        ...reg.engines.map((e) => _EngineCard(engine: e)),
      ],
    );
  }
}

class _EngineCard extends StatelessWidget {
  const _EngineCard({required this.engine});

  final EngineDescriptor engine;

  Color get _healthColor {
    switch (engine.health) {
      case 'HEALTHY':
        return AyzrColors.up;
      case 'WAITING_FOR_DATA':
        return AyzrColors.goldSoft;
      case 'DEGRADED':
        return AyzrColors.warning;
      default:
        return AyzrColors.textMuted;
    }
  }

  @override
  Widget build(BuildContext context) {
    return Card(
      margin: const EdgeInsets.only(bottom: 10),
      child: Padding(
        padding: const EdgeInsets.all(14),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Row(
              children: [
                Expanded(
                  child: Text(
                    engine.strategyFamilyAr.isEmpty
                        ? engine.engineId
                        : engine.strategyFamilyAr,
                    style: const TextStyle(
                      fontSize: 15,
                      fontWeight: FontWeight.w600,
                      color: AyzrColors.textPrimary,
                    ),
                  ),
                ),
                _StatusPill(label: engine.healthAr, color: _healthColor),
              ],
            ),
            const SizedBox(height: 4),
            Text(
              '${engine.engineId} · ${engine.timeframe} · v${engine.version}',
              style: const TextStyle(color: AyzrColors.textMuted, fontSize: 11),
            ),
            if (engine.noteAr.isNotEmpty) ...[
              const SizedBox(height: 6),
              Text(
                engine.noteAr,
                style: const TextStyle(color: AyzrColors.textSecondary, fontSize: 12),
              ),
            ],
          ],
        ),
      ),
    );
  }
}

// ---------------------------------------------------------------------------
// Opportunities
// ---------------------------------------------------------------------------
class _OpportunitiesTab extends StatelessWidget {
  const _OpportunitiesTab({required this.state});

  final AppState state;

  @override
  Widget build(BuildContext context) {
    final sec = state.opportunities;
    if (sec.phase == LoadPhase.loading || sec.phase == LoadPhase.idle) {
      return StatePanel.loading(title: Ar.ecoOpportunities);
    }
    if (sec.failure != null) {
      return StatePanel.forFailure(sec.failure!, onRetry: state.loadOpportunities);
    }
    final cyc = sec.data;
    if (cyc == null || cyc.opportunities.isEmpty) {
      return StatePanel.empty(
        title: Ar.ecoNoOpportunities,
        detail: Ar.ecoNoOpportunitiesHint,
      );
    }
    return ListView(
      padding: const EdgeInsets.all(12),
      children: [
        if (cyc.dataQualityWarnings.isNotEmpty)
          _WarningBlock(title: Ar.ecoDataWarnings, items: cyc.dataQualityWarnings),
        if (cyc.engineFailures.isNotEmpty)
          _WarningBlock(title: Ar.ecoEngineFailures, items: cyc.engineFailures),
        ...cyc.opportunities.map((o) => _OpportunityCard(op: o)),
      ],
    );
  }
}

class _OpportunityCard extends StatelessWidget {
  const _OpportunityCard({required this.op});

  final Opportunity op;

  Color get _stateColor {
    switch (op.state) {
      case 'PUBLISH':
        return AyzrColors.up;
      case 'PUBLISH_WITH_CAUTION':
        return AyzrColors.goldSoft;
      case 'WATCH':
        return AyzrColors.warning;
      default:
        return AyzrColors.textMuted;
    }
  }

  @override
  Widget build(BuildContext context) {
    return Card(
      margin: const EdgeInsets.only(bottom: 12),
      child: Padding(
        padding: const EdgeInsets.all(14),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Row(
              children: [
                Expanded(
                  child: Text(
                    '${op.symbol} · ${op.timeframe} · ${op.directionAr}',
                    style: const TextStyle(
                      fontSize: 15,
                      fontWeight: FontWeight.w700,
                      color: AyzrColors.textPrimary,
                    ),
                  ),
                ),
                _StatusPill(label: op.stateAr, color: _stateColor),
              ],
            ),
            const SizedBox(height: 6),
            Text(
              op.rationaleAr,
              style: const TextStyle(color: AyzrColors.textSecondary, fontSize: 13),
            ),
            const SizedBox(height: 10),
            if (op.entry != null)
              _LevelRow(label: Ar.ecoEntry, level: op.entry!),
            if (op.stopLoss != null)
              _LevelRow(label: Ar.ecoStop, level: op.stopLoss!),
            for (var i = 0; i < op.takeProfits.length; i++)
              _LevelRow(label: '${Ar.ecoTargets} ${i + 1}', level: op.takeProfits[i]),
            const SizedBox(height: 8),
            if (op.levelSourceEngine.isNotEmpty)
              Text(
                '${Ar.ecoLevelSource}: ${op.levelSourceEngine}',
                style: const TextStyle(color: AyzrColors.textMuted, fontSize: 11),
              ),
            if (op.agreeingEngines.isNotEmpty)
              Text(
                '${Ar.ecoAgreeing}: ${op.agreeingEngines.join('، ')}',
                style: const TextStyle(color: AyzrColors.up, fontSize: 11),
              ),
            if (op.disagreeingEngines.isNotEmpty)
              Text(
                '${Ar.ecoDisagreeing}: ${op.disagreeingEngines.join('، ')}',
                style: const TextStyle(color: AyzrColors.down, fontSize: 11),
              ),
            if (op.conflictNotesAr.isNotEmpty)
              _WarningBlock(title: Ar.ecoConflicts, items: op.conflictNotesAr),
            if (op.riskFlags.isNotEmpty)
              _WarningBlock(title: Ar.ecoRiskFlags, items: op.riskFlags),
            const SizedBox(height: 8),
            Row(
              children: [
                Icon(
                  op.astraCorePresent ? Icons.verified_outlined : Icons.remove_circle_outline,
                  size: 14,
                  color: op.astraCorePresent ? AyzrColors.gold : AyzrColors.textMuted,
                ),
                const SizedBox(width: 6),
                Text(
                  op.astraCorePresent ? Ar.ecoCorePresent : Ar.ecoCoreAbsent,
                  style: const TextStyle(color: AyzrColors.textMuted, fontSize: 11),
                ),
              ],
            ),
            const Divider(height: 20),
            ...op.contributingEvents.map((e) => _EventTile(event: e)),
          ],
        ),
      ),
    );
  }
}

class _EventTile extends StatelessWidget {
  const _EventTile({required this.event});

  final EngineEvent event;

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.only(bottom: 8),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Text(
            '${event.strategyFamilyAr} · ${event.directionAr} · ${event.statusAr}',
            style: const TextStyle(
              color: AyzrColors.textPrimary,
              fontSize: 13,
              fontWeight: FontWeight.w600,
            ),
          ),
          if (event.explanationAr.isNotEmpty)
            Text(
              event.explanationAr,
              style: const TextStyle(color: AyzrColors.textSecondary, fontSize: 12),
            ),
          // Probability is shown ONLY when the backend marks it calibrated.
          if (event.confidenceCalibrated && event.confidenceProbability != null)
            Text(
              '${Ar.ecoCalibrated}: ${(event.confidenceProbability! * 100).toStringAsFixed(1)}%',
              style: const TextStyle(color: AyzrColors.goldSoft, fontSize: 11),
            )
          else
            const Text(
              Ar.ecoNoProbability,
              style: TextStyle(color: AyzrColors.textMuted, fontSize: 11),
            ),
        ],
      ),
    );
  }
}

// ---------------------------------------------------------------------------
// Ledger
// ---------------------------------------------------------------------------
class _LedgerTab extends StatelessWidget {
  const _LedgerTab({required this.state});

  final AppState state;

  @override
  Widget build(BuildContext context) {
    final sec = state.ledger;
    if (sec.phase == LoadPhase.loading || sec.phase == LoadPhase.idle) {
      return StatePanel.loading(title: Ar.ecoLedger);
    }
    if (sec.failure != null) {
      return StatePanel.forFailure(sec.failure!, onRetry: state.loadLedger);
    }
    final page = sec.data;
    if (page == null || page.entries.isEmpty) {
      return StatePanel.empty(title: Ar.ecoNoLedger);
    }
    return ListView.builder(
      padding: const EdgeInsets.all(12),
      itemCount: page.entries.length,
      itemBuilder: (context, i) => _LedgerRow(entry: page.entries[i]),
    );
  }
}

class _LedgerRow extends StatelessWidget {
  const _LedgerRow({required this.entry});

  final LedgerEntry entry;

  @override
  Widget build(BuildContext context) {
    return Card(
      margin: const EdgeInsets.only(bottom: 8),
      child: Padding(
        padding: const EdgeInsets.all(12),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Row(
              children: [
                Text(
                  '#${entry.seq}',
                  style: const TextStyle(
                    color: AyzrColors.goldSoft,
                    fontSize: 12,
                    fontFeatures: [FontFeature.tabularFigures()],
                  ),
                ),
                const SizedBox(width: 8),
                Expanded(
                  child: Text(
                    entry.typeAr.isEmpty ? entry.type : entry.typeAr,
                    style: const TextStyle(
                      color: AyzrColors.textPrimary,
                      fontSize: 13,
                      fontWeight: FontWeight.w600,
                    ),
                  ),
                ),
              ],
            ),
            if (entry.detailAr.isNotEmpty) ...[
              const SizedBox(height: 4),
              Text(
                entry.detailAr,
                style: const TextStyle(color: AyzrColors.textSecondary, fontSize: 12),
              ),
            ],
            if (entry.engineId.isNotEmpty)
              Text(
                entry.engineId,
                style: const TextStyle(color: AyzrColors.textMuted, fontSize: 11),
              ),
          ],
        ),
      ),
    );
  }
}

// ---------------------------------------------------------------------------
// Shared small widgets
// ---------------------------------------------------------------------------
class _LevelRow extends StatelessWidget {
  const _LevelRow({required this.label, required this.level});

  final String label;
  final PriceLevel level;

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 2),
      child: Row(
        children: [
          SizedBox(
            width: 110,
            child: Text(
              label,
              style: const TextStyle(color: AyzrColors.textMuted, fontSize: 12),
            ),
          ),
          Text(
            level.price.toStringAsFixed(2),
            style: const TextStyle(
              color: AyzrColors.textPrimary,
              fontSize: 13,
              fontFeatures: [FontFeature.tabularFigures()],
            ),
          ),
          if (level.source.isNotEmpty) ...[
            const SizedBox(width: 8),
            Expanded(
              child: Text(
                level.source,
                style: const TextStyle(color: AyzrColors.textMuted, fontSize: 11),
                overflow: TextOverflow.ellipsis,
              ),
            ),
          ],
        ],
      ),
    );
  }
}

class _StatusPill extends StatelessWidget {
  const _StatusPill({required this.label, required this.color});

  final String label;
  final Color color;

  @override
  Widget build(BuildContext context) {
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 3),
      decoration: BoxDecoration(
        color: color.withOpacity(0.15),
        borderRadius: BorderRadius.circular(20),
        border: Border.all(color: color.withOpacity(0.5)),
      ),
      child: Text(
        label,
        style: TextStyle(color: color, fontSize: 11, fontWeight: FontWeight.w600),
      ),
    );
  }
}

class _InfoChip extends StatelessWidget {
  const _InfoChip({required this.icon, required this.label, required this.color});

  final IconData icon;
  final String label;
  final Color color;

  @override
  Widget build(BuildContext context) {
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
      decoration: BoxDecoration(
        color: color.withOpacity(0.12),
        borderRadius: BorderRadius.circular(10),
      ),
      child: Row(
        children: [
          Icon(icon, size: 16, color: color),
          const SizedBox(width: 8),
          Text(label, style: TextStyle(color: color, fontSize: 13)),
        ],
      ),
    );
  }
}

class _WarningBlock extends StatelessWidget {
  const _WarningBlock({required this.title, required this.items});

  final String title;
  final List<String> items;

  @override
  Widget build(BuildContext context) {
    return Container(
      margin: const EdgeInsets.only(bottom: 10),
      padding: const EdgeInsets.all(10),
      decoration: BoxDecoration(
        color: AyzrColors.warning.withOpacity(0.10),
        borderRadius: BorderRadius.circular(10),
        border: Border.all(color: AyzrColors.warning.withOpacity(0.4)),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Text(
            title,
            style: const TextStyle(
              color: AyzrColors.warning,
              fontSize: 12,
              fontWeight: FontWeight.w700,
            ),
          ),
          const SizedBox(height: 4),
          ...items.map((t) => Text(
                '• $t',
                style: const TextStyle(color: AyzrColors.textSecondary, fontSize: 12),
              )),
        ],
      ),
    );
  }
}
