/// Runtime configuration for the backend connection.
///
/// The base URL and the read-only client bearer token are supplied at runtime
/// (build-time `--dart-define` or the in-app settings screen). No privileged
/// secret is ever embedded in source: the client token only unlocks the
/// read-only AYZR mobile surface (`/news`, `/news/analyze`, `/history`,
/// `/analysis/latest`, `/health`), never ingestion or admin routes.
class AppConfig {
  const AppConfig({
    required this.baseUrl,
    required this.clientToken,
    this.aiBaseUrl = '',
    this.aiToken = '',
  });

  /// e.g. `https://astra-server-production.up.railway.app`
  final String baseUrl;

  /// Read-only bearer token. Empty means "not configured": authenticated
  /// screens then show a clear configuration state instead of calling out.
  final String clientToken;

  /// Base URL of the separate AYZR Native AI Core service. Empty means the AI
  /// features are not configured, and their screens say so rather than calling
  /// out. It is a different host from [baseUrl] on purpose: the AI service is a
  /// separate deployment with its own failure domain.
  final String aiBaseUrl;

  /// Bearer token for the AI service. Separate from [clientToken] so the AI
  /// service can be rotated or revoked without touching backend access.
  final String aiToken;

  bool get hasToken => clientToken.trim().isNotEmpty;

  bool get hasAiToken => aiToken.trim().isNotEmpty;

  /// True when the AI service is configured well enough to call.
  bool get aiConfigured => aiBaseUrl.trim().isNotEmpty && hasAiToken;

  /// Config for the AI client: same shape, but pointed at the AI service.
  AppConfig get forAi => AppConfig(
        baseUrl: aiBaseUrl,
        clientToken: aiToken,
      );

  /// Normalized base: the host root, without a trailing slash and without a
  /// trailing `/api/v1` (or `/api`) segment.
  ///
  /// Every path in [endpoint] is already absolute from the host (`/api/v1/...`),
  /// so a base that itself ends in `/api/v1` would otherwise be doubled into
  /// `/api/v1/api/v1/...`. Accepting both spellings means a user can paste
  /// either `https://host` or `https://host/api/v1` and get the same result.
  String get normalizedBase {
    var b = baseUrl.trim();
    while (b.endsWith('/')) {
      b = b.substring(0, b.length - 1);
    }
    for (final suffix in const ['/api/v1', '/api']) {
      if (b.endsWith(suffix)) {
        b = b.substring(0, b.length - suffix.length);
        while (b.endsWith('/')) {
          b = b.substring(0, b.length - 1);
        }
        break;
      }
    }
    return b;
  }

  Uri endpoint(String path, [Map<String, String>? query]) {
    final base = Uri.parse(normalizedBase);
    // Paths are hosted-root absolute unless the base already carries a prefix
    // that is not `/api/v1`; join without introducing a double slash.
    final prefix = base.path.isEmpty || base.path == '/' ? '' : base.path;
    final joined = path.startsWith('/') ? '$prefix$path' : '$prefix/$path';
    return base.replace(
      path: joined,
      queryParameters: (query == null || query.isEmpty) ? null : query,
    );
  }

  AppConfig copyWith({
    String? baseUrl,
    String? clientToken,
    String? aiBaseUrl,
    String? aiToken,
  }) =>
      AppConfig(
        baseUrl: baseUrl ?? this.baseUrl,
        clientToken: clientToken ?? this.clientToken,
        aiBaseUrl: aiBaseUrl ?? this.aiBaseUrl,
        aiToken: aiToken ?? this.aiToken,
      );

  static const String defaultBaseUrl = String.fromEnvironment(
    'AYZR_BASE_URL',
    defaultValue: 'https://astra-server-production.up.railway.app',
  );

  static const String buildToken = String.fromEnvironment(
    'AYZR_CLIENT_TOKEN',
    defaultValue: '',
  );

  static const String defaultAiBaseUrl = String.fromEnvironment(
    'AYZR_AI_BASE_URL',
    defaultValue: '',
  );

  static const String buildAiToken = String.fromEnvironment(
    'AYZR_AI_TOKEN',
    defaultValue: '',
  );
}
