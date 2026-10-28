import 'package:flutter/material.dart';

import '../../core/app_config.dart';
import '../../core/strings_ar.dart';
import '../../core/theme.dart';
import '../../state/app_state.dart';

/// الإعدادات — configure the backend URL and the read-only client token.
/// The token is stored on-device only; the field is obscured and never logged.
class SettingsScreen extends StatefulWidget {
  const SettingsScreen({super.key, required this.state});

  final AppState state;

  @override
  State<SettingsScreen> createState() => _SettingsScreenState();
}

class _SettingsScreenState extends State<SettingsScreen> {
  late final TextEditingController _url =
      TextEditingController(text: widget.state.config.normalizedBase);
  late final TextEditingController _token =
      TextEditingController(text: widget.state.config.clientToken);
  bool _obscure = true;

  @override
  void dispose() {
    _url.dispose();
    _token.dispose();
    super.dispose();
  }

  Future<void> _save() async {
    await widget.state.applyConfig(AppConfig(
      baseUrl: _url.text,
      clientToken: _token.text,
    ));
    if (!mounted) return;
    ScaffoldMessenger.of(context).showSnackBar(
      const SnackBar(content: Text(Ar.saved)),
    );
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(title: const Text(Ar.settings)),
      body: ListView(
        padding: const EdgeInsets.all(16),
        children: [
          const Text(
            Ar.backendUrl,
            style: TextStyle(color: AyzrColors.textMuted, fontSize: 13),
          ),
          const SizedBox(height: 8),
          TextField(
            controller: _url,
            textDirection: TextDirection.ltr,
            keyboardType: TextInputType.url,
            style: const TextStyle(color: AyzrColors.textPrimary),
            decoration: const InputDecoration(
              hintText: 'https://example.com',
              hintStyle: TextStyle(color: AyzrColors.textMuted),
              border: OutlineInputBorder(),
            ),
          ),
          const SizedBox(height: 20),
          const Text(
            Ar.clientToken,
            style: TextStyle(color: AyzrColors.textMuted, fontSize: 13),
          ),
          const SizedBox(height: 8),
          TextField(
            controller: _token,
            textDirection: TextDirection.ltr,
            obscureText: _obscure,
            style: const TextStyle(color: AyzrColors.textPrimary),
            decoration: InputDecoration(
              hintText: Ar.tokenHint,
              hintStyle: const TextStyle(color: AyzrColors.textMuted),
              border: const OutlineInputBorder(),
              suffixIcon: IconButton(
                icon: Icon(
                  _obscure ? Icons.visibility_off : Icons.visibility,
                  color: AyzrColors.textMuted,
                ),
                onPressed: () => setState(() => _obscure = !_obscure),
              ),
            ),
          ),
          const SizedBox(height: 24),
          FilledButton.icon(
            onPressed: _save,
            icon: const Icon(Icons.save_outlined),
            label: const Text(Ar.save),
            style: FilledButton.styleFrom(
              backgroundColor: AyzrColors.gold,
              foregroundColor: AyzrColors.navyDeep,
            ),
          ),
        ],
      ),
    );
  }
}
