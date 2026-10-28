import 'package:flutter/foundation.dart';

import '../core/app_config.dart';
import '../data/api_client.dart';
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
  Section<LlmQuota> quota = const Section<LlmQuota>();
  Section<ActivityPage> activity = const Section<ActivityPage>();
  Section<ProviderPage> providers = const Section<ProviderPage>();
  Section<List<EventListItem>> events = const Section<List<EventListItem>>();

  bool goldOnly = false;

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

  /// Loads one event's expectation/result turns on demand (detail screen).
  Future<ApiResult<EventDetail>> loadEventDetail(String key) =>
      _client.eventDetail(key);

  /// Loads one article's analysis on demand (detail screen).
  Future<ApiResult<NewsDetail>> loadArticle(String id) =>
      _client.analyze(id);

  /// Refreshes everything the shell shows at once.
  Future<void> refreshAll() async {
    await Future.wait([loadHealth(), loadAnalysis()]);
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
}
