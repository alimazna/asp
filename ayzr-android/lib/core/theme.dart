import 'package:flutter/material.dart';

/// AYZR design tokens — dark navy surfaces with restrained gold accents.
/// Values are centralized here so no screen hardcodes a color.
class AyzrColors {
  const AyzrColors._();

  static const Color navyDeep = Color(0xFF070B18);
  static const Color navy = Color(0xFF0C1428);
  static const Color navyRaised = Color(0xFF131E3A);
  static const Color navyBorder = Color(0xFF22304F);

  static const Color gold = Color(0xFFD4AF37);
  static const Color goldSoft = Color(0xFFE6C766);

  static const Color textPrimary = Color(0xFFEDF0F7);
  static const Color textMuted = Color(0xFF97A0B5);

  static const Color up = Color(0xFF3FA97A);
  static const Color down = Color(0xFFC25B5B);
  static const Color warning = Color(0xFFD99A3D);
  static const Color info = Color(0xFF5B8DC2);
}

class AyzrTheme {
  const AyzrTheme._();

  static ThemeData dark() {
    final base = ThemeData.dark(useMaterial3: true);
    // Arabic-first typeface across the whole app.
    const textTheme = TextTheme(
      bodyLarge: TextStyle(fontFamily: 'Cairo'),
      bodyMedium: TextStyle(fontFamily: 'Cairo'),
      bodySmall: TextStyle(fontFamily: 'Cairo'),
      titleLarge: TextStyle(fontFamily: 'Cairo'),
      titleMedium: TextStyle(fontFamily: 'Cairo'),
      titleSmall: TextStyle(fontFamily: 'Cairo'),
      labelLarge: TextStyle(fontFamily: 'Cairo'),
      labelMedium: TextStyle(fontFamily: 'Cairo'),
      labelSmall: TextStyle(fontFamily: 'Cairo'),
    );
    return base.copyWith(
      scaffoldBackgroundColor: AyzrColors.navyDeep,
      textTheme: base.textTheme.merge(textTheme),
      colorScheme: const ColorScheme.dark(
        primary: AyzrColors.gold,
        onPrimary: AyzrColors.navyDeep,
        secondary: AyzrColors.goldSoft,
        surface: AyzrColors.navy,
        onSurface: AyzrColors.textPrimary,
        error: AyzrColors.down,
      ),
      appBarTheme: const AppBarTheme(
        backgroundColor: AyzrColors.navyDeep,
        foregroundColor: AyzrColors.textPrimary,
        elevation: 0,
        centerTitle: true,
      ),
      cardTheme: CardTheme(
        color: AyzrColors.navy,
        elevation: 0,
        shape: RoundedRectangleBorder(
          borderRadius: BorderRadius.circular(14),
          side: const BorderSide(color: AyzrColors.navyBorder),
        ),
      ),
      navigationBarTheme: NavigationBarThemeData(
        backgroundColor: AyzrColors.navy,
        indicatorColor: AyzrColors.gold.withOpacity(0.16),
        labelTextStyle: WidgetStateProperty.resolveWith((states) {
          final selected = states.contains(WidgetState.selected);
          return TextStyle(
            fontSize: 12,
            fontWeight: selected ? FontWeight.w700 : FontWeight.w500,
            color: selected ? AyzrColors.goldSoft : AyzrColors.textMuted,
          );
        }),
        iconTheme: WidgetStateProperty.resolveWith((states) {
          final selected = states.contains(WidgetState.selected);
          return IconThemeData(
            color: selected ? AyzrColors.gold : AyzrColors.textMuted,
          );
        }),
      ),
      dividerTheme: const DividerThemeData(color: AyzrColors.navyBorder),
    );
  }
}
