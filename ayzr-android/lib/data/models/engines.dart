// AYZR multi-horizon models.
//
// Mirror GET /api/v1/ayzr/engines, /trades and /trades/detail. Every field is
// read straight from the backend; nothing is derived or invented on the
// client. Arabic labels come from the server so the app and the web
// observatory render identical wording.

/// One horizon engine and its frozen profile.
class EngineInfo {
  const EngineInfo({
    required this.id,
    required this.labelAr,
    required this.entryTimeframe,
    required this.contextTimeframes,
    required this.busy,
    required this.publishedCount,
    required this.rsiLong,
    required this.rsiShort,
    required this.stopAtr,
    required this.targetAtr,
    required this.minStrength,
    required this.requireContextAlignment,
    required this.allowRanging,
  });

  final String id; // SHORT | MEDIUM | LONG
  final String labelAr; // قصير المدى | متوسط المدى | بعيد المدى
  final String entryTimeframe;
  final List<String> contextTimeframes;
  final bool busy;
  final int publishedCount;
  final double rsiLong;
  final double rsiShort;
  final double stopAtr;
  final double targetAtr;
  final double minStrength;
  final bool requireContextAlignment;
  final bool allowRanging;

  factory EngineInfo.fromJson(Map<String, dynamic> j) => EngineInfo(
        id: _str(j['id']),
        labelAr: _str(j['label_ar']),
        entryTimeframe: _str(j['entry_timeframe']),
        contextTimeframes: _strList(j['context_timeframes']),
        busy: j['busy'] == true,
        publishedCount: _int(j['published_count']),
        rsiLong: _dbl(j['rsi_long']),
        rsiShort: _dbl(j['rsi_short']),
        stopAtr: _dbl(j['stop_atr']),
        targetAtr: _dbl(j['target_atr']),
        minStrength: _dbl(j['min_strength']),
        requireContextAlignment: j['require_context_alignment'] == true,
        allowRanging: j['allow_ranging'] == true,
      );
}

/// A take-profit level with its reached state.
class TradeTarget {
  const TradeTarget({
    required this.index,
    required this.price,
    required this.reached,
    required this.reachedAt,
  });

  final int index;
  final double price;
  final bool reached;
  final DateTime? reachedAt;

  factory TradeTarget.fromJson(Map<String, dynamic> j) => TradeTarget(
        index: _int(j['index']),
        price: _dbl(j['price']),
        reached: j['reached'] == true,
        reachedAt: _date(j['reached_at']),
      );
}

/// One lifecycle event.
class TradeEvent {
  const TradeEvent({
    required this.at,
    required this.statusLabelAr,
    required this.detailAr,
  });

  final DateTime? at;
  final String statusLabelAr;
  final String detailAr;

  factory TradeEvent.fromJson(Map<String, dynamic> j) => TradeEvent(
        at: _date(j['at']),
        statusLabelAr: _str(j['status_label_ar']),
        detailAr: _str(j['detail_ar']),
      );
}

/// A trade published by a horizon or by the news engine.
class Trade {
  const Trade({
    required this.id,
    required this.source,
    required this.sourceLabelAr,
    required this.symbol,
    required this.direction,
    required this.directionLabelAr,
    required this.entry,
    required this.stop,
    required this.statusLabelAr,
    required this.outcomeLabelAr,
    required this.isOpen,
    required this.publishedAt,
    required this.lastUpdate,
    required this.lastPrice,
    required this.entryTimeframe,
    required this.contextTimeframes,
    required this.targets,
    required this.rationale,
    required this.engineVersion,
    required this.closeReasonAr,
    required this.events,
  });

  final String id;
  final String source; // SHORT | MEDIUM | LONG | NEWS
  final String sourceLabelAr;
  final String symbol;
  final String direction; // BUY | SELL
  final String directionLabelAr;
  final double entry;
  final double stop;
  final String statusLabelAr;
  final String outcomeLabelAr;
  final bool isOpen;
  final DateTime? publishedAt;
  final DateTime? lastUpdate;
  final double lastPrice;
  final String entryTimeframe;
  final List<String> contextTimeframes;
  final List<TradeTarget> targets;
  final String rationale;
  final String engineVersion;
  final String closeReasonAr;
  final List<TradeEvent> events;

  bool get isBuy => direction == 'BUY';
  int get reachedCount => targets.where((t) => t.reached).length;
  List<TradeTarget> get pendingTargets =>
      targets.where((t) => !t.reached).toList();

  factory Trade.fromJson(Map<String, dynamic> j) => Trade(
        id: _str(j['id']),
        source: _str(j['source']),
        sourceLabelAr: _str(j['source_label_ar']),
        symbol: _str(j['symbol']),
        direction: _str(j['direction']),
        directionLabelAr: _str(j['direction_label_ar']),
        entry: _dbl(j['entry']),
        stop: _dbl(j['stop']),
        statusLabelAr: _str(j['status_label_ar']),
        outcomeLabelAr: _str(j['outcome_label_ar']),
        isOpen: j['is_open'] == true,
        publishedAt: _date(j['published_at']),
        lastUpdate: _date(j['last_update']),
        lastPrice: _dbl(j['last_price']),
        entryTimeframe: _str(j['entry_timeframe']),
        contextTimeframes: _strList(j['context_timeframes']),
        targets: _mapList(j['targets'], TradeTarget.fromJson),
        rationale: _str(j['rationale']),
        engineVersion: _str(j['engine_version']),
        closeReasonAr: _str(j['close_reason_ar']),
        events: _mapList(j['events'], TradeEvent.fromJson),
      );
}

// --- small coercion helpers shared by these models -----------------------

String _str(Object? v) => v is String ? v : '';

int _int(Object? v) => v is int ? v : (v is num ? v.toInt() : 0);

double _dbl(Object? v) => v is num ? v.toDouble() : 0.0;

DateTime? _date(Object? v) {
  if (v is int && v > 0) {
    return DateTime.fromMillisecondsSinceEpoch(v * 1000, isUtc: true);
  }
  return null;
}

List<String> _strList(Object? v) =>
    v is List ? v.whereType<String>().toList() : const [];

List<T> _mapList<T>(Object? v, T Function(Map<String, dynamic>) f) {
  if (v is! List) return const [];
  return v.whereType<Map<String, dynamic>>().map(f).toList();
}
