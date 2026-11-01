import 'package:flutter/material.dart';

import '../../core/strings_ar.dart';
import '../../core/theme.dart';
import '../../data/models/events.dart';
import '../../state/app_state.dart';
import '../screens/event_detail_screen.dart';
import 'countdown.dart';
import 'info_widgets.dart';
import 'state_panel.dart';

/// A dedicated "preparation for the upcoming news" section.
///
/// It surfaces the nearest scheduled release REGARDLESS of its impact level, so
/// a low-impact event is never hidden from the owner when it is the next one
/// due. The section carries a live countdown and a button, "تحضير للخبر القادم",
/// that opens the event's preparation view.
///
/// Honesty rules: with no upcoming event it says so plainly; while loading it
/// shows the loading panel; a failed read shows the reason. It never invents an
/// event, a time or a consensus.
class UpcomingNewsSection extends StatelessWidget {
  const UpcomingNewsSection({super.key, required this.state});

  final AppState state;

  @override
  Widget build(BuildContext context) {
    final section = state.upcomingEvents;
    return SectionCard(
      title: Ar.eventPrepareSection,
      icon: Icons.upcoming_rounded,
      children: [
        const Text(
          Ar.eventPrepareHint,
          style: TextStyle(
            color: AyzrColors.textMuted,
            fontSize: 12,
            height: 1.6,
          ),
        ),
        const SizedBox(height: 12),
        switch (section.phase) {
          LoadPhase.idle || LoadPhase.loading =>
            StatePanel.loading(title: Ar.loading),
          LoadPhase.empty => const StatePanel(
              icon: Icons.event_available_outlined,
              title: Ar.eventNoUpcoming,
              color: AyzrColors.textMuted,
            ),
          LoadPhase.error => StatePanel.error(
              detail: section.failure?.message,
              onRetry: () => state.loadUpcomingEvents(),
            ),
          LoadPhase.ready => _ready(context, section.data!),
        },
      ],
    );
  }

  Widget _ready(BuildContext context, List<EventListItem> items) {
    // The nearest release by scheduled time. Computed from the loaded list, not
    // assumed to be first, so a change in server ordering cannot surface the
    // wrong event.
    final nearest = state.nearestUpcomingEvent;
    if (nearest == null) {
      return const StatePanel(
        icon: Icons.event_available_outlined,
        title: Ar.eventNoUpcoming,
        color: AyzrColors.textMuted,
      );
    }
    final e = nearest.event;
    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        Container(
          padding: const EdgeInsets.all(12),
          decoration: BoxDecoration(
            color: AyzrColors.navyRaised,
            borderRadius: BorderRadius.circular(12),
            border: Border.all(color: AyzrColors.navyBorder),
          ),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Row(
                children: [
                  const Icon(Icons.star_rounded,
                      size: 15, color: AyzrColors.gold),
                  const SizedBox(width: 4),
                  const Text(
                    Ar.eventNearest,
                    style: TextStyle(
                      color: AyzrColors.goldSoft,
                      fontSize: 11.5,
                      fontWeight: FontWeight.w700,
                    ),
                  ),
                  const Spacer(),
                  _ImpactChip(impact: e.impact),
                ],
              ),
              const SizedBox(height: 8),
              Text(
                e.title.isEmpty ? Ar.unavailable : e.title,
                style: const TextStyle(
                  color: AyzrColors.textPrimary,
                  fontSize: 14,
                  fontWeight: FontWeight.w700,
                  height: 1.5,
                ),
              ),
              const SizedBox(height: 8),
              Row(
                children: [
                  const Icon(Icons.schedule_rounded,
                      size: 14, color: AyzrColors.textMuted),
                  const SizedBox(width: 4),
                  Text(
                    Fmt.isoToLocal(e.scheduledAtIso),
                    style: const TextStyle(
                        color: AyzrColors.textMuted, fontSize: 12),
                  ),
                  const Spacer(),
                  EventCountdown(targetUtcSeconds: e.scheduledAtUtc),
                ],
              ),
            ],
          ),
        ),
        const SizedBox(height: 12),
        // The action button. It opens the event's preparation view, which shows
        // the expectation turn (written by the backend's event watcher) or an
        // honest "not written yet" state.
        SizedBox(
          width: double.infinity,
          child: FilledButton.icon(
            onPressed: () => _openPreparation(context, nearest),
            icon: const Icon(Icons.playlist_add_check_rounded, size: 18),
            label: const Text(Ar.eventPrepareNext),
            style: FilledButton.styleFrom(
              backgroundColor: AyzrColors.gold,
              foregroundColor: AyzrColors.navyDeep,
              padding: const EdgeInsets.symmetric(vertical: 12),
              textStyle: const TextStyle(
                fontWeight: FontWeight.w700,
                fontSize: 14,
              ),
            ),
          ),
        ),
      ],
    );
  }

  void _openPreparation(BuildContext context, EventListItem item) {
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

/// Impact chip, reused from the events list so the two surfaces agree.
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
      child: Text(
        label,
        style: TextStyle(
            color: color, fontSize: 11, fontWeight: FontWeight.w600),
      ),
    );
  }
}
