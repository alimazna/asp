// AYZR ecosystem surface — model, client, state and widget tests.
//
// These exercise the real parsing and rendering code paths against the four
// ecosystem routes, using the fake HTTP client (no network). They pin the two
// honesty rules that matter most: a missing field is null (never a fabricated
// zero), and an uncalibrated confidence is never rendered as a probability.

import 'package:ayzr_app/core/strings_ar.dart';
import 'package:ayzr_app/data/api_result.dart';
import 'package:ayzr_app/data/models/ecosystem.dart';
import 'package:ayzr_app/state/app_state.dart';
import 'package:ayzr_app/ui/screens/ecosystem_screen.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

import 'test_helpers.dart';

void main() {
  group('Ecosystem models', () {
    test('SessionSnapshot parses sessions and flags unavailable zones', () {
      final s = SessionSnapshot.fromJson(
          (ecoSessionsPayload as Map).cast<String, dynamic>());
      expect(s.sessions, hasLength(3));
      expect(s.londonNewYorkOverlap, isTrue);
      expect(s.unavailableZones, ['Australia/Sydney']);
      expect(s.sessions[0].isOpen, isTrue);
      expect(s.sessions[0].inDst, isTrue);
      // The unavailable zone has a null local time, never "0".
      expect(s.sessions[2].localTime, isNull);
      expect(s.sessions[2].utcOffsetSeconds, isNull);
      expect(s.sessions[2].unavailable, isTrue);
    });

    test('EngineRegistry parses health honestly', () {
      final r = EngineRegistry.fromJson(
          (ecoEnginesPayload as Map).cast<String, dynamic>());
      expect(r.schemaVersion, 1);
      expect(r.engines, hasLength(2));
      expect(r.engines[0].isHealthy, isTrue);
      expect(r.engines[1].isUnavailable, isTrue);
      expect(r.engines[1].noteAr, isNotEmpty);
    });

    test('CycleResult parses a conflict and keeps levels', () {
      final c = CycleResult.fromJson(
          (ecoOpportunitiesPayload as Map).cast<String, dynamic>());
      expect(c.correlationId, 'cycle-1793104200');
      expect(c.dataQualityWarnings, isNotEmpty);
      final op = c.opportunities.single;
      expect(op.hasConflict, isTrue);
      expect(op.state, 'PUBLISH_WITH_CAUTION');
      expect(op.isPublished, isTrue);
      expect(op.entry!.price, 2650.5);
      expect(op.stopLoss!.price, 2644.0);
      expect(op.astraCorePresent, isTrue);
      expect(op.conflictNotesAr, isNotEmpty);
      expect(op.contributingEvents, hasLength(1));
    });

    test('uncalibrated confidence has a null probability, not zero', () {
      final c = CycleResult.fromJson(
          (ecoOpportunitiesPayload as Map).cast<String, dynamic>());
      final ev = c.opportunities.single.contributingEvents.single;
      expect(ev.confidenceCalibrated, isFalse);
      expect(ev.confidenceProbability, isNull);
      expect(ev.confidenceScore, 0.72);
    });

    test('LedgerPage parses rows and count', () {
      final p = LedgerPage.fromJson(
          (ecoLedgerPayload as Map).cast<String, dynamic>());
      expect(p.count, 2);
      expect(p.entries, hasLength(2));
      expect(p.entries[0].seq, 1);
      expect(p.entries[1].opportunityId, isNotEmpty);
    });

    test('a malformed payload degrades to empty, never throws', () {
      final s = SessionSnapshot.fromJson(const {'sessions': 'not-a-list'});
      expect(s.sessions, isEmpty);
      final c = CycleResult.fromJson(const {});
      expect(c.opportunities, isEmpty);
    });
  });

  group('Ecosystem API client', () {
    test('sessions() calls the right path and parses', () async {
      late Uri seen;
      final client = fakeClient(routeTable({
        '/api/v1/ayzr/ecosystem/sessions': (req) {
          seen = req.url;
          return enveloped(ecoSessionsPayload);
        },
      }));
      final r = await client.sessions();
      expect(r.isSuccess, isTrue);
      expect(r.value!.sessions, hasLength(3));
      expect(seen.path, '/api/v1/ayzr/ecosystem/sessions');
    });

    test('opportunities() parses a cycle', () async {
      final client = fakeClient(routeTable({
        '/api/v1/ayzr/ecosystem/opportunities': (_) =>
            enveloped(ecoOpportunitiesPayload),
      }));
      final r = await client.opportunities();
      expect(r.isSuccess, isTrue);
      expect(r.value!.opportunities, hasLength(1));
    });

    test('ledger() sends the limit query', () async {
      late Uri seen;
      final client = fakeClient(routeTable({
        '/api/v1/ayzr/ecosystem/ledger': (req) {
          seen = req.url;
          return enveloped(ecoLedgerPayload);
        },
      }));
      final r = await client.ledger(limit: 50);
      expect(r.isSuccess, isTrue);
      expect(seen.queryParameters['limit'], '50');
    });

    test('a 401 maps to unauthorized, not a crash', () async {
      final client = fakeClient(routeTable({
        '/api/v1/ayzr/ecosystem/engines': (_) =>
            errorBody('unauthorized', 'client token required', 401),
      }));
      final r = await client.ecosystemEngines();
      expect(r.isSuccess, isFalse);
      expect(r.failure!.isUnauthorized, isTrue);
    });

    test('an offline socket maps to offline', () async {
      final client = fakeClient((req) async {
        throw const FormatException('boom');
      });
      final r = await client.sessions();
      expect(r.isSuccess, isFalse);
      expect(r.failure!.kind, ApiFailureKind.offline);
    });
  });

  group('Ecosystem AppState', () {
    test('loadEcosystem settles every section on success', () async {
      final state = await stateWith(routeTable({
        '/api/v1/ayzr/ecosystem/sessions': (_) => enveloped(ecoSessionsPayload),
        '/api/v1/ayzr/ecosystem/engines': (_) => enveloped(ecoEnginesPayload),
        '/api/v1/ayzr/ecosystem/opportunities': (_) =>
            enveloped(ecoOpportunitiesPayload),
        '/api/v1/ayzr/ecosystem/ledger': (_) => enveloped(ecoLedgerPayload),
      }));
      await state.loadEcosystem();
      expect(state.sessions.phase, LoadPhase.ready);
      expect(state.ecosystemEngines.phase, LoadPhase.ready);
      expect(state.opportunities.phase, LoadPhase.ready);
      expect(state.ledger.phase, LoadPhase.ready);
      expect(state.opportunities.data!.opportunities, hasLength(1));
    });

    test('partial failure does not blank the other sections', () async {
      final state = await stateWith(routeTable({
        '/api/v1/ayzr/ecosystem/sessions': (_) => enveloped(ecoSessionsPayload),
        '/api/v1/ayzr/ecosystem/engines': (_) =>
            errorBody('dependency_unavailable', 'not configured', 503),
        '/api/v1/ayzr/ecosystem/opportunities': (_) =>
            enveloped(ecoOpportunitiesPayload),
        '/api/v1/ayzr/ecosystem/ledger': (_) => enveloped(ecoLedgerPayload),
      }));
      await state.loadEcosystem();
      expect(state.sessions.phase, LoadPhase.ready);
      expect(state.ecosystemEngines.phase, LoadPhase.error);
      expect(state.opportunities.phase, LoadPhase.ready);
      expect(state.ledger.phase, LoadPhase.ready);
    });
  });

  group('EcosystemScreen widgets', () {
    Widget wrap(AppState state) => MaterialApp(
          home: Directionality(
            textDirection: TextDirection.rtl,
            child: Scaffold(body: EcosystemScreen(state: state)),
          ),
        );

    testWidgets('renders session names, overlap and DST in Arabic', (t) async {
      final state = await stateWith(routeTable({
        '/api/v1/ayzr/ecosystem/sessions': (_) => enveloped(ecoSessionsPayload),
        '/api/v1/ayzr/ecosystem/engines': (_) => enveloped(ecoEnginesPayload),
        '/api/v1/ayzr/ecosystem/opportunities': (_) =>
            enveloped(ecoOpportunitiesPayload),
        '/api/v1/ayzr/ecosystem/ledger': (_) => enveloped(ecoLedgerPayload),
      }));
      await state.loadEcosystem();
      await t.pumpWidget(wrap(state));
      await t.pumpAndSettle();

      expect(find.text(Ar.ecoSessions), findsOneWidget);
      expect(find.text('جلسة لندن'), findsOneWidget);
      expect(find.text(Ar.ecoOverlap), findsOneWidget);
      expect(find.text(Ar.ecoSessions), findsOneWidget);
    });

    testWidgets('engines tab shows the unavailable note', (t) async {
      final state = await stateWith(routeTable({
        '/api/v1/ayzr/ecosystem/sessions': (_) => enveloped(ecoSessionsPayload),
        '/api/v1/ayzr/ecosystem/engines': (_) => enveloped(ecoEnginesPayload),
        '/api/v1/ayzr/ecosystem/opportunities': (_) =>
            enveloped(ecoOpportunitiesPayload),
        '/api/v1/ayzr/ecosystem/ledger': (_) => enveloped(ecoLedgerPayload),
      }));
      await state.loadEcosystem();
      await t.pumpWidget(wrap(state));
      await t.pumpAndSettle();

      await t.tap(find.text(Ar.ecoEngines));
      await t.pumpAndSettle();
      expect(find.text('تتبّع الاتجاه'), findsWidgets);
      // The microstructure engine's honest "no data source" note is shown.
      expect(find.textContaining('بنية السوق'), findsWidgets);
    });

    testWidgets('empty opportunities shows the honest Arabic message', (t) async {
      final state = await stateWith(routeTable({
        '/api/v1/ayzr/ecosystem/sessions': (_) => enveloped(ecoSessionsPayload),
        '/api/v1/ayzr/ecosystem/engines': (_) => enveloped(ecoEnginesPayload),
        '/api/v1/ayzr/ecosystem/opportunities': (_) => enveloped(const {
          'asOfUtc': 1793104200,
          'correlationId': 'cycle-x',
          'engineFailures': <String>[],
          'dataQualityWarnings': <String>[],
          'opportunities': <Map<String, Object?>>[],
        }),
        '/api/v1/ayzr/ecosystem/ledger': (_) => enveloped(ecoLedgerPayload),
      }));
      await state.loadEcosystem();
      await t.pumpWidget(wrap(state));
      await t.pumpAndSettle();

      await t.tap(find.text(Ar.ecoOpportunities));
      await t.pumpAndSettle();
      expect(find.text(Ar.ecoNoOpportunities), findsOneWidget);
      expect(find.text(Ar.ecoNoOpportunitiesHint), findsOneWidget);
    });

    testWidgets('a conflict opportunity renders both engine lists', (t) async {
      final state = await stateWith(routeTable({
        '/api/v1/ayzr/ecosystem/sessions': (_) => enveloped(ecoSessionsPayload),
        '/api/v1/ayzr/ecosystem/engines': (_) => enveloped(ecoEnginesPayload),
        '/api/v1/ayzr/ecosystem/opportunities': (_) =>
            enveloped(ecoOpportunitiesPayload),
        '/api/v1/ayzr/ecosystem/ledger': (_) => enveloped(ecoLedgerPayload),
      }));
      await state.loadEcosystem();
      await t.pumpWidget(wrap(state));
      await t.pumpAndSettle();

      await t.tap(find.text(Ar.ecoOpportunities));
      await t.pumpAndSettle();
      expect(find.textContaining(Ar.ecoAgreeing), findsOneWidget);
      expect(find.textContaining(Ar.ecoDisagreeing), findsOneWidget);
      // The uncalibrated note must be present, and no "%" probability shown.
      expect(find.text(Ar.ecoNoProbability), findsWidgets);
    });

    testWidgets('offline state shows the offline panel with retry', (t) async {
      final state = await stateWith(routeTable({
        '/api/v1/ayzr/ecosystem/sessions': (_) =>
            errorBody('dependency_unavailable', 'down', 503),
        '/api/v1/ayzr/ecosystem/engines': (_) => enveloped(ecoEnginesPayload),
        '/api/v1/ayzr/ecosystem/opportunities': (_) =>
            enveloped(ecoOpportunitiesPayload),
        '/api/v1/ayzr/ecosystem/ledger': (_) => enveloped(ecoLedgerPayload),
      }));
      await state.loadEcosystem();
      await t.pumpWidget(wrap(state));
      await t.pumpAndSettle();
      expect(find.text(Ar.errorGeneric), findsOneWidget);
    });
  });
}
