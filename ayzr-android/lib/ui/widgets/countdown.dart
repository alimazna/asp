import 'dart:async';

import 'package:flutter/material.dart';

import '../../core/strings_ar.dart';
import '../../core/theme.dart';

/// A live countdown to a scheduled release.
///
/// It ticks once a second while mounted and derives its label purely from the
/// target time and the current time, so it can never show a stale or invented
/// value: when the target is in the past it says "انقضى", when it is now it says
/// "الآن", and otherwise it shows the remaining days/hours/minutes/seconds. A
/// null target renders nothing rather than a fabricated zero.
class EventCountdown extends StatefulWidget {
  const EventCountdown({
    super.key,
    required this.targetUtcSeconds,
    this.compact = false,
  });

  final int? targetUtcSeconds;
  final bool compact;

  @override
  State<EventCountdown> createState() => _EventCountdownState();
}

class _EventCountdownState extends State<EventCountdown> {
  Timer? _timer;
  DateTime _now = DateTime.now();

  @override
  void initState() {
    super.initState();
    _timer = Timer.periodic(const Duration(seconds: 1), (_) {
      if (mounted) setState(() => _now = DateTime.now());
    });
  }

  @override
  void dispose() {
    _timer?.cancel();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final seconds = widget.targetUtcSeconds;
    if (seconds == null) return const SizedBox.shrink();
    final target = DateTime.fromMillisecondsSinceEpoch(
      seconds * 1000,
      isUtc: true,
    );
    final remaining = target.difference(_now.toUtc());

    final label = _format(remaining);
    final color = remaining.isNegative
        ? AyzrColors.textMuted
        : remaining.inHours < 1
            ? AyzrColors.warning
            : AyzrColors.goldSoft;

    if (widget.compact) {
      return Row(
        mainAxisSize: MainAxisSize.min,
        children: [
          Icon(Icons.timer_outlined, size: 13, color: color),
          const SizedBox(width: 4),
          Text(
            label,
            style: TextStyle(
              color: color,
              fontSize: 12,
              fontWeight: FontWeight.w700,
            ),
          ),
        ],
      );
    }
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 6),
      decoration: BoxDecoration(
        color: color.withOpacity(0.12),
        borderRadius: BorderRadius.circular(10),
        border: Border.all(color: color.withOpacity(0.4)),
      ),
      child: Row(
        mainAxisSize: MainAxisSize.min,
        children: [
          Icon(Icons.timer_outlined, size: 15, color: color),
          const SizedBox(width: 6),
          Text(
            label,
            style: TextStyle(
              color: color,
              fontSize: 13,
              fontWeight: FontWeight.w700,
            ),
          ),
        ],
      ),
    );
  }

  /// The countdown label. Arabic reads "بعد ٢س ٣٠د" for a future target; a
  /// past target is "انقضى" and a target within the same second is "الآن".
  static String _format(Duration d) {
    if (d.isNegative || d == Duration.zero) {
      // A target that has passed (or is exactly now) is not counted down.
      return d.inSeconds.abs() < 1 ? Ar.eventCountdownNow : Ar.eventCountdownPassed;
    }
    final days = d.inDays;
    final hours = d.inHours % 24;
    final minutes = d.inMinutes % 60;
    final seconds = d.inSeconds % 60;
    if (days > 0) {
      return '${Ar.eventIn} $days ي $hours س';
    }
    if (hours > 0) {
      return '${Ar.eventIn} $hours س $minutes د';
    }
    if (minutes > 0) {
      return '${Ar.eventIn} $minutes د $seconds ث';
    }
    return '${Ar.eventIn} $seconds ث';
  }
}

