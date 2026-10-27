/// Runtime configuration for the backend connection.
///
/// The base URL and the read-only client bearer token are supplied at runtime
/// (build-time `--dart-define` or the in-app settings screen). No privileged
/// secret is ever embedded in source: the client token only unlocks the
/// read-only AYZR mobile surface (`/news`, `/news/analyze`, `/history`,
/// `/analysis/latest`, `/health`), never ingestion or admin routes.
class AppConfig {
  const AppConfig({required this.baseUrl, required this.clientToken});

  /// e.g. `https://astra-server-production.up.railway.app`
  final String baseUrl;

  /// Read-only bearer token. Empty means "not configured": authenticated
  /// screens then show a clear configuration state instead of calling out.
  final String clientToken;

  bool get hasToken => clientToken.trim().isNotEmpty;

  /// Normalized base without a trailing slash, so path joins are unambiguous.
  String get normalizedBase {
    var b = baseUrl.trim();
    while (b.endsWith('/')) {
      b = b.substring(0, b.length - 1);
    }
    return b;
  }

  Uri endpoint(String path, [Map<String, String>? query]) {
    final base = Uri.parse(normalizedBase);
    return base.replace(
      path: '${base.path}$path',
      queryParameters: (query == null || query.isEmpty) ? null : query,
    );
  }

  AppConfig copyWith({String? baseUrl, String? clientToken}) => AppConfig(
        baseUrl: baseUrl ?? this.baseUrl,
        clientToken: clientToken ?? this.clientToken,
      );

  static const String defaultBaseUrl = String.fromEnvironment(
    'AYZR_BASE_URL',
    defaultValue: 'https://astra-server-production.up.railway.app',
  );

  static const String buildToken = String.fromEnvironment(
    'AYZR_CLIENT_TOKEN',
    defaultValue: '',
  );
}
