/// Result of a backend read: either data or a typed failure.
///
/// Every field the backend cannot source is represented as `null`/absent and
/// surfaces in the UI as "غير متاح" (unavailable). The client never substitutes
/// a zero, an empty string, or a default that could be mistaken for a reading.
library;

/// Failure taxonomy used to pick the right Arabic state in the UI.
enum ApiFailureKind {
  /// Network unreachable / DNS / timeout — the "offline" state.
  offline,

  /// HTTP 401 — the client token is missing or rejected.
  unauthorized,

  /// HTTP 429 — rate limited; a retry after backoff is appropriate.
  rateLimited,

  /// Any other non-2xx or a malformed body.
  server,

  /// The request was cancelled/superseded by a newer one.
  cancelled,
}

class ApiFailure {
  const ApiFailure({
    required this.kind,
    required this.message,
    this.statusCode,
  });

  final ApiFailureKind kind;
  final String message;
  final int? statusCode;

  bool get isOffline => kind == ApiFailureKind.offline;
  bool get isUnauthorized => kind == ApiFailureKind.unauthorized;
}

/// A successful value or a typed failure. Using an explicit result type (rather
/// than throwing) keeps every UI call site forced to handle the failure branch,
/// which is where the Arabic offline/error states are produced.
class ApiResult<T> {
  const ApiResult._(this.value, this.failure);

  const ApiResult.success(T value) : this._(value, null);
  const ApiResult.failure(ApiFailure failure) : this._(null, failure);

  final T? value;
  final ApiFailure? failure;

  bool get isSuccess => failure == null;

  /// Transforms a successful payload while preserving any failure unchanged.
  ApiResult<R> map<R>(R Function(T value) transform) {
    final v = value;
    if (failure != null || v == null) {
      return ApiResult<R>.failure(failure ??
          const ApiFailure(
            kind: ApiFailureKind.server,
            message: 'empty response',
          ));
    }
    return ApiResult<R>.success(transform(v));
  }
}
