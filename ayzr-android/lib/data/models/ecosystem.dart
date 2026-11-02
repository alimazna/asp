// AYZR ecosystem models.
//
// Mirror the four ecosystem routes added on the backend:
//   GET /api/v1/ayzr/ecosystem/sessions
//   GET /api/v1/ayzr/ecosystem/engines
//   GET /api/v1/ayzr/ecosystem/opportunities
//   GET /api/v1/ayzr/ecosystem/ledger
//
// Every field is read straight from the backend response; nothing is derived,
// defaulted or invented on the client. Arabic labels arrive from the server so
// the app and the web observatory render identical wording. A missing field
// becomes null, never a fabricated zero.

class SessionState {
  const SessionState({
    required this.id,
    required this.nameAr,
    required this.zoneId,
    required this.phase,
    required this.phaseAr,
    required this.localTime,
    required this.utcOffsetSeconds,
    required this.inDst,
    required this.nextOpenUtc,
    required this.nextCloseUtc,
    required this.secondsToChange,
    required this.error,
  });

  final String id;
  final String nameAr;
  final String zoneId;
  final String phase;
  final String phaseAr;
  final String? localTime;
  final int? utcOffsetSeconds;
  final bool inDst;
  final int? nextOpenUtc;
  final int? nextCloseUtc;
  final int? secondsToChange;
  final String error;

  bool get isOpen => phase == 'OPEN';
  bool get unavailable => error.isNotEmpty;

  factory SessionState.fromJson(Map<String, dynamic> j) => SessionState(
        id: _str(j['id']),
        nameAr: _str(j['nameAr']),
        zoneId: _str(j['zoneId']),
        phase: _str(j['phase']),
        phaseAr: _str(j['phaseAr']),
        localTime: j['localTime'] is String ? j['localTime'] as String : null,
        utcOffsetSeconds: _intOrNull(j['utcOffsetSeconds']),
        inDst: j['inDst'] == true,
        nextOpenUtc: _intOrNull(j['nextOpenUtc']),
        nextCloseUtc: _intOrNull(j['nextCloseUtc']),
        secondsToChange: _intOrNull(j['secondsToChange']),
        error: _str(j['error']),
      );
}

class SessionSnapshot {
  const SessionSnapshot({
    required this.asOfUtc,
    required this.londonNewYorkOverlap,
    required this.openSessionIds,
    required this.unavailableZones,
    required this.sessions,
  });

  final int asOfUtc;
  final bool londonNewYorkOverlap;
  final List<String> openSessionIds;
  final List<String> unavailableZones;
  final List<SessionState> sessions;

  factory SessionSnapshot.fromJson(Map<String, dynamic> j) => SessionSnapshot(
        asOfUtc: _int(j['asOfUtc']),
        londonNewYorkOverlap: j['londonNewYorkOverlap'] == true,
        openSessionIds: _strList(j['openSessionIds']),
        unavailableZones: _strList(j['unavailableZones']),
        sessions: _mapList(j['sessions'], SessionState.fromJson),
      );
}

class EngineDescriptor {
  const EngineDescriptor({
    required this.engineId,
    required this.version,
    required this.strategyFamily,
    required this.strategyFamilyAr,
    required this.timeframe,
    required this.health,
    required this.healthAr,
    required this.noteAr,
  });

  final String engineId;
  final String version;
  final String strategyFamily;
  final String strategyFamilyAr;
  final String timeframe;
  final String health;
  final String healthAr;
  final String noteAr;

  bool get isHealthy => health == 'HEALTHY';
  bool get isUnavailable => health == 'UNAVAILABLE';

  factory EngineDescriptor.fromJson(Map<String, dynamic> j) => EngineDescriptor(
        engineId: _str(j['engineId']),
        version: _str(j['version']),
        strategyFamily: _str(j['strategyFamily']),
        strategyFamilyAr: _str(j['strategyFamilyAr']),
        timeframe: _str(j['timeframe']),
        health: _str(j['health']),
        healthAr: _str(j['healthAr']),
        noteAr: _str(j['noteAr']),
      );
}

class EngineRegistry {
  const EngineRegistry({required this.schemaVersion, required this.engines});

  final int schemaVersion;
  final List<EngineDescriptor> engines;

  factory EngineRegistry.fromJson(Map<String, dynamic> j) => EngineRegistry(
        schemaVersion: _int(j['contractSchemaVersion']),
        engines: _mapList(j['engines'], EngineDescriptor.fromJson),
      );
}

class PriceLevel {
  const PriceLevel({
    required this.price,
    required this.source,
    required this.noteAr,
  });

