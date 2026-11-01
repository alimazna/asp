import 'package:flutter/foundation.dart';

import '../core/app_config.dart';
import '../data/api_client.dart';
import '../data/models/engines.dart';
import '../data/api_result.dart';
import '../data/models/analysis.dart';
import '../data/models/events.dart';
import '../data/models/health.dart';
import '../data/models/activity.dart';
import '../data/models/news.dart';
import 'settings_store.dart';

/// UI-facing phase of an async read. Distinguishing `empty` from `error` and
/// `offline` matters: the mission forbids showing a missing value as healthy or
/// zero, and each phase maps to a different Arabic state widget.
enum LoadPhase { idle, loading, ready, empty, error }

/// Generic async section state.
class Section<T> {
  const Section({
    this.phase = LoadPhase.idle,
    this.data,
    this.failure,
  });

  const Section.ready(this.data)
      : phase = LoadPhase.ready,
        failure = null;

  const Section.empty()
      : phase = LoadPhase.empty,
        data = null,
        failure = null;

  const Section.failed(this.failure)
      : phase = LoadPhase.error,
        data = null;

  final LoadPhase phase;
  final T? data;
  final ApiFailure? failure;

  Section<T> loading() => Section<T>(phase: LoadPhase.loading, data: data);
}

/// Application state: owns the API client, the loaded sections and the derived
/// freshness/offline flags. Kept deliberately small and synchronous in its
/// notifications so widget tests can pump deterministically.
class AppState extends ChangeNotifier {
  AppState({required SettingsStore settings, ApiClient? client})
      : _settings = settings,
        _client = client ?? ApiClient(config: settings.load());
 
  final SettingsStore _settings;
  final ApiClient _client;

  ApiClient get client => _client;
  AppConfig get config => _client.config;

  Section<HealthV1> health = const Section<HealthV1>();
  Section<AnalysisLatest> analysis = const Section<AnalysisLatest>();
  Section<List<NewsListItem>> news = const Section<List<NewsListItem>>();
  Section<List<HistoryEntry>> history = const Section<List<HistoryEntry>>();
  Section<List<NewsResult>> newsResults = const Section<List<NewsResult>>();
  Section<LlmQuota> quota = const Section<LlmQuota>();
  Section<ActivityPage> activity = const Section<ActivityPage>();
  Section<ProviderPage> providers = const Section<ProviderPage>();
  Section<List<EventListItem>> events = const Section<List<EventListItem>>();
  /// Upcoming events regardless of impact, for the "prepare for the next news"
  /// section. Distinct from [events] (which the user may filter to gold-relevant
  /// only): this list must show the nearest release whatever its impact level.
  Section<List<EventListItem>> upcomingEvents =
      const Section<List<EventListItem>>();
  // AYZR multi-horizon surface (read-only).
  Section<List<EngineInfo>> engines = const Section<List<EngineInfo>>();
  Section<List<Trade>> trades = const Section<List<Trade>>();
  Section<List<Trade>> openTrades = const Section<List<Trade>>();

  bool goldOnly = false;

  /// The active direction filter on the news-results log ('' = all). One of the
  /// four backend tokens or empty; the backend ignores an unknown value.
  String newsResultDirection = '';

  /// Which event scope is shown: `upcoming` (default) or `past`.
  String eventScope = 'upcoming';

  /// True when the AI provider's daily quota is spent and the backend is
  /// withholding calls. Drives the Arabic "analysis paused" banner. A temporary
  /// rate limit is surfaced separately and does not raise this flag.
  bool get aiQuotaExhausted => quota.data?.quotaExhausted ?? false;

  /// True when the provider is temporarily rate-limited (resolves in seconds).
  bool get aiRateLimited => quota.data?.rateLimited ?? false;

  /// True when the most recent health read failed with a connectivity error.
  bool get isOffline => health.phase == LoadPhase.error &&
      (health.failure?.isOffline ?? false);

  /// True when the backend reports it is running in a limited mode.
  bool get isDegraded {
    final h = health.data;
    return h != null && !h.isOk && !h.isOffline;
  }

