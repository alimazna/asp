import 'package:flutter/material.dart';

import '../../core/strings_ar.dart';
import '../../core/theme.dart';
import '../../data/models/events.dart';
import '../../data/models/news.dart';
import '../../state/app_state.dart';
import '../widgets/analysis_view.dart';
import '../widgets/info_widgets.dart';
import '../widgets/state_panel.dart';

/// تفاصيل الحدث — loads `GET /api/v1/events/detail?key=` and renders the event
/// plus its two analysis turns.
///
/// The two turns are kept visibly separate. A turn that has not run yet shows a
/// "pending" panel; a turn that ran but came back unavailable shows its own
/// reason. "Not yet" must never look like "ran and found nothing".
class EventDetailScreen extends StatefulWidget {
  const EventDetailScreen({
    super.key,
    required this.state,
    required this.eventKey,
    required this.preview,
  });

  final AppState state;
  final String eventKey;
  final ScheduledEvent preview;

  @override
  State<EventDetailScreen> createState() => _EventDetailScreenState();
}

class _EventDetailScreenState extends State<EventDetailScreen> {
  Section<EventDetail> _section = const Section<EventDetail>();

  @override
  void initState() {
    super.initState();
    _load();
  }

  Future<void> _load() async {
    setState(() => _section = _section.loading());
    final r = await widget.state.loadEventDetail(widget.eventKey);
    if (!mounted) return;
    setState(() {
      _section = r.isSuccess
          ? Section<EventDetail>.ready(r.value!)
          : Section<EventDetail>.failed(r.failure!);
    });
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(title: const Text(Ar.eventDetails)),
      body: SectionBuilder<EventDetail>(
        section: _section,
        onRetry: _load,
        builder: (context, detail) => ListView(
          padding: const EdgeInsets.all(16),
          children: [
            _eventCard(detail.event),
            const SizedBox(height: 14),
            _turn(
              title: Ar.eventExpectation,
              icon: Icons.insights_outlined,
              present: detail.hasExpectation,
              json: detail.expectationJson,
              pendingText: Ar.eventExpectationPending,
            ),
            const SizedBox(height: 14),
            _turn(
              title: Ar.eventResult,
              icon: Icons.assessment_outlined,
              present: detail.hasResult,
              json: detail.resultJson,
              pendingText: Ar.eventResultPending,
            ),
          ],
        ),
      ),
    );
  }

  Widget _eventCard(ScheduledEvent e) => SectionCard(
        title: Ar.eventDetails,
        icon: Icons.event_rounded,
        children: [
          Text(
            e.title.isEmpty ? Ar.unavailable : e.title,
            style: const TextStyle(
              color: AyzrColors.textPrimary,
              fontSize: 15,
              fontWeight: FontWeight.w700,
              height: 1.5,
            ),
          ),
          const SizedBox(height: 10),
          InfoRow(label: Ar.eventScheduledAt, value: Fmt.isoToLocal(e.scheduledAtIso)),
          InfoRow(label: Ar.source, value: Fmt.text(e.country)),
          InfoRow(
            label: Ar.eventConsensus,
            value: (e.forecast == null || e.forecast!.trim().isEmpty)
                ? Ar.eventForecastUnavailable
                : e.forecast!.trim(),
          ),
          InfoRow(
            label: Ar.eventPrevious,
            value: (e.previous == null || e.previous!.trim().isEmpty)
                ? Ar.eventForecastUnavailable
                : e.previous!.trim(),
          ),
          InfoRow(label: Ar.sourceLabel, value: Fmt.text(e.source)),
        ],
      );

  Widget _turn({
    required String title,
    required IconData icon,
    required bool present,
    required Map<String, dynamic>? json,
    required String pendingText,
  }) {
    if (!present || json == null) {
      return SectionCard(
        title: title,
        icon: icon,
        children: [
          Padding(
            padding: const EdgeInsets.symmetric(vertical: 8),
            child: Text(
              pendingText,
              style: const TextStyle(color: AyzrColors.textMuted, fontSize: 13),
            ),
          ),
        ],
      );
    }
    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        Padding(
          padding: const EdgeInsets.only(bottom: 8, right: 4),
          child: Row(
            children: [
              Icon(icon, size: 16, color: AyzrColors.gold),
              const SizedBox(width: 6),
              Text(
                title,
                style: const TextStyle(
                  color: AyzrColors.gold,
                  fontSize: 13.5,
                  fontWeight: FontWeight.w700,
                ),
              ),
            ],
          ),
        ),
        AnalysisView(analysis: NewsAnalysis.fromJson(json)),
      ],
    );
  }
}
