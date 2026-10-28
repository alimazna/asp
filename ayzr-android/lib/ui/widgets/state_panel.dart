import 'package:flutter/material.dart';

import '../../core/strings_ar.dart';
import '../../core/theme.dart';
import '../../data/api_result.dart';
import '../../state/app_state.dart';

/// A centered status panel used for every non-ready section state. Each state
/// has a distinct Arabic label so "no data" is never confused with "offline" or
/// "error" — a core honesty requirement of this app.
class StatePanel extends StatelessWidget {
  const StatePanel({
    super.key,
    required this.icon,
    required this.title,
    this.detail,
    this.color,
    this.actionLabel,
    this.onAction,
  });

  final IconData icon;
  final String title;
  final String? detail;
  final Color? color;
  final String? actionLabel;
  final VoidCallback? onAction;

  factory StatePanel.loading({String? title}) => StatePanel(
        icon: Icons.hourglass_top_rounded,
        title: title ?? Ar.loading,
        color: AyzrColors.goldSoft,
      );

  factory StatePanel.empty({String? title, String? detail}) => StatePanel(
        icon: Icons.inbox_rounded,
        title: title ?? Ar.empty,
        detail: detail,
        color: AyzrColors.textMuted,
      );

  factory StatePanel.offline({VoidCallback? onRetry}) => StatePanel(
        icon: Icons.cloud_off_rounded,
        title: Ar.offline,
        detail: Ar.offlineHint,
        color: AyzrColors.warning,
        actionLabel: onRetry != null ? Ar.retry : null,
        onAction: onRetry,
      );

  factory StatePanel.unauthorized({VoidCallback? onSettings}) => StatePanel(
        icon: Icons.key_off_rounded,
        title: 'يلزم إعداد رمز العميل',
        detail: 'افتح الإعدادات لإدخال عنوان الخادم ورمز العميل.',
        color: AyzrColors.warning,
        actionLabel: onSettings != null ? Ar.settings : null,
        onAction: onSettings,
      );

  factory StatePanel.error({String? detail, VoidCallback? onRetry}) =>
      StatePanel(
        icon: Icons.error_outline_rounded,
        title: Ar.errorGeneric,
        detail: detail,
        color: AyzrColors.down,
        actionLabel: onRetry != null ? Ar.retry : null,
        onAction: onRetry,
      );

  /// Maps a typed failure to the correct panel.
  static StatePanel forFailure(ApiFailure failure, {VoidCallback? onRetry}) {
    if (failure.isUnauthorized) return StatePanel.unauthorized();
    if (failure.isOffline) return StatePanel.offline(onRetry: onRetry);
    return StatePanel.error(detail: failure.message, onRetry: onRetry);
  }

  @override
  Widget build(BuildContext context) {
    final tint = color ?? AyzrColors.textMuted;
    return Center(
      child: Padding(
        padding: const EdgeInsets.all(28),
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            Icon(icon, size: 44, color: tint),
            const SizedBox(height: 14),
            Text(
              title,
              textAlign: TextAlign.center,
              style: const TextStyle(
                color: AyzrColors.textPrimary,
                fontSize: 16,
                fontWeight: FontWeight.w700,
              ),
            ),
            if (detail != null) ...[
              const SizedBox(height: 8),
              Text(
                detail!,
                textAlign: TextAlign.center,
                style: const TextStyle(
                  color: AyzrColors.textMuted,
                  fontSize: 13,
                  height: 1.5,
                ),
              ),
            ],
            if (actionLabel != null && onAction != null) ...[
              const SizedBox(height: 18),
              OutlinedButton.icon(
                onPressed: onAction,
                icon: const Icon(Icons.refresh_rounded, size: 18),
                label: Text(actionLabel!),
                style: OutlinedButton.styleFrom(
                  foregroundColor: AyzrColors.goldSoft,
                  side: const BorderSide(color: AyzrColors.gold),
                ),
              ),
            ],
          ],
        ),
      ),
    );
  }
}

/// Renders the correct widget for a [Section], invoking [builder] when ready.
class SectionBuilder<T> extends StatelessWidget {
  const SectionBuilder({
    super.key,
    required this.section,
    required this.builder,
    this.onRetry,
    this.emptyTitle,
    this.loadingTitle,
  });

  final Section<T> section;
  final Widget Function(BuildContext context, T data) builder;
  final VoidCallback? onRetry;
  final String? emptyTitle;
  final String? loadingTitle;

  @override
  Widget build(BuildContext context) {
    switch (section.phase) {
      case LoadPhase.idle:
      case LoadPhase.loading:
        return StatePanel.loading(title: loadingTitle);
      case LoadPhase.empty:
        return StatePanel.empty(title: emptyTitle);
      case LoadPhase.error:
        return StatePanel.forFailure(section.failure!, onRetry: onRetry);
      case LoadPhase.ready:
        return builder(context, section.data as T);
    }
  }
}

/// A small banner shown when data is stale or the backend is degraded. It never
/// hides the data; it annotates it.
class NoticeBanner extends StatelessWidget {
  const NoticeBanner({
    super.key,
    required this.icon,
    required this.text,
    required this.color,
  });

  final IconData icon;
  final String text;
  final Color color;

  factory NoticeBanner.stale() => const NoticeBanner(
        icon: Icons.schedule_rounded,
        text: '${Ar.stale} — ${Ar.staleHint}',
        color: AyzrColors.warning,
      );

  factory NoticeBanner.degraded() => const NoticeBanner(
        icon: Icons.warning_amber_rounded,
        text: Ar.degraded,
        color: AyzrColors.warning,
      );

  /// The permanent shadow-only notice. Rendered on the home screen so the
  /// no-execution posture is always visible to the user.
  factory NoticeBanner.shadow() => const NoticeBanner(
        icon: Icons.visibility_outlined,
        text: Ar.shadowOnly,
        color: AyzrColors.gold,
      );

  @override
  Widget build(BuildContext context) {
    return Container(
      width: double.infinity,
      margin: const EdgeInsets.only(bottom: 12),
      padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 10),
      decoration: BoxDecoration(
        color: color.withOpacity(0.12),
        borderRadius: BorderRadius.circular(10),
        border: Border.all(color: color.withOpacity(0.4)),
      ),
      child: Row(
        children: [
          Icon(icon, size: 18, color: color),
          const SizedBox(width: 8),
          Expanded(
            child: Text(
              text,
              style: TextStyle(color: color, fontSize: 12.5, height: 1.4),
            ),
          ),
        ],
      ),
    );
  }
}
