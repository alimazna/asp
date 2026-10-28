import 'package:flutter/material.dart';

import '../../core/strings_ar.dart';
import '../../core/theme.dart';
import '../../data/models/news.dart';
import '../../state/app_state.dart';
import '../widgets/analysis_view.dart';
import '../widgets/info_widgets.dart';
import '../widgets/state_panel.dart';

/// تفاصيل الخبر — loads `GET /api/v1/news/analyze?id=` on demand and renders the
/// Arabic analysis. The analysis `status` is the gate: only `ok` may present a
/// proposal; any other status shows the reason instead of levels.
class NewsDetailScreen extends StatefulWidget {
  const NewsDetailScreen({
    super.key,
    required this.state,
    required this.articleId,
    required this.preview,
  });

  final AppState state;
  final String articleId;
  final NewsArticle preview;

  @override
  State<NewsDetailScreen> createState() => _NewsDetailScreenState();
}

class _NewsDetailScreenState extends State<NewsDetailScreen> {
  Section<NewsDetail> _section = const Section<NewsDetail>();

  @override
  void initState() {
    super.initState();
    _load();
  }

  Future<void> _load() async {
    setState(() => _section = _section.loading());
    final r = await widget.state.loadArticle(widget.articleId);
    if (!mounted) return;
    setState(() {
      _section = r.isSuccess
          ? Section<NewsDetail>.ready(r.value!)
          : Section<NewsDetail>.failed(r.failure!);
    });
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(title: const Text(Ar.articleDetails)),
      body: SectionBuilder<NewsDetail>(
        section: _section,
        onRetry: _load,
        builder: (context, detail) => ListView(
          padding: const EdgeInsets.all(16),
          children: [
            _articleCard(detail.article),
            if (!detail.hasAnalysis || detail.analysis == null)
              const Padding(
                padding: EdgeInsets.symmetric(vertical: 24),
                child: StatePanel(
                  icon: Icons.article_outlined,
                  title: Ar.noAnalysisYet,
                  color: AyzrColors.textMuted,
                ),
              )
            else
              AnalysisView(analysis: detail.analysis!),
          ],
        ),
      ),
    );
  }

  Widget _articleCard(NewsArticle a) => SectionCard(
        title: Ar.headline,
        icon: Icons.newspaper_rounded,
        children: [
          Text(
            a.headline.isEmpty ? Ar.unavailable : a.headline,
            style: const TextStyle(
              color: AyzrColors.textPrimary,
              fontSize: 15,
              fontWeight: FontWeight.w700,
              height: 1.5,
            ),
          ),
          const SizedBox(height: 10),
          InfoRow(label: Ar.source, value: Fmt.text(a.source)),
          InfoRow(
            label: Ar.updatedAt,
            value: Fmt.isoToLocal(a.publishedAtIso),
          ),
          if (a.bodyAvailable && a.bodyText != null) ...[
            const Divider(height: 22),
            Text(
              a.bodyText!,
              style: const TextStyle(
                color: AyzrColors.textPrimary,
                fontSize: 13,
                height: 1.6,
              ),
            ),
          ],
        ],
      );
}