  /// Stale when the freshness metadata exceeds the threshold, or the backend
  /// explicitly reports degraded data. Null freshness means "unknown", which is
  /// not treated as stale — it is shown as unavailable.
  bool get isStale {
    final a = analysis.data;
    if (a == null) return false;
    final f = a.meta.dataFreshnessSec;
    return f != null && f > 3600;
  }

  Future<void> loadHealth() async {
    health = health.loading();
    notifyListeners();
    final r = await _client.health();
    health = r.isSuccess
        ? Section<HealthV1>.ready(r.value!)
        : Section<HealthV1>.failed(r.failure!);
    notifyListeners();
  }

  Future<void> loadAnalysis() async {
    analysis = analysis.loading();
    notifyListeners();
    final r = await _client.latestAnalysis();
    analysis = r.isSuccess
        ? Section<AnalysisLatest>.ready(r.value!)
        : Section<AnalysisLatest>.failed(r.failure!);
    notifyListeners();
  }

  Future<void> loadNews({bool? goldOnly}) async {
    if (goldOnly != null) this.goldOnly = goldOnly;
    news = news.loading();
    notifyListeners();
    // The feed is the point of this screen, so it is loaded first and on its
    // own: a failure of the quota snapshot must not withhold the news list.
    final r = await _client.news(goldOnly: this.goldOnly);
    if (!r.isSuccess) {
      news = Section<List<NewsListItem>>.failed(r.failure!);
    } else {
      final items = r.value!;
      news = items.isEmpty
          ? const Section<List<NewsListItem>>.empty()
          : Section<List<NewsListItem>>.ready(items);
    }
    notifyListeners();
    // The quota banner is secondary and must never block or fail the feed.
    await loadQuota();
  }

  Future<void> loadHistory() async {
    history = history.loading();
    notifyListeners();
    final r = await _client.history();
    if (!r.isSuccess) {
      history = Section<List<HistoryEntry>>.failed(r.failure!);
    } else {
      final items = r.value!;
      history = items.isEmpty
          ? const Section<List<HistoryEntry>>.empty()
          : Section<List<HistoryEntry>>.ready(items);
    }
    notifyListeners();
  }

  /// Loads the news-results log — every persisted analysis joined to its
  /// article's full source metadata. Distinct from [loadHistory] (which is the
  /// ASTRA analysis history): this is the news-intelligence results log.
  Future<void> loadNewsResults({String? direction}) async {
    if (direction != null) newsResultDirection = direction;
    newsResults = newsResults.loading();
    notifyListeners();
    final r = await _client.newsResults(direction: newsResultDirection);
    if (!r.isSuccess) {
      newsResults = Section<List<NewsResult>>.failed(r.failure!);
    } else {
      final items = r.value!;
      newsResults = items.isEmpty
          ? const Section<List<NewsResult>>.empty()
          : Section<List<NewsResult>>.ready(items);
    }
    notifyListeners();
  }

  /// Loads the AI provider's availability snapshot. A failure here is not
  /// surfaced as an error state: the quota banner simply stays hidden, because
  /// an unknown quota must not be shown as "exhausted" or "ok".
  Future<void> loadQuota() async {
    final r = await _client.quota();
    quota = r.isSuccess
        ? Section<LlmQuota>.ready(r.value!)
        : const Section<LlmQuota>.empty();
    notifyListeners();
  }

  /// Loads the durable activity log — what the backend actually did.
  Future<void> loadActivity({String kind = ''}) async {
    activity = activity.loading();
    notifyListeners();
    final r = await _client.activity(kind: kind);
    if (!r.isSuccess) {
      activity = Section<ActivityPage>.failed(r.failure!);
    } else {
      final page = r.value!;
      activity = page.entries.isEmpty
          ? const Section<ActivityPage>.empty()
          : Section<ActivityPage>.ready(page);
    }
    notifyListeners();
  }

  /// Loads the AI provider chain status. Like the quota snapshot, a failure
  /// here is not an error state: an unknown provider status must not be shown
  /// as working or broken.
  Future<void> loadProviders() async {
    final r = await _client.providers();
    providers = r.isSuccess
        ? Section<ProviderPage>.ready(r.value!)
        : const Section<ProviderPage>.empty();
    notifyListeners();
  }

