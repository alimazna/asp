import 'package:flutter/material.dart';

import '../../core/strings_ar.dart';
import '../../core/theme.dart';
import '../../data/models/news.dart';
import '../../state/app_state.dart';
import '../widgets/info_widgets.dart';
import '../widgets/state_panel.dart';

/// السجل — persisted analyses, newest first, from `GET /api/v1/history`.
class HistoryScreen extends StatelessWidget {
  const HistoryScreen({super.key, required this.state});

  final AppState state;

  @override
  Widget build(BuildContext context) {
    return RefreshIndicator(
      onRefresh: state.loadHistory,
      child: SectionBuilder<List<HistoryEntry>>(
        section: state.history,
        onRetry: state.loadHistory,
        emptyTitle: Ar.historyEmpty,
        builder: (context, items) => ListView.separated(
          padding: const EdgeInsets.all(16),
          itemCount: items.length,
          separatorBuilder: (_, __) => const SizedBox(height: 10),
          itemBuilder: (context, i) => _HistoryTile(entry: items[i]),
        ),
      ),
    );
  }
}

class _HistoryTile extends StatelessWidget {
  const _HistoryTile({required this.entry});

  final HistoryEntry entry;

  @override
  Widget build(BuildContext context) {
    final an = entry.analysis;
    return Card(
      margin: EdgeInsets.zero,
      child: Padding(
        padding: const EdgeInsets.all(14),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Text(
              entry.headline.isEmpty ? Ar.unavailable : entry.headline,
              style: const TextStyle(
                color: AyzrColors.textPrimary,
                fontSize: 14,
                fontWeight: FontWeight.w700,
                height: 1.5,
              ),
            ),
            const SizedBox(height: 8),
            if (an.summaryAr.isNotEmpty)
              Text(
                an.summaryAr,
                maxLines: 3,
                overflow: TextOverflow.ellipsis,
                style: const TextStyle(
                  color: AyzrColors.textMuted,
                  fontSize: 12.5,
                  height: 1.5,
                ),
              ),
            const SizedBox(height: 10),
            Row(
              children: [
                DirectionChip(direction: an.direction),
                const SizedBox(width: 8),
                Expanded(
                  child: Text(
                    Fmt.text(entry.source),
                    style: const TextStyle(
                      color: AyzrColors.textMuted,
                      fontSize: 11.5,
                    ),
                  ),
                ),
              ],
            ),
          ],
        ),
      ),
    );
  }
}
