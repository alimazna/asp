import 'package:flutter/material.dart';

import '../../core/strings_ar.dart';
import '../../core/theme.dart';
import '../../data/models/news.dart';

/// Arabic banner shown when the AI provider is unavailable for a *reason the
/// user should know about*. It distinguishes four situations that call for
/// different user action, because collapsing them into one message is
/// dishonest:
///
///   * all providers down  — no credential can answer. Retrying will not help;
///                           the credential must be rotated. The reason is
///                           stated (rejected key vs spent budget) when known.
///   * quota exhausted     — the last chain result was a spent daily budget.
///                           Paused until the provider's window resets.
///   * rate limited        — a temporary burst limit; resolves in seconds.
///   * degraded            — at least one provider is down but another answers,
///                           so analysis still works and this is informational.
///
/// The reset time is shown only when the provider actually supplied one. When
/// it is unknown the banner says so rather than inventing a countdown.
class QuotaBanner extends StatelessWidget {
  const QuotaBanner({super.key, required this.quota});

  final LlmQuota quota;

  @override
  Widget build(BuildContext context) {
    if (quota.isOk && !quota.allProvidersDown && quota.providersUnavailable == 0) {
      return const SizedBox.shrink();
    }

    final down = quota.allProvidersDown;
    final exhausted = quota.quotaExhausted || quota.allProvidersQuotaExhausted;

    // A rejected key is NOT "busy, retry shortly": no retry will ever clear it,
    // so it must read as a stop, not a wait. This was the previous behaviour and
    // it told the user to retry when the real fix was to rotate the credential.
    final String title;
    final String body;
    final Color color;
    final IconData icon;
    if (down && exhausted) {
      title = Ar.allExhaustedTitle;
      body = Ar.allExhaustedBody;
      color = AyzrColors.warning;
      icon = Icons.hourglass_disabled_rounded;
    } else if (down) {
      title = Ar.allDownTitle;
      body = Ar.allDownBody;
      color = AyzrColors.down;
      icon = Icons.cloud_off_rounded;
    } else if (exhausted) {
      title = Ar.quotaExhaustedTitle;
      body = Ar.quotaExhaustedBody;
      color = AyzrColors.warning;
      icon = Icons.hourglass_bottom_rounded;
    } else if (quota.rateLimited) {
      title = Ar.rateLimitedTitle;
      body = Ar.rateLimitedBody;
      color = AyzrColors.gold;
      icon = Icons.schedule_rounded;
    } else {
      // Degraded: some providers are down, but analysis still works.
      title = Ar.degradedTitle;
      body = Ar.degradedBody;
      color = AyzrColors.gold;
      icon = Icons.info_outline_rounded;
    }

    return Container(
      margin: const EdgeInsets.fromLTRB(16, 8, 16, 0),
      padding: const EdgeInsets.all(14),
      decoration: BoxDecoration(
        color: color.withOpacity(0.10),
        borderRadius: BorderRadius.circular(14),
        border: Border.all(color: color.withOpacity(0.4)),
      ),
      child: Row(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Icon(icon, color: color, size: 20),
          const SizedBox(width: 10),
          Expanded(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text(
                  title,
                  style: TextStyle(
                    color: color,
                    fontSize: 13.5,
                    fontWeight: FontWeight.w700,
                  ),
                ),
                const SizedBox(height: 6),
                Text(
                  body,
                  style: const TextStyle(
                    color: AyzrColors.textPrimary,
                    fontSize: 12.5,
                    height: 1.5,
                  ),
                ),
                if (down && !exhausted) ...[
                  const SizedBox(height: 6),
                  Text(
                    _downReason(),
                    style: const TextStyle(
                      color: AyzrColors.textMuted,
                      fontSize: 12,
                      height: 1.4,
                    ),
                  ),
                ],
                if (exhausted) ...[
                  const SizedBox(height: 6),
                  Text(
                    _resetLabel(),
                    style: const TextStyle(
                      color: AyzrColors.textMuted,
                      fontSize: 12,
                      height: 1.4,
                    ),
                  ),
                ],
                if (quota.providersUnavailable > 0 &&
                    quota.providers.isNotEmpty) ...[
                  const SizedBox(height: 8),
                  Text(
                    _unavailableList(),
                    style: const TextStyle(
                      color: AyzrColors.textMuted,
                      fontSize: 11.5,
                      height: 1.5,
                    ),
                  ),
                ],
              ],
            ),
          ),
        ],
      ),
    );
  }

  /// Why the whole chain is down, from the per-provider outcomes. Only states a
  /// cause the data supports; otherwise it says the cause is unknown.
  String _downReason() {
    final unavailable =
        quota.providers.where((p) => p.unavailable).toList();
    if (unavailable.isNotEmpty &&
        unavailable.every((p) => p.lastOutcome == 'auth_failure')) {
      return Ar.allDownReasonAuth;
    }
    if (unavailable.isNotEmpty &&
        unavailable.every((p) => p.lastOutcome == 'quota_exhausted')) {
      return Ar.allDownReasonQuota;
    }
    return Ar.allDownReasonUnknown;
  }

  /// Names the providers that are down, so the owner knows which key to fix.
  String _unavailableList() {
    final names = quota.providers
        .where((p) => p.unavailable)
        .map((p) => p.provider)
        .where((n) => n.isNotEmpty)
        .toList();
    if (names.isEmpty) return '';
    return '${Ar.providersDownList}: ${names.join('، ')}';
  }

  /// Honest reset label: a concrete time only when the provider gave one.
  String _resetLabel() {
    final until = quota.cooldownUntilUtc;
    if (until == null || until <= 0) return Ar.quotaResetUnknown;
    final dt = DateTime.fromMillisecondsSinceEpoch(until * 1000, isUtc: true)
        .toLocal();
    String two(int n) => n.toString().padLeft(2, '0');
    return '${Ar.quotaResetAt} ${two(dt.hour)}:${two(dt.minute)}';
  }
}
