import 'package:flutter/material.dart';

import '../../core/strings_ar.dart';
import '../../core/theme.dart';
import '../../data/models/news.dart';
import '../../state/app_state.dart';
import '../widgets/info_widgets.dart';
import '../widgets/quota_banner.dart';
import '../widgets/state_panel.dart';
import 'news_detail_screen.dart';

/// الأخبار — the news list with a gold-only filter. Each row shows the latest
/// analysis status so the user knows whether an article has been analysed
/// without opening it.
class NewsScreen extends StatelessWidget {
  const NewsScreen({super.key, required this.state});

  final AppState state;

  @override
  Widget build(BuildContext context) {
    return Column(
      children: [
        _filterBar(),
        // The banner decides its own visibility: it hides only when every
        // provider is healthy, so a degraded chain (some keys down) is still
        // surfaced rather than silently swallowed.
        if (state.quota.data != null) QuotaBanner(quota: state.quota.data!),
        Expanded(
          child: RefreshIndicator(
            onRefresh: () => state.loadNews(),
            child: SectionBuilder<List<NewsListItem>>(
              section: state.news,
              onRetry: () => state.loadNews(),
              emptyTitle: Ar.empty,
              builder: (context, items) => ListView.separated(
                padding: const EdgeInsets.all(16),
                itemCount: items.length,
                separatorBuilder: (_, __) => const SizedBox(height: 10),
                itemBuilder: (context, i) => _NewsTile(
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

  Widget _filterBar() {
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 8),
      color: AyzrColors.navy,
      child: Row(
        children: [
          const Icon(Icons.filter_alt_outlined,
              size: 18, color: AyzrColors.textMuted),
          const SizedBox(width: 8),
          const Text(
            Ar.goldOnly,
            style: TextStyle(color: AyzrColors.textPrimary, fontSize: 13),
          ),
          const Spacer(),
          Switch(
            value: state.goldOnly,
            activeColor: AyzrColors.gold,
            onChanged: (v) => state.loadNews(goldOnly: v),
          ),
        ],
      ),
    );
  }

  void _openDetail(BuildContext context, NewsListItem item) {
    Navigator.of(context).push(
      MaterialPageRoute(
        builder: (_) => NewsDetailScreen(
          state: state,
          articleId: item.article.articleId,
          preview: item.article,
        ),
      ),
    );
  }
}

class _NewsTile extends StatelessWidget {
  const _NewsTile({required this.item, required this.onTap});

  final NewsListItem item;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) {
    final a = item.article;
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
              Text(
                a.headline.isEmpty ? Ar.unavailable : a.headline,
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
                  const Icon(Icons.source_outlined,
                      size: 14, color: AyzrColors.textMuted),
                  const SizedBox(width: 4),
                  Expanded(
                    child: Text(
                      Fmt.text(a.source),
                      style: const TextStyle(
                        color: AyzrColors.textMuted,
                        fontSize: 12,
                      ),
                    ),
                  ),
                  _StatusChip(status: item.analysisStatus),
                ],
              ),
            ],
          ),
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
    final analyzed = status == 'ok' || status == 'analyzed';
    final color = analyzed ? AyzrColors.up : AyzrColors.textMuted;
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 3),
      decoration: BoxDecoration(
        color: color.withOpacity(0.14),
        borderRadius: BorderRadius.circular(12),
        border: Border.all(color: color.withOpacity(0.4)),
      ),
      child: Text(
        analyzed ? Ar.statusAnalyzed : Ar.statusNone,
        style: TextStyle(color: color, fontSize: 11, fontWeight: FontWeight.w600),
      ),
    );
  }
}
