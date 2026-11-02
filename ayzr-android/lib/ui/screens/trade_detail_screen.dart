import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

import '../../core/theme.dart';
import '../../data/models/engines.dart';
import '../widgets/info_widgets.dart';

/// تفاصيل الصفقة — the full frozen origin, targets and lifecycle log, plus the
/// smart-copy action and the "أخذت الصفقة" action.
///
/// Smart copy always emits the LATEST saved state (current status, which
/// targets are hit, remaining targets, any close recommendation, the close
/// reason) — never the stale origin-only data. All copied text is Arabic.
class TradeDetailScreen extends StatelessWidget {
  const TradeDetailScreen({
    super.key,
    required this.trade,
    required this.onTakeTrade,
  });

  final Trade trade;
  final void Function(Trade) onTakeTrade;

  @override
  Widget build(BuildContext context) {
    final accent = trade.isBuy ? AyzrColors.up : AyzrColors.down;
    return Directionality(
      textDirection: TextDirection.rtl,
      child: Scaffold(
        backgroundColor: AyzrColors.navyDeep,
        appBar: AppBar(
          backgroundColor: AyzrColors.navy,
          foregroundColor: AyzrColors.textPrimary,
          title: Text('${trade.sourceLabelAr} — ${trade.directionLabelAr}'),
          actions: [
            IconButton(
              tooltip: 'نسخ آخر تحديث',
              icon: const Icon(Icons.copy_rounded),
              onPressed: () async {
                await Clipboard.setData(
                    ClipboardData(text: buildCopyText(trade)));
                if (context.mounted) {
                  ScaffoldMessenger.of(context).showSnackBar(
                    const SnackBar(content: Text('نُسخ آخر تحديث للصفقة')),
                  );
                }
              },
            ),
          ],
        ),
        body: ListView(
          padding: const EdgeInsets.all(16),
          children: [
            _statusCard(accent),
            _levelsCard(),
            _targetsCard(),
            _rationaleCard(),
            _eventsCard(),
            const SizedBox(height: 8),
            FilledButton.icon(
              onPressed: () => onTakeTrade(trade),
              icon: const Icon(Icons.check_circle_outline_rounded),
              label: const Text('أخذت الصفقة'),
              style: FilledButton.styleFrom(
                backgroundColor: AyzrColors.gold,
                foregroundColor: AyzrColors.navyDeep,
                minimumSize: const Size.fromHeight(48),
              ),
            ),
          ],
        ),
      ),
    );
  }

  /// Builds the Arabic smart-copy payload from the CURRENT state.
  static String buildCopyText(Trade t) {
    final b = StringBuffer();
    b.writeln('صفقة ${t.sourceLabelAr} — ${t.directionLabelAr}');
    b.writeln('الرمز: ${t.symbol}');
    b.writeln('الحالة الحالية: ${t.statusLabelAr}');
    b.writeln('النتيجة: ${t.outcomeLabelAr}');
    b.writeln('سعر الدخول: ${t.entry.toStringAsFixed(2)}');
    b.writeln('وقف الخسارة: ${t.stop.toStringAsFixed(2)}');
    final hit = t.targets.where((x) => x.reached).toList();
    final rem = t.pendingTargets;
    b.writeln('الأهداف المحققة: ${hit.isEmpty ? "لا يوجد" : hit.map((x) => x.price.toStringAsFixed(2)).join(" · ")}');
    b.writeln('الأهداف المتبقية: ${rem.isEmpty ? "لا يوجد" : rem.map((x) => x.price.toStringAsFixed(2)).join(" · ")}');
    b.writeln('الإطار الزمني: ${t.entryTimeframe}');
    b.writeln('أطر السياق: ${t.contextTimeframes.join(" · ")}');
    if (t.closeReasonAr.isNotEmpty) {
      b.writeln('سبب الإغلاق/التوصية: ${t.closeReasonAr}');
    }
    b.writeln('آخر سعر: ${t.lastPrice.toStringAsFixed(2)}');
    return b.toString().trim();
  }

  Widget _statusCard(Color accent) => SectionCard(
        title: 'الحالة',
        icon: Icons.info_outline_rounded,
        children: [
          InfoRow(label: 'الحالة', value: trade.statusLabelAr),
          InfoRow(label: 'النتيجة', value: trade.outcomeLabelAr),
          InfoRow(label: 'الاتجاه', value: trade.directionLabelAr),
          InfoRow(label: 'المحرك', value: trade.sourceLabelAr),
          InfoRow(
              label: 'آخر سعر',
              value: trade.lastPrice.toStringAsFixed(2)),
        ],
      );

  Widget _levelsCard() => SectionCard(
        title: 'المستويات',
        icon: Icons.straighten_rounded,
        children: [
          InfoRow(
              label: 'سعر الدخول',
              value: trade.entry.toStringAsFixed(2)),
          InfoRow(
              label: 'وقف الخسارة', value: trade.stop.toStringAsFixed(2)),
          InfoRow(label: 'الإطار الزمني', value: trade.entryTimeframe),
          InfoRow(
              label: 'أطر السياق',
              value: trade.contextTimeframes.isEmpty
                  ? '—'
                  : trade.contextTimeframes.join(' · ')),
        ],
      );

  Widget _targetsCard() => SectionCard(
        title: 'الأهداف',
        icon: Icons.flag_rounded,
        children: [
          if (trade.targets.isEmpty)
            const InfoRow(label: 'الأهداف', value: 'غير محددة'),
          for (final tg in trade.targets)
            InfoRow(
              label: 'الهدف ${tg.index + 1}',
              value:
                  '${tg.price.toStringAsFixed(2)} ${tg.reached ? "✓ تحقق" : "— لم يتحقق"}',
            ),
        ],
      );

  Widget _rationaleCard() => SectionCard(
        title: 'سبب الإصدار',
        icon: Icons.psychology_alt_rounded,
        children: [
          InfoRow(
              label: 'المنطق',
              value: trade.rationale.isEmpty ? 'غير متاح' : trade.rationale),
          InfoRow(label: 'إصدار المحرك', value: trade.engineVersion),
        ],
      );

  Widget _eventsCard() => SectionCard(
        title: 'سجل الأحداث',
        icon: Icons.history_rounded,
        children: [
          if (trade.events.isEmpty)
            const InfoRow(label: 'السجل', value: 'لا توجد أحداث'),
          for (final e in trade.events)
            InfoRow(label: e.statusLabelAr, value: e.detailAr),
        ],
      );
}
