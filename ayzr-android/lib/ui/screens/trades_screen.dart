import 'package:flutter/material.dart';

import '../../core/theme.dart';
import '../../data/models/engines.dart';
import '../widgets/state_panel.dart';
import 'trade_detail_screen.dart';

/// الصفقات — every trade published by a horizon or by the news engine.
///
/// The list is read-only. Opening a trade shows its frozen origin, targets and
/// lifecycle events. A "أخذت الصفقة" action copies the signal into the LOCAL
/// journal (handled by the caller); nothing about that choice leaves the phone.
class TradesScreen extends StatelessWidget {
  const TradesScreen({
    super.key,
    required this.trades,
    required this.loading,
    required this.error,
    required this.onRetry,
    required this.onTakeTrade,
  });

  final List<Trade> trades;
  final bool loading;
  final String? error;
  final Future<void> Function() onRetry;
  final void Function(Trade) onTakeTrade;

  @override
  Widget build(BuildContext context) {
    if (loading && trades.isEmpty) {
      return const Center(
        child: CircularProgressIndicator(color: AyzrColors.gold),
      );
    }
    if (error != null && trades.isEmpty) {
      return StatePanel(
        icon: Icons.cloud_off_rounded,
        title: 'تعذّر تحميل الصفقات',
        detail: error!,
        actionLabel: 'إعادة المحاولة',
        onAction: () => onRetry(),
      );
    }
    if (trades.isEmpty) {
      return StatePanel(
        icon: Icons.inbox_rounded,
        title: 'لا توجد صفقات',
        detail: 'لم يُصدر أي محرك صفقة حتى الآن. ستظهر هنا فور صدورها.',
        actionLabel: 'إعادة المحاولة',
        onAction: () => onRetry(),
      );
    }
    return RefreshIndicator(
      onRefresh: onRetry,
      child: ListView.separated(
        padding: const EdgeInsets.all(16),
        itemCount: trades.length,
        separatorBuilder: (_, __) => const SizedBox(height: 12),
        itemBuilder: (context, i) => _tradeCard(context, trades[i]),
      ),
    );
  }

  Widget _tradeCard(BuildContext context, Trade t) {
    final accent = t.isBuy ? AyzrColors.up : AyzrColors.down;
    return Container(
      decoration: BoxDecoration(
        color: AyzrColors.navy,
        borderRadius: BorderRadius.circular(16),
        border: Border.all(color: AyzrColors.navyBorder),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          Padding(
            padding: const EdgeInsets.fromLTRB(16, 14, 16, 8),
            child: Row(
              children: [
                Container(
                  padding:
                      const EdgeInsets.symmetric(horizontal: 10, vertical: 5),
                  decoration: BoxDecoration(
                    color: accent.withOpacity(0.15),
                    borderRadius: BorderRadius.circular(8),
                    border: Border.all(color: accent.withOpacity(0.5)),
                  ),
                  child: Text(
                    t.directionLabelAr,
                    style: TextStyle(
                        color: accent,
                        fontSize: 12,
                        fontWeight: FontWeight.w800),
                  ),
                ),
                const SizedBox(width: 10),
                Text(t.sourceLabelAr,
                    style: const TextStyle(
                        color: AyzrColors.textPrimary,
                        fontSize: 15,
                        fontWeight: FontWeight.w700)),
                const Spacer(),
                if (t.isOpen)
                  _pill('نشطة', AyzrColors.warning)
                else
                  _pill(t.outcomeLabelAr, _outcomeColor(t.outcomeLabelAr)),
              ],
            ),
          ),
          Padding(
            padding: const EdgeInsets.fromLTRB(16, 0, 16, 6),
            child: Text(
              t.statusLabelAr,
              style: const TextStyle(color: AyzrColors.gold, fontSize: 13),
            ),
          ),
          Padding(
            padding: const EdgeInsets.fromLTRB(16, 4, 16, 12),
            child: Row(
              children: [
                _level('الدخول', t.entry, AyzrColors.textPrimary),
                _level('الوقف', t.stop, AyzrColors.down),
                _level('الأهداف', t.reachedCount.toDouble(),
                    AyzrColors.up,
                    raw: '${t.reachedCount}/${t.targets.length}'),
              ],
            ),
          ),
          const Divider(color: AyzrColors.navyBorder, height: 1),
          Row(
            children: [
              Expanded(
                child: TextButton.icon(
                  onPressed: () => Navigator.of(context).push(
                    MaterialPageRoute(
                      builder: (_) => TradeDetailScreen(
                        trade: t,
                        onTakeTrade: onTakeTrade,
                      ),
                    ),
                  ),
                  icon: const Icon(Icons.receipt_long_rounded, size: 18),
                  label: const Text('التفاصيل'),
                  style: TextButton.styleFrom(
                      foregroundColor: AyzrColors.textMuted),
                ),
              ),
              Container(
                  width: 1, height: 28, color: AyzrColors.navyBorder),
              Expanded(
                child: TextButton.icon(
                  onPressed: () => onTakeTrade(t),
                  icon: const Icon(Icons.check_circle_outline_rounded, size: 18),
                  label: const Text('أخذت الصفقة'),
                  style: TextButton.styleFrom(
                      foregroundColor: AyzrColors.gold),
                ),
              ),
            ],
          ),
        ],
      ),
    );
  }

  Color _outcomeColor(String label) {
    if (label.contains('رابحة')) return AyzrColors.up;
    if (label.contains('خاسرة')) return AyzrColors.down;
    return AyzrColors.textMuted;
  }

  Widget _level(String label, double value, Color color, {String? raw}) => Expanded(
        child: Column(
          children: [
            Text(label,
                style: const TextStyle(
                    color: AyzrColors.textMuted, fontSize: 11)),
            const SizedBox(height: 3),
            Text(
              raw ?? value.toStringAsFixed(2),
              style: TextStyle(
                  color: color, fontSize: 15, fontWeight: FontWeight.w700),
            ),
          ],
        ),
      );

  Widget _pill(String text, Color color) => Container(
        padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 4),
        decoration: BoxDecoration(
          color: color.withOpacity(0.15),
          borderRadius: BorderRadius.circular(20),
          border: Border.all(color: color.withOpacity(0.5)),
        ),
        child: Text(text,
            style: TextStyle(
                color: color, fontSize: 11, fontWeight: FontWeight.w700)),
      );
}
