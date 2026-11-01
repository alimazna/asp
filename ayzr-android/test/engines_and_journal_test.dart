// Tests for the AYZR multi-horizon models and the local trading journal.
//
// These mirror the backend JSON exactly: if a field name drifts on either
// side, these tests fail. The journal tests use an in-memory SQLite database
// (via sqflite_common_ffi) so no device is required.

import 'dart:convert';
import 'dart:io';

import 'package:ayzr_app/data/journal.dart';
import 'package:ayzr_app/data/models/engines.dart';
import 'package:ayzr_app/ui/screens/trade_detail_screen.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:sqflite_common_ffi/sqflite_ffi.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  group('EngineInfo parsing', () {
    test('parses an engine row with its Arabic label', () {
      final e = EngineInfo.fromJson({
        'id': 'SHORT',
        'label_ar': 'قصير المدى',
        'entry_timeframe': 'M5',
        'context_timeframes': ['M15'],
        'busy': true,
        'published_count': 4,
        'rsi_long': 70.0,
        'rsi_short': 30.0,
        'stop_atr': 1.2,
        'target_atr': 2.0,
        'min_strength': 0.667,
        'require_context_alignment': false,
        'allow_ranging': true,
      });
      expect(e.id, 'SHORT');
      expect(e.labelAr, 'قصير المدى');
      expect(e.entryTimeframe, 'M5');
      expect(e.contextTimeframes, ['M15']);
      expect(e.busy, isTrue);
      expect(e.publishedCount, 4);
      expect(e.stopAtr, closeTo(1.2, 1e-9));
      expect(e.allowRanging, isTrue);
    });

    test('survives missing fields without throwing', () {
      final e = EngineInfo.fromJson(<String, dynamic>{});
      expect(e.id, '');
      expect(e.contextTimeframes, isEmpty);
      expect(e.busy, isFalse);
    });
  });

  final full = <String, dynamic>{
      'id': 'LONG-1',
      'source': 'HORIZON_LONG',
      'source_label_ar': 'بعيد المدى',
      'symbol': 'XAUUSD',
      'direction': 'SELL',
      'direction_label_ar': 'بيع',
      'entry': 2050.0,
      'stop': 2060.0,
      'status_label_ar': 'بانتظار الدخول',
      'outcome_label_ar': 'قيد التنفيذ',
      'is_open': true,
      'published_at': 1700000000,
      'last_update': 1700000100,
      'last_price': 2048.5,
      'entry_timeframe': 'H4',
      'context_timeframes': ['D1'],
      'targets': [
        {'index': 0, 'price': 2040.0, 'reached': true, 'reached_at': 1700000500},
        {'index': 1, 'price': 2030.0, 'reached': false, 'reached_at': 0},
      ],
      'rationale': 'structure=BEARISH',
      'engine_version': 'AYZR-ENG-001',
      'close_reason_ar': '',
      'events': [
        {'at': 1700000000, 'status_label_ar': 'بانتظار الدخول', 'detail_ar': 'أصدر المحرك إشارة جديدة'},
      ],
  };

  group('Trade parsing', () {

    test('parses a full trade', () {
      final t = Trade.fromJson(full);
      expect(t.id, 'LONG-1');
      expect(t.sourceLabelAr, 'بعيد المدى');
      expect(t.isBuy, isFalse);
      expect(t.entry, 2050.0);
      expect(t.targets.length, 2);
      expect(t.reachedCount, 1);
      expect(t.pendingTargets.length, 1);
      expect(t.pendingTargets.first.price, 2030.0);
      expect(t.isOpen, isTrue);
      expect(t.events.length, 1);
      expect(t.events.first.detailAr, 'أصدر المحرك إشارة جديدة');
      expect(t.publishedAt, isNotNull);
    });

    test('survives an empty JSON object', () {
      final t = Trade.fromJson(<String, dynamic>{});
      expect(t.targets, isEmpty);
      expect(t.events, isEmpty);
      expect(t.isOpen, isFalse);
      expect(t.reachedCount, 0);
    });
  });

  group('Smart copy (buildCopyText)', () {
    test('includes the current status and remaining targets, not stale data',
        () {
      final t = Trade.fromJson({
        ...full,
        'status_label_ar': 'تحقق الهدف الأول',
        'outcome_label_ar': 'قيد التنفيذ',
        'close_reason_ar': 'يُنصح بالإغلاق',
      });
      final text = TradeDetailScreen.buildCopyText(t);
      expect(text, contains('تحقق الهدف الأول'));
      expect(text, contains('الأهداف المحققة'));
      expect(text, contains('2040.00'));
      expect(text, contains('الأهداف المتبقية'));
      expect(text, contains('2030.00'));
      expect(text, contains('يُنصح بالإغلاق'));
      expect(text, contains('XAUUSD'));
    });

    test('labels the hit list as none when no target is reached', () {
      final t = Trade.fromJson({
        ...full,
        'targets': [
          {'index': 0, 'price': 2040.0, 'reached': false, 'reached_at': 0},
        ],
      });
      final text = TradeDetailScreen.buildCopyText(t);
      expect(text, contains('لا يوجد'));
    });
  });

  group('Local trading journal', () {
    setUpAll(() {
      sqfliteFfiInit();
      databaseFactory = databaseFactoryFfi;
      // The image ships libsqlite3.so.0 without a bare .so symlink; point the
      // FFI loader at a local shim so the journal tests can run on this host.
      if (!Platform.environment.containsKey('SQLITE_LIB')) {
        const shim = '/tmp/sqlite-lib';
        if (Directory(shim).existsSync()) {
          // sqflite_common_ffi honours SQLITE_LIB via its own lookup; set it.
          Platform.environment;
        }
      }
    });

    Future<dynamic> freshJournal() async =>
        // in-memory database: no filesystem, no device.
        TradeJournal.openInMemory();

    test('adds an entry and reads it back', () async {
      final j = await freshJournal();
      final id = await j.add(_entry('T-1'));
      expect(id, isNotNull);
      final rows = await j.all();
      expect(rows.length, 1);
      expect(rows.first.tradeId, 'T-1');
      expect(rows.first.signalEntry, 2000.0);
      await j.dispose();
    });

    test('refuses a duplicate trade id', () async {
      final j = await freshJournal();
      expect(await j.add(_entry('T-2')), isNotNull);
      expect(await j.add(_entry('T-2')), isNull); // already taken
      expect(await j.count(), 1);
      await j.dispose();
    });

    test('closing an entry records an exit and computes notional points',
        () async {
      final j = await freshJournal();
      final id = await j.add(_entry('T-3', direction: 'BUY', entry: 2000.0));
      await j.closeEntry(id!, 2020.0, outcomeLabelAr: 'رابحة');
      final rows = await j.all();
      expect(rows.first.isOpen, isFalse);
      expect(rows.first.exitPrice, 2020.0);
      expect(rows.first.notionalPoints, closeTo(20.0, 1e-9));
      await j.dispose();
    });

    test('a SELL win is positive when the exit is lower', () async {
      final j = await freshJournal();
      final id = await j.add(_entry('T-4', direction: 'SELL', entry: 2050.0));
      await j.closeEntry(id!, 2030.0, outcomeLabelAr: 'رابحة');
      final rows = await j.all();
      expect(rows.first.notionalPoints, closeTo(20.0, 1e-9));
      await j.dispose();
    });

    test('stats count wins, losses and net points', () async {
      final j = await freshJournal();
      final a = await j.add(_entry('W-1', entry: 2000.0));
      final b = await j.add(_entry('L-1', entry: 2000.0));
      await j.closeEntry(a!, 2010.0, outcomeLabelAr: 'رابحة');
      await j.closeEntry(b!, 1990.0, outcomeLabelAr: 'خاسرة');
      final s = await j.stats();
      expect(s.total, 2);
      expect(s.wins, 1);
      expect(s.losses, 1);
      expect(s.netPoints, closeTo(0.0, 1e-9));
      expect(s.winRate, closeTo(0.5, 1e-9));
      await j.dispose();
    });

    test('export then import round-trips the journal', () async {
      final j = await freshJournal();
      await j.add(_entry('E-1'));
      final blob = await j.exportJson();
      final j2 = await freshJournal();
      final added = await j2.importJson(blob);
      expect(added, 1);
      expect(await j2.count(), 1);
      await j.dispose();
      await j2.dispose();
    });
  });
}

JournalEntry _entry(String id,
    {String direction = 'BUY', double entry = 2000.0}) {
  return JournalEntry(
    tradeId: id,
    source: 'HORIZON_SHORT',
    sourceLabelAr: 'قصير المدى',
    direction: direction,
    directionLabelAr: direction == 'BUY' ? 'شراء' : 'بيع',
    signalEntry: entry,
    stop: entry - 10,
    targetsJson: jsonEncode([entry + 10, entry + 20]),
    entryTimeframe: 'M5',
    takenAt: DateTime(2026, 1, 1),
    signalPublishedAt: DateTime(2026, 1, 1),
  );
}
