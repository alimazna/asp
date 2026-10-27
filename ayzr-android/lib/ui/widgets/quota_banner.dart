import 'package:flutter/material.dart';

import '../../core/strings_ar.dart';
import '../../core/theme.dart';
import '../../data/models/news.dart';

/// Arabic banner shown when the AI provider is unavailable for a *reason the
/// user should know about*: a spent daily quota (paused until the provider's
/// window resets) or a temporary burst limit (resolves in seconds).
///
/// It is deliberately distinct from a generic error: a spent quota is not a
/// fault the user can fix, so it reads as "paused", and the reset time is shown
/// only when the provider actually supplied one. When the reset is unknown the
/// banner says so rather than inventing a countdown.
class QuotaBanner extends StatelessWidget {
  const QuotaBanner({super.key, required this.quota});

  final LlmQuota quota;

  @override
  Widget build(BuildContext context) {
    if (quota.isOk) return const SizedBox.shrink();

    final exhausted = quota.quotaExhausted;
    final color = exhausted ? AyzrColors.warning : AyzrColors.gold;
    final title = exhausted ? Ar.quotaExhaustedTitle : Ar.rateLimitedTitle;
    final body = exhausted ? Ar.quotaExhaustedBody : Ar.rateLimitedBody;

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
          Icon(exhausted ? Icons.hourglass_bottom_rounded : Icons.schedule_rounded,
              color: color, size: 20),
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
              ],
            ),
          ),
        ],
      ),
    );
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