  final double price;
  final String source;
  final String noteAr;

  factory PriceLevel.fromJson(Map<String, dynamic> j) => PriceLevel(
        price: _dbl(j['price']),
        source: _str(j['source']),
        noteAr: _str(j['noteAr']),
      );
}

class EvidenceItem {
  const EvidenceItem({
    required this.code,
    required this.textAr,
    required this.value,
  });

  final String code;
  final String textAr;
  final double? value;

  factory EvidenceItem.fromJson(Map<String, dynamic> j) => EvidenceItem(
        code: _str(j['code']),
        textAr: _str(j['textAr']),
        value: _dblOrNull(j['value']),
      );
}

class Opportunity {
  const Opportunity({
    required this.opportunityId,
    required this.symbol,
    required this.timeframe,
    required this.direction,
    required this.directionAr,
    required this.anchorBarUtc,
    required this.createdAt,
    required this.updatedAt,
    required this.state,
    required this.stateAr,
    required this.rationaleAr,
    required this.entry,
    required this.stopLoss,
    required this.takeProfits,
    required this.levelSourceEngine,
    required this.agreeingEngines,
    required this.disagreeingEngines,
    required this.conflictNotesAr,
    required this.riskFlags,
    required this.astraCorePresent,
    required this.hasConflict,
    required this.contributingEvents,
  });

  final String opportunityId;
  final String symbol;
  final String timeframe;
  final String direction;
  final String directionAr;
  final int anchorBarUtc;
  final int createdAt;
  final int updatedAt;
  final String state;
  final String stateAr;
  final String rationaleAr;
  final PriceLevel? entry;
  final PriceLevel? stopLoss;
  final List<PriceLevel> takeProfits;
  final String levelSourceEngine;
  final List<String> agreeingEngines;
  final List<String> disagreeingEngines;
  final List<String> conflictNotesAr;
  final List<String> riskFlags;
  final bool astraCorePresent;
  final bool hasConflict;
  final List<EngineEvent> contributingEvents;

  /// A proposal is actionable only when the orchestrator published it.
  bool get isPublished => state == 'PUBLISH' || state == 'PUBLISH_WITH_CAUTION';

  factory Opportunity.fromJson(Map<String, dynamic> j) => Opportunity(
        opportunityId: _str(j['opportunityId']),
        symbol: _str(j['symbol']),
        timeframe: _str(j['timeframe']),
        direction: _str(j['direction']),
        directionAr: _str(j['directionAr']),
        anchorBarUtc: _int(j['anchorBarUtc']),
        createdAt: _int(j['createdAt']),
        updatedAt: _int(j['updatedAt']),
        state: _str(j['state']),
        stateAr: _str(j['stateAr']),
        rationaleAr: _str(j['rationaleAr']),
        entry: _mapOrNull(j['entry'], PriceLevel.fromJson),
        stopLoss: _mapOrNull(j['stopLoss'], PriceLevel.fromJson),
        takeProfits: _mapList(j['takeProfits'], PriceLevel.fromJson),
        levelSourceEngine: _str(j['levelSourceEngine']),
        agreeingEngines: _strList(j['agreeingEngines']),
        disagreeingEngines: _strList(j['disagreeingEngines']),
        conflictNotesAr: _strList(j['conflictNotesAr']),
        riskFlags: _strList(j['riskFlags']),
        astraCorePresent: j['astraCorePresent'] == true,
        hasConflict: j['hasConflict'] == true,
        contributingEvents: _mapList(j['contributingEvents'], EngineEvent.fromJson),
      );
}

/// One engine event as serialized by the backend.
class EngineEvent {
  const EngineEvent({
    required this.eventId,
    required this.engineId,
    required this.strategyFamilyAr,
    required this.eventType,
    required this.direction,
    required this.directionAr,
    required this.timeframe,
    required this.status,
    required this.statusAr,
    required this.dataQuality,
    required this.explanationAr,
    required this.reasonCodes,
    required this.supportingEvidence,
    required this.contradictingEvidence,
    required this.riskFlags,
    required this.confidenceCalibrated,
    required this.confidenceProbability,
    required this.confidenceScore,
    required this.immutableCoreOutput,
  });

  final String eventId;
  final String engineId;
  final String strategyFamilyAr;
  final String eventType;
  final String direction;
  final String directionAr;
  final String timeframe;
  final String status;
  final String statusAr;
  final String dataQuality;
  final String explanationAr;
  final List<String> reasonCodes;
  final List<EvidenceItem> supportingEvidence;
  final List<EvidenceItem> contradictingEvidence;
  final List<String> riskFlags;
  final bool confidenceCalibrated;
  final double? confidenceProbability;
  final double? confidenceScore;
  final bool immutableCoreOutput;

