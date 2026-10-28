import 'package:flutter/material.dart';

import '../../core/strings_ar.dart';
import '../../core/theme.dart';
import '../../data/models/events.dart';
import '../../state/app_state.dart';
import '../widgets/info_widgets.dart';
import '../widgets/state_panel.dart';
import 'event_detail_screen.dart';

/// الأحداث الاقتصادية — scheduled macro releases and their consensus.
///
/// The list never invents a consensus: an event the feed carried without a
/// forecast shows "لم يُنشر توقع". The stage chip reflects whether an
/// expectation turn exists, distinct from whether it produced a proposal.
class EventsScreen extends StatelessWidget {
  const EventsScreen({super.key, required this.state});

  final AppState state;

  @override
  Widget build(BuildContext context) {
    final upcoming = state.eventScope == 'upcoming';
    return Column(
      children: [
        _scopeBar(upcoming),
        Expanded(
          child: RefreshIndicator(
            onRefresh: () => state.loadEvents(),
            child: SectionBuilder<List<EventListItem>>(
              section: state.events,
              onRetry: () => state.loadEvents(),
              emptyTitle: Ar.eventsEmpty,
              builder: (context, items) => ListView.separated(
                padding: const EdgeInsets.all(16),
                itemCount: items.length,
                separatorBuilder: (_, __) => const SizedBox(height: 10),
                itemBuilder: (context, i) => _EventTile(
                  item: items[i],
                  onTap: () => _openDetail(context, items[i]),
                ),
              ),
            ),
          ),
        ),
      ],
    );
  }

  Widget _scopeBar(bool upcoming) {
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 10),
      color: AyzrColors.navy,
      child: Row(
        children: [
          Expanded(
            child: _ScopeChip(
              label: Ar.eventUpcoming,
              selected: upcoming,
              onTap: () => state.loadEvents(scope: 'upcoming'),
            ),
          ),
          const SizedBox(width: 8),
          Expanded(
            child: _ScopeChip(
              label: Ar.eventPast,
              selected: !upcoming,
              onTap: () => state.loadEvents(scope: 'past'),
            ),
          ),
        ],
      ),
    );
  }

  void _openDetail(BuildContext context, EventListItem item) {
    Navigator.of(context).push(
      MaterialPageRoute(
        builder: (_) => EventDetailScreen(
          state: state,
          eventKey: item.event.eventKey,
          preview: item.event,
        ),
      ),
    );
  }
}

class _ScopeChip extends StatelessWidget {
  const _ScopeChip({
    required this.label,
    required this.selected,
    required this.onTap,
  });

  final String label;
  final bool selected;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) {
    return InkWell(
      onTap: onTap,
      borderRadius: BorderRadius.circular(10),
      child: Container(
        padding: const EdgeInsets.symmetric(vertical: 8),
        alignment: Alignment.center,
        decoration: BoxDecoration(
          color: selected
              ? AyzrColors.gold.withOpacity(0.16)
              : Colors.transparent,
          borderRadius: BorderRadius.circular(10),
          border: Border.all(
            color: selected
                ? AyzrColors.gold.withOpacity(0.6)
                : AyzrColors.textMuted.withOpacity(0.3),
          ),
        ),
        child: Text(
          label,
          style: TextStyle(
            color: selected ? AyzrColors.gold : AyzrColors.textMuted,
            fontSize: 13,
            fontWeight: FontWeight.w700,
          ),
        ),
      ),
    );
  }
}

class _EventTile extends StatelessWidget {
  const _EventTile({required this.item, required this.onTap});

