import 'package:flutter/material.dart';

import '../../core/strings_ar.dart';
import '../../core/theme.dart';
import '../../data/models/news.dart';
import '../../state/app_state.dart';
import '../widgets/analysis_view.dart';
import '../widgets/info_widgets.dart';
import '../widgets/state_panel.dart';

/// نتائج تحليل الأخبار — the news-results log from
/// `GET /api/v1/news/results`. Every persisted analysis joined to the full
/// source metadata of its article: headline, source link, publish time and the
/// analysis outcome in one row.
///
/// This is deliberately separate from السجل (the ASTRA analysis history): the
/// results log is the news-intelligence record, and it is the surface that lets
/// the user audit what the AI concluded and on which source.
class NewsResultsScreen extends StatelessWidget {
  const NewsResultsScreen({super.key, required this.state});

  final AppState state;

  @override
  Widget build(BuildContext context) {
    return Column(
      children: [
        _filterBar(),
        Expanded(
          child: RefreshIndicator(
            onRefresh: () => state.loadNewsResults(),
            child: SectionBuilder<List<NewsResult>>(
              section: state.newsResults,
              onRetry: () => state.loadNewsResults(),
              emptyTitle: Ar.newsResultsEmpty,
              builder: (context, items) => ListView.separated(
                padding: const EdgeInsets.all(16),
                itemCount: items.length,
                separatorBuilder: (_, __) => const SizedBox(height: 10),
                itemBuilder: (context, i) => _ResultTile(result: items[i]),
              ),
            ),
          ),
        ),
      ],
    );
  }

  Widget _filterBar() {
    // The four backend direction tokens plus "all". A ChoiceChip row keeps the
    // filter visible without a menu the user has to open.
    const options = <(String, String)>[
      ('', Ar.filterAll),
      ('bullish', Ar.dirBullish),
      ('bearish', Ar.dirBearish),
      ('sideways', Ar.dirSideways),
      ('no_trade', Ar.dirNoTrade),
    ];
    return Container(
      color: AyzrColors.navy,
      padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
      child: SingleChildScrollView(
        scrollDirection: Axis.horizontal,
        child: Row(
          children: [
            const Icon(Icons.filter_alt_outlined,
                size: 18, color: AyzrColors.textMuted),
            const SizedBox(width: 8),
            for (final (value, label) in options)
              Padding(
                padding: const EdgeInsets.only(left: 8),
                child: ChoiceChip(
                  label: Text(label),
                  selected: state.newsResultDirection == value,
                  onSelected: (_) => state.loadNewsResults(direction: value),
                  labelStyle: TextStyle(
                    fontSize: 12,
                    color: state.newsResultDirection == value
                        ? AyzrColors.navyDeep
                        : AyzrColors.textPrimary,
                    fontWeight: FontWeight.w600,
                  ),
                  selectedColor: AyzrColors.gold,
                  backgroundColor: AyzrColors.navyRaised,
                  side: const BorderSide(color: AyzrColors.navyBorder),
                ),
              ),
          ],
        ),
      ),
    );
  }
}

class _ResultTile extends StatelessWidget {
  const _ResultTile({required this.result});

  final NewsResult result;

  @override
  Widget build(BuildContext context) {
    final a = result.article;
    final an = result.analysis;
    return Card(
      margin: EdgeInsets.zero,
      child: Padding(
        padding: const EdgeInsets.all(14),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            // The headline is shown only when the source row was retained; a
            // missing article renders "غير متاح" rather than a fabricated one.
            Text(
              result.articleAvailable && a.headline.isNotEmpty
                  ? a.headline
                  : Ar.unavailable,
              style: const TextStyle(
                color: AyzrColors.textPrimary,
                fontSize: 14.5,
                fontWeight: FontWeight.w700,
                height: 1.5,
              ),
            ),
            const SizedBox(height: 10),
            Row(
              children: [
                DirectionChip(direction: an.direction),
                const SizedBox(width: 8),
                _ChannelChip(channel: an.aiGoldChannel),
                const Spacer(),
                _StatusChip(status: an.status),
              ],
            ),
            const SizedBox(height: 10),
            InfoRow(label: Ar.source, value: Fmt.text(a.source)),
            InfoRow(
              label: Ar.publishedAt,
              value: Fmt.isoToLocal(a.publishedAtIso),
            ),
            if (a.category.isNotEmpty)
              InfoRow(label: Ar.categoryLabel, value: a.category),
            InfoRow(
              label: Ar.updatedAt,
              value: epochToLocal(an.analyzedAtUtc),
            ),
            if (an.summaryAr.isNotEmpty) ...[
              const Divider(height: 20),
              Text(
                an.summaryAr,
                maxLines: 4,
                overflow: TextOverflow.ellipsis,
                style: const TextStyle(
                  color: AyzrColors.textMuted,
                  fontSize: 12.5,
                  height: 1.5,
                ),
              ),
            ],
          ],
        ),
      ),
    );
  }
}

/// The gold-transmission channel, as a small tinted chip. Rendered for every
/// value including "unknown", so an unanswered classifier is visible.
class _ChannelChip extends StatelessWidget {
  const _ChannelChip({required this.channel});

  final String channel;

  @override
  Widget build(BuildContext context) {
    final label = goldChannelLabel(channel);
    final known = channel != 'unknown' && channel.isNotEmpty;
    final color = known ? AyzrColors.goldSoft : AyzrColors.textMuted;
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 3),
      decoration: BoxDecoration(
        color: color.withOpacity(0.12),
        borderRadius: BorderRadius.circular(12),
        border: Border.all(color: color.withOpacity(0.4)),
      ),
      child: Text(
        label,
        style: TextStyle(
          color: color,
          fontSize: 11,
          fontWeight: FontWeight.w600,
        ),
      ),
    );
  }
}

class _StatusChip extends StatelessWidget {
  const _StatusChip({required this.status});

  final String status;

  @override
  Widget build(BuildContext context) {
    final ok = status == 'ok';
    final color = ok ? AyzrColors.up : AyzrColors.textMuted;
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 3),
      decoration: BoxDecoration(
        color: color.withOpacity(0.14),
        borderRadius: BorderRadius.circular(12),
        border: Border.all(color: color.withOpacity(0.4)),
      ),
      child: Text(
        ok ? Ar.statusAnalyzed : Ar.analysisUnavailable,
        style: TextStyle(color: color, fontSize: 11, fontWeight: FontWeight.w600),
      ),
    );
  }
}