  /// Loads the scheduled macro events. `scope` is `upcoming` or `past`.
  Future<void> loadEvents({String? scope}) async {
    if (scope != null) eventScope = scope;
    events = events.loading();
    notifyListeners();
    final r = await _client.events(scope: eventScope);
    if (!r.isSuccess) {
      events = Section<List<EventListItem>>.failed(r.failure!);
    } else {
      final items = r.value!;
      events = items.isEmpty
          ? const Section<List<EventListItem>>.empty()
          : Section<List<EventListItem>>.ready(items);
    }
    notifyListeners();
  }

  /// Loads upcoming events regardless of impact, for the "prepare for the next
  /// news" section. The section must not hide a low-impact release, so
  /// `goldOnly` is false here. A failure is not fatal to the rest of the app:
  /// the section simply shows its unavailable state.
  Future<void> loadUpcomingEvents() async {
    upcomingEvents = upcomingEvents.loading();
    notifyListeners();
    final r = await _client.events(scope: 'upcoming', goldOnly: false);
    if (!r.isSuccess) {
      upcomingEvents = Section<List<EventListItem>>.failed(r.failure!);
    } else {
      final items = r.value!;
      upcomingEvents = items.isEmpty
          ? const Section<List<EventListItem>>.empty()
          : Section<List<EventListItem>>.ready(items);
    }
    notifyListeners();
  }

  /// The nearest upcoming event by scheduled time, or null when none is known.
  /// The backend already returns upcoming events nearest-first, but the client
  /// does not rely on that ordering: it selects the minimum itself so a change
  /// in server ordering cannot silently surface the wrong "next news".
  EventListItem? get nearestUpcomingEvent {
    final items = upcomingEvents.data;
    if (items == null || items.isEmpty) return null;
    EventListItem? best;
    for (final it in items) {
      final t = it.event.scheduledAtUtc;
      if (t == null) continue;
      if (best == null ||
          t < (best.event.scheduledAtUtc ?? 1 << 62)) {
        best = it;
      }
    }
    return best;
  }

  /// Loads one event's expectation/result turns on demand (detail screen).
  Future<ApiResult<EventDetail>> loadEventDetail(String key) =>
      _client.eventDetail(key);

  /// Loads one article's analysis on demand (detail screen).
  Future<ApiResult<NewsDetail>> loadArticle(String id) =>
      _client.analyze(id);

  /// Refreshes everything the shell shows at once.
  Future<void> refreshAll() async {
    await Future.wait([loadHealth(), loadAnalysis(), loadQuota()]);
  }

  Future<void> applyConfig(AppConfig next) async {
    await _settings.save(next);
    _client.updateConfig(next);
    notifyListeners();
  }

  @override
  void dispose() {
    _client.dispose();
    super.dispose();
  }

  /// GET /api/v1/ayzr/engines — the three horizon engines.
  Future<void> loadEngines() async {
    engines = engines.loading();
    notifyListeners();
    final r = await _client.engines();
    engines = r.isSuccess
        ? Section<List<EngineInfo>>.ready(r.value!)
        : Section<List<EngineInfo>>.failed(r.failure!);
    notifyListeners();
  }

  /// GET /api/v1/ayzr/trades — every published trade.
  Future<void> loadTrades() async {
    trades = trades.loading();
    notifyListeners();
    final r = await _client.trades(state: 'all');
    trades = r.isSuccess
        ? Section<List<Trade>>.ready(r.value!)
        : Section<List<Trade>>.failed(r.failure!);
    notifyListeners();
  }

  /// GET /api/v1/ayzr/trades?state=open — the currently active trades.
  Future<void> loadOpenTrades() async {
    openTrades = openTrades.loading();
    notifyListeners();
    final r = await _client.trades(state: 'open');
    openTrades = r.isSuccess
        ? Section<List<Trade>>.ready(r.value!)
        : Section<List<Trade>>.failed(r.failure!);
    notifyListeners();
  }
}
