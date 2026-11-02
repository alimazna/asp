import 'package:shared_preferences/shared_preferences.dart';

import '../core/app_config.dart';

/// Persists the backend URL and the read-only client token on the device.
///
/// The token is stored in shared_preferences and never logged. It is a
/// read-only credential for the private mobile surface, not a privileged
/// secret, and it is never compiled into the binary.
class SettingsStore {
  SettingsStore(this._prefs);

  final SharedPreferences _prefs;

  static const _kBaseUrl = 'ayzr.base_url';
  static const _kToken = 'ayzr.client_token';

  static Future<SettingsStore> open() async =>
      SettingsStore(await SharedPreferences.getInstance());

  AppConfig load() {
    final base = _prefs.getString(_kBaseUrl);
    final token = _prefs.getString(_kToken);
    return AppConfig(
      baseUrl: (base == null || base.isEmpty)
          ? AppConfig.defaultBaseUrl
          : base,
      clientToken: (token == null || token.isEmpty)
          ? AppConfig.buildToken
          : token,
    );
  }

  Future<void> save(AppConfig config) async {
    await _prefs.setString(_kBaseUrl, config.normalizedBase);
    await _prefs.setString(_kToken, config.clientToken.trim());
  }
}