  factory EngineEvent.fromJson(Map<String, dynamic> j) {
    final conf = j['confidence'] is Map<String, dynamic>
        ? j['confidence'] as Map<String, dynamic>
        : const <String, dynamic>{};
    return EngineEvent(
      eventId: _str(j['eventId']),
      engineId: _str(j['engineId']),
      strategyFamilyAr: _str(j['strategyFamilyAr']),
      eventType: _str(j['eventType']),
      direction: _str(j['direction']),
      directionAr: _str(j['directionAr']),
      timeframe: _str(j['timeframe']),
      status: _str(j['status']),
      statusAr: _str(j['statusAr']),
      dataQuality: _str(j['dataQuality']),
      explanationAr: _str(j['explanationAr']),
      reasonCodes: _strList(j['reasonCodes']),
      supportingEvidence: _mapList(j['supportingEvidence'], EvidenceItem.fromJson),
      contradictingEvidence:
          _mapList(j['contradictingEvidence'], EvidenceItem.fromJson),
      riskFlags: _strList(j['riskFlags']),
      confidenceCalibrated: conf['calibrated'] == true,
      confidenceProbability: _dblOrNull(conf['probability']),
      confidenceScore: _dblOrNull(conf['score']),
      immutableCoreOutput: j['immutableCoreOutput'] == true,
    );
  }
}

class CycleResult {
  const CycleResult({
    required this.asOfUtc,
    required this.correlationId,
    required this.engineFailures,
    required this.dataQualityWarnings,
    required this.opportunities,
  });

  final int asOfUtc;
  final String correlationId;
  final List<String> engineFailures;
  final List<String> dataQualityWarnings;
  final List<Opportunity> opportunities;

  factory CycleResult.fromJson(Map<String, dynamic> j) => CycleResult(
        asOfUtc: _int(j['asOfUtc']),
        correlationId: _str(j['correlationId']),
        engineFailures: _strList(j['engineFailures']),
        dataQualityWarnings: _strList(j['dataQualityWarnings']),
        opportunities: _mapList(j['opportunities'], Opportunity.fromJson),
      );
}

class LedgerEntry {
  const LedgerEntry({
    required this.seq,
    required this.atUtc,
    required this.type,
    required this.typeAr,
    required this.correlationId,
    required this.opportunityId,
    required this.engineId,
    required this.reasonCode,
    required this.detailAr,
  });

  final int seq;
  final int atUtc;
  final String type;
  final String typeAr;
  final String correlationId;
  final String opportunityId;
  final String engineId;
  final String reasonCode;
  final String detailAr;

  factory LedgerEntry.fromJson(Map<String, dynamic> j) => LedgerEntry(
        seq: _int(j['seq']),
        atUtc: _int(j['atUtc']),
        type: _str(j['type']),
        typeAr: _str(j['typeAr']),
        correlationId: _str(j['correlationId']),
        opportunityId: _str(j['opportunityId']),
        engineId: _str(j['engineId']),
        reasonCode: _str(j['reasonCode']),
        detailAr: _str(j['detailAr']),
      );
}

class LedgerPage {
  const LedgerPage({required this.count, required this.entries});

  final int count;
  final List<LedgerEntry> entries;

  factory LedgerPage.fromJson(Map<String, dynamic> j) => LedgerPage(
        count: _int(j['count']),
        entries: _mapList(j['entries'], LedgerEntry.fromJson),
      );
}

// --- small coercion helpers (local, so the file is self-contained) --------

String _str(Object? v) => v is String ? v : '';
int _int(Object? v) => v is int ? v : (v is num ? v.toInt() : 0);
int? _intOrNull(Object? v) => v is int ? v : (v is num ? v.toInt() : null);
double _dbl(Object? v) => v is num ? v.toDouble() : 0.0;
double? _dblOrNull(Object? v) => v is num ? v.toDouble() : null;

List<String> _strList(Object? v) =>
    v is List ? v.whereType<String>().toList() : const [];

List<T> _mapList<T>(Object? v, T Function(Map<String, dynamic>) f) {
  if (v is! List) return const [];
  return v.whereType<Map<String, dynamic>>().map(f).toList();
}

T? _mapOrNull<T>(Object? v, T Function(Map<String, dynamic>) f) {
  if (v is Map<String, dynamic>) return f(v);
  return null;
}
