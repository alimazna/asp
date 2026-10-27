import 'package:flutter/material.dart';
import 'package:flutter_localizations/flutter_localizations.dart';

import '../core/strings_ar.dart';
import '../core/theme.dart';
import '../state/app_state.dart';
import 'screens/activity_screen.dart';
import 'screens/analysis_screen.dart';
import 'screens/history_screen.dart';
import 'screens/home_screen.dart';
import 'screens/news_screen.dart';
import 'screens/settings_screen.dart';

/// The RTL shell: a bottom navigation bar over the four Arabic sections, with a
/// settings entry point for the backend URL and client token.
class AppShell extends StatefulWidget {
  const AppShell({super.key, required this.state});

  final AppState state;

  @override
  State<AppShell> createState() => _AppShellState();
}

class _AppShellState extends State<AppShell> {
  int _index = 0;

  static const _titles = [Ar.navHome, Ar.navAnalysis, Ar.navNews, Ar.navHistory];

  @override
  void initState() {
    super.initState();
    WidgetsBinding.instance.addPostFrameCallback((_) => _loadInitial());
  }

  Future<void> _loadInitial() async {
    await widget.state.loadHealth();
    await widget.state.loadAnalysis();
    await widget.state.loadNews();
    await widget.state.loadHistory();
  }

  void _onSelect(int i) {
    setState(() => _index = i);
    // Lazy (re)load per section so a failure in one does not block the others.
    switch (i) {
      case 0:
        widget.state.loadHealth();
        break;
      case 1:
        widget.state.loadAnalysis();
        break;
      case 2:
        widget.state.loadNews();
        break;
      case 3:
        widget.state.loadHistory();
        break;
    }
  }

  @override
  Widget build(BuildContext context) {
    return Directionality(
      textDirection: TextDirection.rtl,
      child: ListenableBuilder(
        listenable: widget.state,
        builder: (context, _) {
          final screens = [
            HomeScreen(state: widget.state),
            AnalysisScreen(state: widget.state),
            NewsScreen(state: widget.state),
            HistoryScreen(state: widget.state),
          ];
          return Scaffold(
            appBar: AppBar(
              title: Text(_titles[_index]),
              actions: [
                IconButton(
                  tooltip: Ar.activityTitle,
                  icon: const Icon(Icons.receipt_long_outlined),
                  onPressed: () => Navigator.of(context).push(
                    MaterialPageRoute(
                      builder: (_) => Scaffold(
                        appBar: AppBar(title: const Text(Ar.activityTitle)),
                        body: ActivityScreen(state: widget.state),
                      ),
                    ),
                  ),
                ),
                IconButton(
                  tooltip: Ar.settings,
                  icon: const Icon(Icons.settings_outlined),
                  onPressed: () => Navigator.of(context).push(
                    MaterialPageRoute(
                      builder: (_) => SettingsScreen(state: widget.state),
                    ),
                  ),
                ),
              ],
            ),
            body: IndexedStack(index: _index, children: screens),
            bottomNavigationBar: NavigationBar(
              selectedIndex: _index,
              onDestinationSelected: _onSelect,
              destinations: const [
                NavigationDestination(
                  icon: Icon(Icons.home_outlined),
                  selectedIcon: Icon(Icons.home_rounded),
                  label: Ar.navHome,
                ),
                NavigationDestination(
                  icon: Icon(Icons.insights_outlined),
                  selectedIcon: Icon(Icons.insights_rounded),
                  label: Ar.navAnalysis,
                ),
                NavigationDestination(
                  icon: Icon(Icons.newspaper_outlined),
                  selectedIcon: Icon(Icons.newspaper_rounded),
                  label: Ar.navNews,
                ),
                NavigationDestination(
                  icon: Icon(Icons.history_outlined),
                  selectedIcon: Icon(Icons.history_rounded),
                  label: Ar.navHistory,
                ),
              ],
            ),
          );
        },
      ),
    );
  }
}

/// Root widget kept separate from `main()` so widget tests can inject a fully
/// constructed [AppState] (with a fake HTTP client) without touching the
/// platform channel used by shared_preferences.
class AyzrApp extends StatelessWidget {
  const AyzrApp({super.key, required this.state});

  final AppState state;

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: Ar.appName,
      debugShowCheckedModeBanner: false,
      theme: AyzrTheme.dark(),
      locale: const Locale('ar'),
      supportedLocales: const [Locale('ar')],
      localizationsDelegates: const [
        GlobalMaterialLocalizations.delegate,
        GlobalWidgetsLocalizations.delegate,
        GlobalCupertinoLocalizations.delegate,
      ],
      home: AppShell(state: state),
    );
  }
}