  final EventListItem item;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) {
    final e = item.event;
    return Card(
      margin: EdgeInsets.zero,
      child: InkWell(
        onTap: onTap,
        borderRadius: BorderRadius.circular(14),
        child: Padding(
          padding: const EdgeInsets.all(14),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Row(
                children: [
                  Expanded(
                    child: Text(
                      e.title.isEmpty ? Ar.unavailable : e.title,
                      style: const TextStyle(
                        color: AyzrColors.textPrimary,
                        fontSize: 14.5,
                        fontWeight: FontWeight.w700,
                        height: 1.5,
                      ),
                    ),
                  ),
                  _ImpactChip(impact: e.impact),
                ],
              ),
              const SizedBox(height: 8),
              Row(
                children: [
                  const Icon(Icons.flag_outlined,
                      size: 14, color: AyzrColors.textMuted),
                  const SizedBox(width: 4),
                  Text(
                    Fmt.text(e.country),
                    style: const TextStyle(
                        color: AyzrColors.textMuted, fontSize: 12),
                  ),
                  const SizedBox(width: 12),
                  const Icon(Icons.schedule_rounded,
                      size: 14, color: AyzrColors.textMuted),
                  const SizedBox(width: 4),
                  Expanded(
                    child: Text(
                      Fmt.isoToLocal(e.scheduledAtIso),
                      style: const TextStyle(
                          color: AyzrColors.textMuted, fontSize: 12),
                    ),
                  ),
                ],
              ),
              const SizedBox(height: 10),
              Row(
                children: [
                  _Metric(label: Ar.eventConsensus, value: e.forecast),
                  const SizedBox(width: 16),
                  _Metric(label: Ar.eventPrevious, value: e.previous),
                  const Spacer(),
                  _StageChip(item: item),
                ],
              ),
            ],
          ),
        ),
      ),
    );
  }
}

/// A consensus/previous value. A null forecast renders the honest "not
/// published" label, never "0".
class _Metric extends StatelessWidget {
  const _Metric({required this.label, required this.value});

  final String label;
  final String? value;

  @override
  Widget build(BuildContext context) {
    final present = value != null && value!.trim().isNotEmpty;
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Text(label,
            style: const TextStyle(color: AyzrColors.textMuted, fontSize: 11)),
        const SizedBox(height: 2),
        Text(
          present ? value!.trim() : Ar.eventForecastUnavailable,
          style: TextStyle(
            color: present ? AyzrColors.textPrimary : AyzrColors.textMuted,
            fontSize: 13,
            fontWeight: FontWeight.w600,
          ),
        ),
      ],
    );
  }
}

class _ImpactChip extends StatelessWidget {
  const _ImpactChip({required this.impact});

  final String impact;

  @override
  Widget build(BuildContext context) {
    final (label, color) = switch (impact) {
      'high' => (Ar.eventImpactHigh, AyzrColors.down),
      'medium' => (Ar.eventImpactMedium, AyzrColors.warning),
      'low' => (Ar.eventImpactLow, AyzrColors.textMuted),
      'holiday' => (Ar.eventImpactHoliday, AyzrColors.textMuted),
      _ => (Ar.eventImpactUnknown, AyzrColors.textMuted),
    };
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 3),
      decoration: BoxDecoration(
        color: color.withOpacity(0.14),
        borderRadius: BorderRadius.circular(12),
        border: Border.all(color: color.withOpacity(0.4)),
      ),
      child: Text(label,
          style:
              TextStyle(color: color, fontSize: 11, fontWeight: FontWeight.w600)),
    );
  }
}

class _StageChip extends StatelessWidget {
  const _StageChip({required this.item});

  final EventListItem item;

  @override
  Widget build(BuildContext context) {
    final (label, color) = item.hasResult
        ? (Ar.eventStageReleased, AyzrColors.up)
        : item.hasExpectation
            ? (Ar.eventStagePrepared, AyzrColors.gold)
            : (Ar.eventStageScheduled, AyzrColors.textMuted);
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 3),
      decoration: BoxDecoration(
        color: color.withOpacity(0.14),
        borderRadius: BorderRadius.circular(12),
        border: Border.all(color: color.withOpacity(0.4)),
      ),
      child: Text(label,
          style:
              TextStyle(color: color, fontSize: 11, fontWeight: FontWeight.w600)),
    );
  }
}
