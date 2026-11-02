import 'package:flutter/material.dart';

import '../../core/strings_ar.dart';
import '../../core/theme.dart';

/// Formatting helpers. The central rule: a null/absent value renders as the
/// Arabic "غير متاح" placeholder, never as "0" or a blank that could be read as
/// a real reading.
class Fmt {
  const Fmt._();

  static const String unavailable = Ar.unavailable;

  static String number(double? v, {int decimals = 2}) =>
      v == null ? unavailable : v.toStringAsFixed(decimals);

  static String pct(double? v, {int decimals = 0}) =>
      v == null ? unavailable : '${(v * 100).toStringAsFixed(decimals)}٪';

  static String pctFraction(double? v, {int decimals = 1}) =>
      v == null ? unavailable : '${v.toStringAsFixed(decimals)}٪';

  static String text(String? v) =>
      (v == null || v.trim().isEmpty) ? unavailable : v;

  /// ISO-8601 UTC ("2026-10-16T12:00:00Z") to a compact Arabic-friendly label.
  /// Returns "غير متاح" when the input is absent or unparseable.
  static String isoToLocal(String? iso) {
    if (iso == null || iso.isEmpty) return unavailable;
    final dt = DateTime.tryParse(iso);
    if (dt == null) return unavailable;
    final local = dt.toLocal();
    String two(int n) => n.toString().padLeft(2, '0');
    return '${local.year}-${two(local.month)}-${two(local.day)} '
        '${two(local.hour)}:${two(local.minute)}';
  }
}

/// A labelled key/value row used across the analysis and news detail screens.
class InfoRow extends StatelessWidget {
  const InfoRow({super.key, required this.label, required this.value, this.color});

  final String label;
  final String value;
  final Color? color;

  @override
  Widget build(BuildContext context) {
    final isUnavailable = value == Fmt.unavailable;
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 6),
      child: Row(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          SizedBox(
            width: 132,
            child: Text(
              label,
              style: const TextStyle(
                color: AyzrColors.textMuted,
                fontSize: 13,
              ),
            ),
          ),
          Expanded(
            child: Text(
              value,
              style: TextStyle(
                color: color ??
                    (isUnavailable
                        ? AyzrColors.textMuted
                        : AyzrColors.textPrimary),
                fontSize: 13.5,
                fontWeight: FontWeight.w600,
              ),
            ),
          ),
        ],
      ),
    );
  }
}

/// A titled card grouping a set of rows. Used for the analysis sections.
class SectionCard extends StatelessWidget {
  const SectionCard({
    super.key,
    required this.title,
    required this.children,
    this.icon,
  });

  final String title;
  final List<Widget> children;
  final IconData? icon;

  @override
  Widget build(BuildContext context) {
    return Card(
      margin: const EdgeInsets.only(bottom: 14),
      child: Padding(
        padding: const EdgeInsets.all(16),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: [
            Row(
              children: [
                if (icon != null) ...[
                  Icon(icon, size: 18, color: AyzrColors.gold),
                  const SizedBox(width: 8),
                ],
                Text(
                  title,
                  style: const TextStyle(
                    color: AyzrColors.goldSoft,
                    fontSize: 15,
                    fontWeight: FontWeight.w700,
                  ),
                ),
              ],
            ),
            const Divider(height: 20),
            ...children,
          ],
        ),
      ),
    );
  }
}

/// A directional chip with an Arabic label and up/down/flat coloring.
class DirectionChip extends StatelessWidget {
  const DirectionChip({super.key, required this.direction});

  final String direction;

  static String labelAr(String d) {
    switch (d.toUpperCase()) {
      case 'UP':
      case 'LONG':
      case 'BULLISH':
        return Ar.directionUp;
      case 'DOWN':
      case 'SHORT':
      case 'BEARISH':
        return Ar.directionDown;
      case 'FLAT':
      case 'SIDEWAYS':
        return Ar.directionFlat;
      case 'NONE':
      case 'NO_TRADE':
      case 'UNKNOWN':
        return Ar.directionNone;
      default:
        return Ar.directionNone;
    }
  }

  static Color colorFor(String d) {
    switch (d.toUpperCase()) {
      case 'UP':
      case 'LONG':
      case 'BULLISH':
        return AyzrColors.up;
      case 'DOWN':
      case 'SHORT':
      case 'BEARISH':
        return AyzrColors.down;
      case 'FLAT':
      case 'SIDEWAYS':
        return AyzrColors.info;
      default:
        return AyzrColors.textMuted;
    }
  }

  @override
  Widget build(BuildContext context) {
    final color = colorFor(direction);
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 6),
      decoration: BoxDecoration(
        color: color.withOpacity(0.14),
        borderRadius: BorderRadius.circular(20),
        border: Border.all(color: color.withOpacity(0.5)),
      ),
      child: Text(
        labelAr(direction),
        style: TextStyle(
          color: color,
          fontWeight: FontWeight.w700,
          fontSize: 13,
        ),
      ),
    );
  }
}
