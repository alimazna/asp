import 'package:flutter/material.dart';

import '../../core/strings_ar.dart';
import '../../core/theme.dart';
import '../../data/models/activity.dart';
import '../../state/app_state.dart';
import '../widgets/info_widgets.dart';
import '../widgets/state_panel.dart';

/// سجل النشاط — the durable record of what the backend actually did.
///
/// This screen exists so a successful news read, candle read or AI call is
/// *visible* rather than merely claimed. Every row is a real operation the
/// server logged; nothing here is inferred by the client.
class ActivityScreen extends StatefulWidget {
  const ActivityScreen({super.key, required this.state});

  final AppState state;

  @override
  State<ActivityScreen> createState() => _ActivityScreenState();
}

class _ActivityScreenState extends State<ActivityScreen> {
  static const _kinds = <String>[
    '',
    'news_read',
    'candle_read',
    'llm_analyze',
    'news_ingest',
    'history_read',
  ];

  String _kind = '';

  @override
  void initState() {
    super.initState();
    WidgetsBinding.instance.addPostFrameCallback((_) => _reload());
  }

  Future<void> _reload() async {
    await Future.wait([
      widget.state.loadActivity(kind: _kind),
      widget.state.loadProviders(),
    ]);
  }

  @override
  Widget build(BuildContext context) {
    return RefreshIndicator(
      onRefresh: _reload,
      child: ListView(
        padding: const EdgeInsets.all(16),
        children: [
          _ProvidersCard(state: widget.state),
          const SizedBox(height: 12),
          _KindFilter(
            kinds: _kinds,
            selected: _kind,
            onSelect: (k) {
              setState(() => _kind = k);
              widget.state.loadActivity(kind: k);
            },
          ),
          const SizedBox(height: 12),
          SectionBuilder<ActivityPage>(
            section: widget.state.activity,
            onRetry: _reload,
            emptyTitle: Ar.activityEmpty,
            builder: (context, page) => Column(
              crossAxisAlignment: CrossAxisAlignment.stretch,
              children: [
                _TallyCard(tally: page.tally),
                const SizedBox(height: 12),
                for (final e in page.entries)
                  Padding(
                    padding: const EdgeInsets.only(bottom: 10),
                    child: _ActivityTile(entry: e),
                  ),
              ],
            ),
          ),
        ],
      ),
    );
  }
}

/// Which AI credentials are configured and how each has performed. Secret-free:
/// only labels and counts cross the wire.
class _ProvidersCard extends StatelessWidget {
  const _ProvidersCard({required this.state});

  final AppState state;

  @override
  Widget build(BuildContext context) {
    final page = state.providers.data;
    return Card(
      margin: EdgeInsets.zero,
      child: Padding(
        padding: const EdgeInsets.all(16),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            const Text(Ar.providersTitle, style: _kHeading),
            const SizedBox(height: 4),
            const Text(Ar.providersSubtitle, style: _kMuted),
            const SizedBox(height: 12),
            if (page == null || page.providers.isEmpty) ...[
              const Text(Ar.providerNone, style: _kBody),
              const SizedBox(height: 4),
              const Text(Ar.providerNoneHint, style: _kMuted),
            ] else ...[
              Text('${Ar.activeProvider}: ${Fmt.text(page.activeProvider)}',
                  style: _kBody),
              const SizedBox(height: 10),
              for (final p in page.providers)
                Padding(
                  padding: const EdgeInsets.only(bottom: 8),
                  child: _ProviderRow(provider: p),
                ),
            ],
          ],
        ),
      ),
    );
  }
}

class _ProviderRow extends StatelessWidget {
  const _ProviderRow({required this.provider});

  final ProviderStatus provider;

  @override
  Widget build(BuildContext context) {
    final (label, color) = provider.isWorking
        ? (Ar.providerWorking, AyzrColors.up)
        : provider.isUntried
            ? (Ar.providerUntried, AyzrColors.textMuted)
            : (Ar.providerFailed, AyzrColors.down);
    return Row(
      children: [
        Container(
          width: 8,
          height: 8,
          decoration: BoxDecoration(color: color, shape: BoxShape.circle),
        ),
        const SizedBox(width: 8),
        Expanded(child: Text(provider.provider, style: _kBody)),
        Text(
          '$label · ${Ar.providerSuccesses} ${provider.successes}/'
          '${provider.attempts}',
          style: _kMuted.copyWith(color: color),
        ),
      ],
    );
  }
}

class _KindFilter extends StatelessWidget {
  const _KindFilter({
    required this.kinds,
    required this.selected,
    required this.onSelect,
  });

  final List<String> kinds;
  final String selected;
  final ValueChanged<String> onSelect;

