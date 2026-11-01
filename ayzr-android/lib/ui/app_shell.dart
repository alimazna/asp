import 'dart:convert';

import 'package:flutter/material.dart';
import 'package:flutter_localizations/flutter_localizations.dart';

import '../core/strings_ar.dart';
import '../core/theme.dart';
import '../state/app_state.dart';
import 'screens/activity_screen.dart';
import 'screens/analysis_screen.dart';
import 'screens/events_screen.dart';
import 'screens/history_screen.dart';
import 'screens/home_screen.dart';
import 'screens/news_screen.dart';
import 'screens/news_results_screen.dart';
import 'screens/settings_screen.dart';
import 'screens/trades_screen.dart';
import '../data/journal.dart';
import '../data/models/engines.dart';

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

  static const _titles = [
    Ar.navHome,
    Ar.navAnalysis,
    'الصفقات',
    Ar.navNews,
    Ar.navHistory,
  ];

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
    await widget.state.loadEvents();
    await widget.state.loadUpcomingEvents();
    await widget.state.loadTrades();
  }

  TradeJournal? _journal;

  /// Record a signal in the LOCAL journal. Nothing leaves the device.
  Future<void> _takeTrade(Trade t) async {
    _journal ??= await TradeJournal.open();
    final targets = t.targets.map((x) => x.price).toList();
    final id = await _journal!.add(JournalEntry(
      tradeId: t.id,
      source: t.source,
      sourceLabelAr: t.sourceLabelAr,
      direction: t.direction,
      directionLabelAr: t.directionLabelAr,
      signalEntry: t.entry,
      stop: t.stop,
      targetsJson: jsonEncode(targets),
      entryTimeframe: t.entryTimeframe,
      takenAt: DateTime.now(),
      signalPublishedAt: t.publishedAt ?? DateTime.now(),
    ));
    if (!mounted) return;
    ScaffoldMessenger.of(context).showSnackBar(SnackBar(
      content: Text(id == null
          ? 'هذه الصفقة مسجّلة مسبقاً في دفترك'
          : 'أُضيفت الصفقة إلى دفترك المحلي'),
    ));
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
        widget.state.loadTrades();
        break;
      case 4:
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
            TradesScreen(
              trades: widget.state.trades.data ?? const [],
              loading: widget.state.trades.phase == LoadPhase.loading,
              error: widget.state.trades.failure?.message,
              onRetry: widget.state.loadTrades,
              onTakeTrade: _takeTrade,
            ),
            NewsScreen(state: widget.state),
            HistoryScreen(state: widget.state),
          ];
          return Scaffold(
            appBar: AppBar(
              title: Text(_titles[_index]),
              actions: [
                IconButton(
                  tooltip: Ar.newsResultsTitle,
                  icon: const Icon(Icons.fact_check_outlined),
                  onPressed: () {
                    widget.state.loadNewsResults();
                    Navigator.of(context).push(
                      MaterialPageRoute(
                        builder: (_) => Scaffold(
                          appBar:
                              AppBar(title: const Text(Ar.newsResultsTitle)),
                          body: NewsResultsScreen(state: widget.state),
                        ),
                      ),
                    );
                  },
                ),
                IconButton(
                  tooltip: Ar.eventsTitle,
                  icon: const Icon(Icons.event_note_outlined),
                  onPressed: () => Navigator.of(context).push(
                    MaterialPageRoute(
                      builder: (_) => Scaffold(
                        appBar: AppBar(title: const Text(Ar.eventsTitle)),
                        body: EventsScreen(state: widget.state),
                      ),
                    ),
                  ),
                ),
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
                  icon: Icon(Icons.candlestick_chart_outlined),
                  selectedIcon: Icon(Icons.candlestick_chart_rounded),
                  label: 'الصفقات',
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
