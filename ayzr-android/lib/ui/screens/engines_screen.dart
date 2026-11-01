import 'package:flutter/material.dart';

import '../../core/theme.dart';
import '../../data/models/engines.dart';
import '../widgets/state_panel.dart';

/// المحركات — the three independent horizon engines (SHORT / MEDIUM / LONG).
///
/// Every value comes from GET /api/v1/ayzr/engines. Nothing is computed on the
/// client. A busy engine is shown as holding an active trade; the empty state
/// is explicit rather than a fabricated row.
class EnginesScreen extends StatelessWidget {
  const EnginesScreen({
    super.key,
    required this.engines,
    required this.loading,
    required this.error,
    required this.onRetry,
  });

  final List<EngineInfo> engines;
  final bool loading;
  final String? error;
  final Future<void> Function() onRetry;

  @override
  Widget build(BuildContext context) {
    if (loading && engines.isEmpty) {
      return const Center(
        child: CircularProgressIndicator(color: AyzrColors.gold),
      );
    }
    if (error != null && engines.isEmpty) {
      return StatePanel(
        icon: Icons.cloud_off_rounded,
        title: 'تعذّر تحميل المحركات',
        detail: error!,
        actionLabel: 'إعادة المحاولة',
        onAction: () => onRetry(),
      );
    }
    if (engines.isEmpty) {
      return StatePanel(
        icon: Icons.memory_rounded,
        title: 'لا توجد محركات',
        detail: 'لم يُرجع الخادم أي محرك. تأكد من تشغيل طبقة AYZR.',
        actionLabel: 'إعادة المحاولة',
        onAction: () => onRetry(),
      );
    }
    return RefreshIndicator(
      onRefresh: onRetry,
      child: ListView(
        padding: const EdgeInsets.all(16),
        children: [
          for (final e in engines) _engineCard(context, e),
        ],
      ),
    );
  }

  Widget _engineCard(BuildContext context, EngineInfo e) {
    final busy = e.busy;
    final accent = busy ? AyzrColors.warning : AyzrColors.gold;
    return Container(
      margin: const EdgeInsets.only(bottom: 14),
      decoration: BoxDecoration(
        color: AyzrColors.navy,
        borderRadius: BorderRadius.circular(16),
        border: Border.all(color: AyzrColors.navyBorder),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          // Header: label + busy state.
          Container(
            padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 12),
            decoration: BoxDecoration(
              color: accent.withOpacity(0.10),
              borderRadius:
                  const BorderRadius.vertical(top: Radius.circular(16)),
            ),
            child: Row(
              children: [
                Icon(Icons.auto_graph_rounded, color: accent, size: 20),
                const SizedBox(width: 10),
                Text(
                  e.labelAr,
                  style: const TextStyle(
                    color: AyzrColors.textPrimary,
                    fontSize: 16,
                    fontWeight: FontWeight.w700,
                  ),
                ),
                const Spacer(),
                _pill(
                  busy ? 'مشغول بصفقة' : 'متاح',
                  busy ? AyzrColors.warning : AyzrColors.up,
                ),
              ],
            ),
          ),
          Padding(
            padding: const EdgeInsets.all(16),
            child: Column(
              children: [
                _row('الإطار الزمني للدخول', e.entryTimeframe),
                _row('أطر السياق',
                    e.contextTimeframes.isEmpty ? '—' : e.contextTimeframes.join(' · ')),
                const Divider(color: AyzrColors.navyBorder, height: 20),
                _row('وقف الخسارة', '${e.stopAtr} × ATR'),
                _row('الهدف', '${e.targetAtr} × ATR'),
                _row('نطاق RSI',
                    '${e.rsiShort.toStringAsFixed(0)} – ${e.rsiLong.toStringAsFixed(0)}'),
                _row('حد القوة', e.minStrength.toStringAsFixed(2)),
                _row('يتطلب توافق السياق',
                    e.requireContextAlignment ? 'نعم' : 'لا'),
                _row('يسمح بالتذبذب', e.allowRanging ? 'نعم' : 'لا'),
                const Divider(color: AyzrColors.navyBorder, height: 20),
                _row('الإشارات الصادرة', '${e.publishedCount}'),
              ],
            ),
          ),
        ],
      ),
    );
  }

  Widget _row(String label, String value) => Padding(
        padding: const EdgeInsets.symmetric(vertical: 5),
        child: Row(
          children: [
            Text(label,
                style: const TextStyle(
                    color: AyzrColors.textMuted, fontSize: 13)),
            const Spacer(),
            Text(value,
                style: const TextStyle(
                    color: AyzrColors.textPrimary,
                    fontSize: 13,
                    fontWeight: FontWeight.w600)),
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
