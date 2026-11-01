// AYZR-JOURNAL-001 — Local personal trading journal.
//
// This is the ONLY place a user's personal trading data lives. It is a local
// SQLite database on the device. Nothing here is ever sent to the backend:
// no sync, no upload, no telemetry. The journal keeps working with no network.
//
// The recorded entry price is the SIGNAL price published by the engine, not
// the live market price — the UI states that results computed from it are
// notional (based on the signal price), not broker-account P&L.

import 'dart:convert';
import 'dart:io';

import 'package:path/path.dart' as p;
import 'package:sqflite/sqflite.dart';

/// One journal entry: a signal the user chose to take personally.
class JournalEntry {
  const JournalEntry({
    this.id,
    required this.tradeId,
    required this.source,
    required this.sourceLabelAr,
    required this.direction,
    required this.directionLabelAr,
    required this.signalEntry,
    required this.stop,
    required this.targetsJson,
    required this.entryTimeframe,
    required this.takenAt,
    required this.signalPublishedAt,
    this.statusLabelAr = 'مفتوحة',
    this.outcomeLabelAr = 'قيد التنفيذ',
    this.closedAt,
    this.exitPrice,
    this.note = '',
  });

  final int? id;
  final String tradeId;
  final String source;
  final String sourceLabelAr;
  final String direction;
  final String directionLabelAr;
  final double signalEntry; // entry price AS PUBLISHED by the engine
  final double stop;
  final String targetsJson; // JSON list of target prices
  final String entryTimeframe;
  final DateTime takenAt;
  final DateTime signalPublishedAt;
  final String statusLabelAr;
  final String outcomeLabelAr;
  final DateTime? closedAt;
  final double? exitPrice;
  final String note;

  bool get isOpen => closedAt == null;

  List<double> get targets {
    final decoded = jsonDecode(targetsJson);
    if (decoded is! List) return const [];
    return decoded.whereType<num>().map((e) => e.toDouble()).toList();
  }

  /// Notional result in PRICE POINTS (not currency). Positive means a win.
  /// Uses the signal entry, so this is the signal-price result, not broker P&L.
  double get notionalPoints {
    final exit = exitPrice;
    if (exit == null) return 0.0;
    final diff = direction == 'BUY' ? exit - signalEntry : signalEntry - exit;
    return diff;
  }

  Map<String, Object?> toMap() => {
        if (id != null) 'id': id,
        'trade_id': tradeId,
        'source': source,
        'source_label_ar': sourceLabelAr,
        'direction': direction,
        'direction_label_ar': directionLabelAr,
        'signal_entry': signalEntry,
        'stop': stop,
        'targets_json': targetsJson,
        'entry_timeframe': entryTimeframe,
        'taken_at': takenAt.millisecondsSinceEpoch,
        'signal_published_at': signalPublishedAt.millisecondsSinceEpoch,
        'status_label_ar': statusLabelAr,
        'outcome_label_ar': outcomeLabelAr,
        'closed_at': closedAt?.millisecondsSinceEpoch,
        'exit_price': exitPrice,
        'note': note,
      };

  factory JournalEntry.fromMap(Map<String, Object?> m) => JournalEntry(
        id: m['id'] as int?,
        tradeId: (m['trade_id'] as String?) ?? '',
        source: (m['source'] as String?) ?? '',
        sourceLabelAr: (m['source_label_ar'] as String?) ?? '',
        direction: (m['direction'] as String?) ?? 'BUY',
        directionLabelAr: (m['direction_label_ar'] as String?) ?? '',
        signalEntry: (m['signal_entry'] as num?)?.toDouble() ?? 0.0,
        stop: (m['stop'] as num?)?.toDouble() ?? 0.0,
        targetsJson: (m['targets_json'] as String?) ?? '[]',
        entryTimeframe: (m['entry_timeframe'] as String?) ?? '',
        takenAt: DateTime.fromMillisecondsSinceEpoch(
            (m['taken_at'] as int?) ?? 0),
        signalPublishedAt: DateTime.fromMillisecondsSinceEpoch(
            (m['signal_published_at'] as int?) ?? 0),
        statusLabelAr: (m['status_label_ar'] as String?) ?? 'مفتوحة',
        outcomeLabelAr: (m['outcome_label_ar'] as String?) ?? 'قيد التنفيذ',
        closedAt: m['closed_at'] == null
            ? null
            : DateTime.fromMillisecondsSinceEpoch(m['closed_at'] as int),
        exitPrice: (m['exit_price'] as num?)?.toDouble(),
        note: (m['note'] as String?) ?? '',
      );
}

/// Aggregate performance over the journal. All figures are notional points.
class JournalStats {
  const JournalStats({
    required this.total,
    required this.wins,
    required this.losses,
    required this.breakeven,
    required this.netPoints,
    required this.grossWinPoints,
    required this.grossLossPoints,
    required this.bestSource,
  });

  final int total;
  final int wins;
  final int losses;
  final int breakeven;
  final double netPoints;
  final double grossWinPoints;
  final double grossLossPoints;
  final String bestSource;

  double get winRate => total == 0 ? 0 : wins / total;
}

/// Local-only journal backed by SQLite. Never talks to the network.
class TradeJournal {
  TradeJournal._(this._db);

