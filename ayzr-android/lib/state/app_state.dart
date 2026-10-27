import 'package:flutter/foundation.dart';

import '../core/app_config.dart';
import '../data/api_client.dart';
import '../data/api_result.dart';
import '../data/models/analysis.dart';
import '../data/models/health.dart';
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

  bool goldOnly = false;

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
    // Refresh the quota snapshot alongside the feed so the banner reflects the
    // same moment as the data. Both run concurrently.
    final results = await Future.wait([
      _client.news(goldOnly: this.goldOnly),
      _client.quota(),
    ]);
    final r = results[0] as ApiResult<List<NewsListItem>>;
    final q = results[1] as ApiResult<LlmQuota>;
    if (!r.isSuccess) {
      news = Section<List<NewsListItem>>.failed(r.failure!);
    } else {
      final items = r.value!;
      news = items.isEmpty
          ? const Section<List<NewsListItem>>.empty()
          : Section<List<NewsListItem>>.ready(items);
    }
    quota = q.isSuccess
        ? Section<LlmQuota>.ready(q.value!)
        : const Section<LlmQuota>.empty();
    notifyListeners();
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
