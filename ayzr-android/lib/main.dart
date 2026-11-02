import 'package:flutter/material.dart';

import 'state/app_state.dart';
import 'state/settings_store.dart';
import 'ui/app_shell.dart';

Future<void> main() async {
  WidgetsFlutterBinding.ensureInitialized();
  final settings = await SettingsStore.open();
  final state = AppState(settings: settings);
  runApp(AyzrApp(state: state));
}
