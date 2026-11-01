"""AYZR design tokens: dark navy + restrained gold.

Source of truth for the shipped app is Agent B's Flutter theme
(`ayzr-android/lib/core/theme.dart`, class `AyzrColors`). The tokens below are
those exact values, so generated bitmaps and the running UI cannot drift.

Cross-check against the repository brand reference
(docs/brand/ASTRA_VISUAL_IDENTITY_REFERENCE.jpg), median-cut quantised:
dominant dark surface #0A1823, dominant light surface #E1E7E7, mid gray
#BDC4CA. Those samples corroborate the navy/neutral family; they are recorded
here for provenance only and are not used for the shipped surfaces.

This module holds presentation tokens only. It contains no price, candle,
news, indicator, level or probability values, by design.
"""

# --- Dark navy surfaces (== AyzrColors in lib/core/theme.dart) ---
NAVY_DEEP = (7, 11, 24)     # #070B18 navyDeep   — scaffold background
NAVY = (12, 20, 40)         # #0C1428 navy       — surface
NAVY_RAISED = (19, 30, 58)  # #131E3A navyRaised
NAVY_BORDER = (34, 48, 79)  # #22304F navyBorder

# Aliases kept for the drawing primitives' gradient stops.
NAVY_950 = NAVY_DEEP
NAVY_900 = (9, 15, 31)
NAVY_800 = NAVY
NAVY_700 = NAVY_RAISED
NAVY_600 = NAVY_BORDER
NAVY_500 = (46, 62, 96)

# --- Neutral accents (== AyzrColors) ---
TEXT_PRIMARY = (237, 240, 247)  # #EDF0F7
TEXT_MUTED = (151, 160, 181)    # #97A0B5
SILVER_400 = (135, 143, 150)    # brand-reference mid gray #878F96
SILVER_300 = (189, 196, 202)    # brand-reference light gray #BDC4CA
OFFWHITE = TEXT_PRIMARY

# --- Gold accents (== AyzrColors.gold / goldSoft) ---
GOLD = (212, 175, 55)       # #D4AF37
GOLD_SOFT = (230, 199, 102)  # #E6C766
GOLD_700 = (138, 108, 32)
GOLD_600 = (168, 134, 42)
GOLD_500 = GOLD
GOLD_400 = GOLD_SOFT
GOLD_300 = (240, 221, 150)

# --- Transparent / absent ---
CLEAR = (0, 0, 0, 0)


def rgb(color, alpha=255):
    """Return an RGBA tuple from an RGB/RGBA token."""
    if len(color) == 4:
        return color
    return (color[0], color[1], color[2], alpha)


# Android launcher-icon density ladder. Legacy launcher icons are 48dp,
# adaptive icon layers are 108dp (with a 72dp guaranteed-visible safe zone),
# notification small icons are 24dp.
LEGACY_ICON_DP = 48
ADAPTIVE_LAYER_DP = 108
NOTIFICATION_ICON_DP = 24

DENSITY_SCALE = {
    "mdpi": 1.0,
    "hdpi": 1.5,
    "xhdpi": 2.0,
    "xxhdpi": 3.0,
    "xxxhdpi": 4.0,
}

DENSITY_ORDER = ["mdpi", "hdpi", "xhdpi", "xxhdpi", "xxxhdpi"]


def dp_to_px(dp, density):
    return int(round(dp * DENSITY_SCALE[density]))


# Reference canvas sizes used for the generated backgrounds/splash.
PHONE_PORTRAIT = (1080, 1920)
PHONE_PORTRAIT_XL = (1440, 2560)