  static String labelFor(String kind) => switch (kind) {
        'news_read' => Ar.kindNewsRead,
        'candle_read' => Ar.kindCandleRead,
        'llm_analyze' => Ar.kindLlmAnalyze,
        'news_ingest' => Ar.kindNewsIngest,
        'history_read' => Ar.kindHistoryRead,
        _ => Ar.filterAll,
      };

  @override
  Widget build(BuildContext context) {
    return SingleChildScrollView(
      scrollDirection: Axis.horizontal,
      reverse: true, // RTL: the first chip sits on the right
      child: Row(
        children: [
          for (final k in kinds)
            Padding(
              padding: const EdgeInsets.only(left: 8),
              child: ChoiceChip(
                label: Text(labelFor(k)),
                selected: k == selected,
                onSelected: (_) => onSelect(k),
              ),
            ),
        ],
      ),
    );
  }
}

class _TallyCard extends StatelessWidget {
  const _TallyCard({required this.tally});

  final ActivityTally tally;

  @override
  Widget build(BuildContext context) {
    final cells = <(String, int, Color)>[
      (Ar.outcomeOk, tally.ok, AyzrColors.up),
      (Ar.outcomeEmpty, tally.empty, AyzrColors.textMuted),
      (Ar.outcomeRejected, tally.rejected, AyzrColors.warning),
      (Ar.outcomeUnavailable, tally.unavailable, AyzrColors.warning),
      (Ar.outcomeError, tally.error, AyzrColors.down),
    ];
    return Card(
      margin: EdgeInsets.zero,
      child: Padding(
        padding: const EdgeInsets.all(16),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Row(
              children: [
                const Expanded(
                  child: Text(Ar.activitySummary, style: _kHeading),
                ),
                Text('${Ar.activityTotal} ${tally.total}', style: _kMuted),
              ],
            ),
            const SizedBox(height: 12),
            Wrap(
              spacing: 16,
              runSpacing: 10,
              children: [
                for (final (label, value, color) in cells)
                  Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      Text('$value', style: _kHeading.copyWith(color: color)),
                      Text(label, style: _kMuted),
                    ],
                  ),
              ],
            ),
          ],
        ),
      ),
    );
  }
}

class _ActivityTile extends StatelessWidget {
  const _ActivityTile({required this.entry});

  final ActivityEntry entry;

  static (String, Color) _outcomeVisual(String outcome) => switch (outcome) {
        'ok' => (Ar.outcomeOk, AyzrColors.up),
        'empty' => (Ar.outcomeEmpty, AyzrColors.textMuted),
        'rejected' => (Ar.outcomeRejected, AyzrColors.warning),
        'unavailable' => (Ar.outcomeUnavailable, AyzrColors.warning),
        _ => (Ar.outcomeError, AyzrColors.down),
      };

  @override
  Widget build(BuildContext context) {
    final (outcomeLabel, color) = _outcomeVisual(entry.outcome);
    final kindLabel = _KindFilter.labelFor(entry.kind);
    return Card(
      margin: EdgeInsets.zero,
      child: Padding(
        padding: const EdgeInsets.all(14),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Row(
              children: [
                Container(
                  padding:
                      const EdgeInsets.symmetric(horizontal: 8, vertical: 3),
                  decoration: BoxDecoration(
                    color: color.withOpacity(0.15),
                    borderRadius: BorderRadius.circular(6),
                  ),
                  child:
                      Text(outcomeLabel, style: _kMuted.copyWith(color: color)),
                ),
                const SizedBox(width: 8),
                Expanded(child: Text(kindLabel, style: _kBody)),
                if (entry.at != null)
                  Text(Fmt.isoToLocal(entry.at!.toIso8601String()),
                      style: _kMuted),
              ],
            ),
            const SizedBox(height: 8),
            Text(Fmt.text(entry.detail), style: _kBody),
            const SizedBox(height: 6),
            Wrap(
              spacing: 14,
              runSpacing: 4,
              children: [
                if (entry.source.isNotEmpty)
                  Text('${Ar.sourceLabel}: ${entry.source}', style: _kMuted),
                if (entry.itemCount > 0)
                  Text('${Ar.itemsLabel}: ${entry.itemCount}', style: _kMuted),
                if (entry.hasDuration)
                  Text('${Ar.durationLabel}: ${entry.durationMs} ms',
                      style: _kMuted),
              ],
            ),
            if (entry.reason.isNotEmpty) ...[
              const SizedBox(height: 6),
              Text(Fmt.text(entry.reason), style: _kMuted),
            ],
          ],
        ),
      ),
    );
  }
}

const _kHeading = TextStyle(
  color: AyzrColors.textPrimary,
  fontSize: 14,
  fontWeight: FontWeight.w700,
);
const _kBody = TextStyle(
  color: AyzrColors.textPrimary,
  fontSize: 13,
  height: 1.5,
);
const _kMuted = TextStyle(
  color: AyzrColors.textMuted,
  fontSize: 11.5,
  height: 1.4,
);