  final Database _db;

  static const _fileName = 'ayzr_journal.db';

  static int _testSeq = 0;

  /// Open an isolated throwaway journal (tests only). Never used by the app.
  /// Each call gets its own file so two journals in one test run never share
  /// state — sqflite's bare `:memory:` path is process-wide, not per-open.
  static Future<TradeJournal> openInMemory() async {
    final dir = await Directory.systemTemp.createTemp('ayzr_journal_test_');
    final path = p.join(dir.path, 'j${_testSeq++}.db');
    final db = await openDatabase(path, version: 1, onCreate: _createSchema);
    return TradeJournal._(db);
  }

  static Future<void> _createSchema(Database db, int version) async {
    await db.execute('''
      CREATE TABLE journal (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        trade_id TEXT NOT NULL,
        source TEXT NOT NULL,
        source_label_ar TEXT NOT NULL,
        direction TEXT NOT NULL,
        direction_label_ar TEXT NOT NULL,
        signal_entry REAL NOT NULL,
        stop REAL NOT NULL,
        targets_json TEXT NOT NULL,
        entry_timeframe TEXT NOT NULL,
        taken_at INTEGER NOT NULL,
        signal_published_at INTEGER NOT NULL,
        status_label_ar TEXT NOT NULL,
        outcome_label_ar TEXT NOT NULL,
        closed_at INTEGER,
        exit_price REAL,
        note TEXT NOT NULL DEFAULT ''
      )
    ''');
    await db.execute('CREATE INDEX idx_journal_trade ON journal(trade_id)');
  }

  static Future<TradeJournal> open({String? overridePath}) async {
    final path = overridePath ??
        p.join(await getDatabasesPath(), _fileName);
    final db = await openDatabase(
      path,
      version: 1,
      onCreate: _createSchema,
    );
    return TradeJournal._(db);
  }

  /// Insert a journal entry. Duplicate trade ids are refused so tapping
  /// "أخذت الصفقة" twice does not create two records.
  Future<int?> add(JournalEntry e) async {
    final existing = await _db.query('journal',
        where: 'trade_id = ?', whereArgs: [e.tradeId], limit: 1);
    if (existing.isNotEmpty) return null; // already taken
    return _db.insert('journal', e.toMap());
  }

  Future<List<JournalEntry>> all() async {
    final rows = await _db.query('journal', orderBy: 'taken_at DESC');
    return rows.map(JournalEntry.fromMap).toList();
  }

  Future<List<JournalEntry>> openEntries() async {
    final rows = await _db.query('journal',
        where: 'closed_at IS NULL', orderBy: 'taken_at DESC');
    return rows.map(JournalEntry.fromMap).toList();
  }

  Future<bool> closeEntry(int id, double exitPrice,
      {String outcomeLabelAr = 'أغلقت يدوياً'}) async {
    final n = await _db.update(
      'journal',
      {
        'closed_at': DateTime.now().millisecondsSinceEpoch,
        'exit_price': exitPrice,
        'status_label_ar': 'مغلقة',
        'outcome_label_ar': outcomeLabelAr,
      },
      where: 'id = ?',
      whereArgs: [id],
    );
    return n > 0;
  }

  Future<int> count() async {
    final r = await _db.rawQuery('SELECT COUNT(*) AS c FROM journal');
    return (r.first['c'] as int?) ?? 0;
  }

  /// Aggregate stats over CLOSED entries (open ones have no result yet).
  Future<JournalStats> stats() async {
    final rows = await _db.query('journal', where: 'closed_at IS NOT NULL');
    final entries = rows.map(JournalEntry.fromMap).toList();
    int wins = 0, losses = 0, be = 0;
    double net = 0, gw = 0, gl = 0;
    final bySource = <String, double>{};
    for (final e in entries) {
      final pts = e.notionalPoints;
      net += pts;
      bySource[e.sourceLabelAr] = (bySource[e.sourceLabelAr] ?? 0) + pts;
      if (pts > 0) {
        wins++;
        gw += pts;
      } else if (pts < 0) {
        losses++;
        gl += pts;
      } else {
        be++;
      }
    }
    String best = '';
    double bestVal = double.negativeInfinity;
    bySource.forEach((k, v) {
      if (v > bestVal) {
        bestVal = v;
        best = k;
      }
    });
    return JournalStats(
      total: entries.length,
      wins: wins,
      losses: losses,
      breakeven: be,
      netPoints: net,
      grossWinPoints: gw,
      grossLossPoints: gl,
      bestSource: best,
    );
  }

  /// Export the whole journal as JSON (for a local backup file).
  Future<String> exportJson() async {
    final entries = await all();
    return jsonEncode(entries.map((e) => e.toMap()).toList());
  }

  /// Restore from a JSON backup. Existing rows are preserved (no wipe).
  Future<int> importJson(String data) async {
    final decoded = jsonDecode(data);
    if (decoded is! List) return 0;
    int added = 0;
    for (final raw in decoded) {
      if (raw is! Map) continue;
      final e = JournalEntry.fromMap(raw.cast<String, Object?>());
      final id = await add(e);
      if (id != null) added++;
    }
    return added;
  }

  Future<void> dispose() => _db.close();
}
