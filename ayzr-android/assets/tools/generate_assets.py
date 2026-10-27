#!/usr/bin/env python3
"""AYZR asset generator — deterministic Pillow pipeline.

Produces every raster asset the AYZR Android app needs:

  * legacy launcher icons at all five Android densities (mipmap-*)
  * adaptive-icon layers (foreground / background / monochrome) at 108dp
  * Play Store listing icon at 512x512
  * in-app brand mark and notification small icon
  * dark-navy/gold splash backgrounds (portrait + tablet)
  * app background texture
  * rasterised previews of the SVG source graphics

Everything is pure geometry: no model, no randomness, no invented data. Run
from this directory:

    python3 generate_assets.py            # write all assets
    python3 generate_assets.py --check     # write to a temp dir and verify only

See docs/ayzr/VISUAL_DESIGN.md for the honest capability boundary.
"""

import argparse
import hashlib
import json
import os
import shutil
import sys
import tempfile

from PIL import Image, ImageDraw, ImageFilter

import ayzr_draw as D
from ayzr_palette import (
    CLEAR,
    DENSITY_ORDER,
    DENSITY_SCALE,
    GOLD,
    GOLD_600,
    GOLD_SOFT,
    LEGACY_ICON_DP,
    NAVY,
    NAVY_BORDER,
    NAVY_DEEP,
    NAVY_RAISED,
    TEXT_PRIMARY,
)

HERE = os.path.dirname(os.path.abspath(__file__))
ASSETS = os.path.dirname(HERE)
REPO_ASSETS = ASSETS

# Density -> legacy launcher icon pixel size (48dp).
LEGACY_PX = {d: int(round(LEGACY_ICON_DP * DENSITY_SCALE[d])) for d in DENSITY_ORDER}
# Adaptive layers are 108dp.
ADAPTIVE_PX = {d: int(round(108 * DENSITY_SCALE[d])) for d in DENSITY_ORDER}
# Notification small icons are 24dp.
NOTIF_PX = {d: int(round(24 * DENSITY_SCALE[d])) for d in DENSITY_ORDER}


def _save_png(img, path, optimize=True):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    img.save(path, "PNG", optimize=optimize)
    return path


def _save_webp(img, path, quality=88, lossless=False):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    img.save(path, "WEBP", quality=quality, method=6, lossless=lossless)
    return path


# --------------------------------------------------------------------------
# Launcher icons
# --------------------------------------------------------------------------

def plate(size, radius=None, with_grid=True):
    """Opaque navy plate. `radius=None` gives a full square (Play Store).

    NOTE on Pillow semantics: `ImageDraw` *replaces* pixels including their
    alpha; it does not blend. A semi-transparent decoration drawn straight onto
    an opaque plate therefore punches holes in the plate's alpha. Every
    translucent element here is drawn on its own transparent layer and then
    composited with `Image.alpha_composite`.
    """
    def _fn(draw, side):
        if radius is None:
            draw.rectangle((0, 0, side - 1, side - 1), fill=NAVY_DEEP)
        else:
            draw.rounded_rectangle(
                (0, 0, side - 1, side - 1), radius=radius * D.SS, fill=NAVY_DEEP
            )
        # Diagonal sheen: a raised-navy wedge in the lower half (fully opaque,
        # so it is safe to draw directly).
        band = int(side * 0.55)
        draw.polygon(
            [(0, side - band), (side, side - band - int(side * 0.35)),
             (side, side), (0, side)],
            fill=NAVY_RAISED,
        )

    base = D.render(size, _fn)
    if with_grid:
        overlay = D.render(
            size,
            lambda d, s: D.draw_grid(d, s, spacing=0.125,
                                     color=(34, 48, 79, 70)),
        )
        base.alpha_composite(overlay)
    return base


def mark_layer(size, inset=0.30, ring=True, glow=True):
    """Transparent layer holding just the gold mark (optionally bloomed)."""
    def _fn(draw, side):
        D.draw_gem(draw, side, inset=inset, stroke=GOLD,
                   outline_w=max(1, int(round(side * 0.030))))
        if ring:
            D.draw_arcs(draw, side, radii=[0.40, 0.47], color=NAVY_BORDER,
                        width=max(1, int(round(side * 0.010))))

    img = D.render(size, _fn)
    return D.add_glow(img, radius=max(2, size // 22)) if glow else img


def legacy_icon(size):
    """Full-bleed rounded-square icon: navy plate + bloomed gold mark.

    The glow is applied to the mark layer only, then composited over the
    plate, so the navy surface keeps its exact theme colour.
    """
    radius = int(size * 0.22)
    base = plate(size, radius=radius)
    base.alpha_composite(mark_layer(size, inset=0.30))
    # Clip to the rounded plate so the icon has a clean silhouette.
    mask = D.rounded_rect_mask(size, radius)
    out = Image.new("RGBA", (size, size), CLEAR)
    out.paste(base, (0, 0), mask)
    return out


def adaptive_foreground(size):
    """Adaptive foreground layer: transparent, mark inside the 72dp safe zone.

    An adaptive icon is a 108dp layer of which only the central 72dp is
    guaranteed visible; the outer ring can be masked away by the launcher
    (circle, squircle, rounded square). Everything drawn here — including the
    bloom — must fit inside a radius of 36/108 = 1/3 of the canvas, otherwise a
    launcher that masks aggressively clips the mark.

    `verify_assets.py` asserts this geometry, so the constraint is enforced
    rather than assumed.
    """
    # Safe-zone radius in canvas fractions, with margin for the bloom.
    ring = [0.235, 0.290]

    def _fn(draw, side):
        D.draw_gem(draw, side, inset=0.335, stroke=GOLD,
                   outline_w=max(1, int(round(side * 0.028))))
        D.draw_arcs(draw, side, radii=ring, color=GOLD_SOFT,
                    width=max(1, int(round(side * 0.009))))

    img = D.render(size, _fn)
    return D.add_glow(img, radius=max(1, size // 34), strength=0.42)


def adaptive_background(size):
    """Adaptive background layer: opaque navy gradient plate."""
    grad = D.navy_background((size, size)).convert("RGBA")
    # The grid is translucent, so it must live on its own layer: drawing it
    # straight onto the opaque plate would overwrite the plate's alpha.
    overlay = D.render(
        size, lambda d, s: D.draw_grid(d, s, spacing=0.09,
                                       color=(34, 48, 79, 80))
    )
    return Image.alpha_composite(grad, overlay).convert("RGB")


def adaptive_monochrome(size):
    """Themed-icon (monochrome) layer: white silhouette, transparent bg.

    Same 72dp safe-zone constraint as the foreground layer.
    """
    def _fn(draw, side):
        D.draw_gem(draw, side, inset=0.335, stroke=(255, 255, 255),
                   outline_w=max(1, int(round(side * 0.028))))
        D.draw_arcs(draw, side, radii=[0.235, 0.290], color=(255, 255, 255),
                    width=max(1, int(round(side * 0.009))))

    return D.render(size, _fn)


def play_store_icon(size=512):
    """Google Play listing icon: 512x512, 32-bit PNG, full-bleed square.

    Play requires a full square (no rounded corners, no transparency).
    """
    base = plate(size, radius=None)
    base.alpha_composite(mark_layer(size, inset=0.28))
    return base.convert("RGB")


def notification_icon(size):
    """Notification small icon: white-on-transparent, single-colour glyph.

    Android tints this glyph, so it must be pure white + alpha only.
    """
    def _fn(draw, side):
        D.draw_gem(draw, side, inset=0.16, stroke=(255, 255, 255),
                   outline_w=max(1, int(round(side * 0.055))))

    img = D.render(size, _fn)
    # Collapse to white + alpha so no stray gold survives tinting.
    alpha = img.getchannel("A")
    white = Image.new("RGBA", (size, size), (255, 255, 255, 0))
    white.putalpha(alpha)
    return white


def brand_mark(size):
    """Transparent in-app brand mark (gold gem + ring)."""
    return D.make_mark(size, gold=GOLD, glow=True, inset=0.20)


# --------------------------------------------------------------------------
# Splash / backgrounds
# --------------------------------------------------------------------------

def splash(size):
    """Dark-navy gradient splash: faint grid, gold bloom, centred mark.

    Composed in layers so the square mark can be placed exactly, since
    `draw_gem` works on a square coordinate space.
    """
    w, h = size
    ref = min(w, h)
    cx, cy = w // 2, int(h * 0.40)

    def _overlay(draw, cw, ch):
        D.draw_grid_rect(draw, cw, ch, spacing=0.055,
                         color=(34, 48, 79, 46), ref=ref * D.SS)
        # Lower decorative bands: abstract gold rules, not a price series.
        base = ch * 0.80
        for i in range(3):
            y = base + i * ch * 0.022
            inset = cw * (0.30 - i * 0.05)
            draw.rounded_rectangle(
                (inset, y, cw - inset, y + ch * 0.006),
                radius=ch * 0.003, fill=(212, 175, 55, 90 - i * 20),
            )

    grad = D.navy_background(size).convert("RGBA")
    out = Image.alpha_composite(grad, D.render_rect(size, _overlay))

    # Warm core inside the gem. A wide, low-alpha gold wash over dark navy
    # reads as gray, so the halo is deliberately smaller than the mark: it
    # glows through the translucent facets and dies before reaching the plate.
    halo_d = int(ref * 0.20)
    halo = Image.new("RGBA", (halo_d, halo_d), CLEAR)
    halo.paste((212, 175, 55, 96), (0, 0), D.radial_mask(halo_d, falloff=2.2))
    out.alpha_composite(halo, (cx - halo_d // 2, cy - halo_d // 2))

    mark = brand_mark(int(ref * 0.30))
    out.alpha_composite(mark, (cx - mark.width // 2, cy - mark.height // 2))
    return out.convert("RGB")


def app_background(size):
    """Subtle app background texture (dark navy gradient + faint grid)."""
    w, h = size
    ref = min(w, h)

    def _overlay(draw, cw, ch):
        D.draw_grid_rect(draw, cw, ch, spacing=0.075,
                         color=(34, 48, 79, 60), ref=ref * D.SS)
        D.draw_arcs(draw, ref * D.SS, center=(cw * 0.5, ch * 0.10),
                    radii=[0.34, 0.42], color=(34, 48, 79, 150),
                    width=max(2, int(ref * D.SS * 0.0018)))

    grad = D.navy_background(size).convert("RGBA")
    overlay = D.render_rect(size, _overlay)
    return Image.alpha_composite(grad, overlay).convert("RGB")


# --------------------------------------------------------------------------
# In-app vector-style glyphs (raster previews of the SVG set)
# --------------------------------------------------------------------------

GLYPH_BUILDERS = {
    "ic_analysis": lambda draw, side: (
        D.draw_waveform(draw, side),
        D.draw_bars(draw, side, color=(212, 175, 55, 150)),
    ),
    "ic_news": lambda draw, side: D.draw_waveform(draw, side),
    "ic_history": lambda draw, side: D.draw_doc(draw, side),
    "ic_market": lambda draw, side: D.draw_bars(draw, side),
    "ic_gem": lambda draw, side: D.draw_gem(draw, side, inset=0.12),
    "ic_alert": lambda draw, side: (
        D.draw_arcs(draw, side, radii=[0.30, 0.40], color=GOLD, width=None),
        D.draw_gem(draw, side, inset=0.36, stroke=GOLD_SOFT,
                   outline_w=max(1, int(round(side * 0.045)))),
    ),
}


def glyph(name, size):
    return D.render(size, GLYPH_BUILDERS[name])


# --------------------------------------------------------------------------
# SVG sources
# --------------------------------------------------------------------------

def write_svgs(out_dir):
    """Write hand-authored SVG sources (vector, scalable, tiny).

    These are real SVG files with explicit viewBox geometry — not raster
    traces. They are the resolution-independent source for the raster
    previews; keep them in sync with the Pillow generators by hand.
    """
    svg_dir = os.path.join(out_dir, "graphics", "svg")
    os.makedirs(svg_dir, exist_ok=True)
    files = {}

    files["ayzr_mark.svg"] = """<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 512 512" width="512" height="512" role="img" aria-label="AYZR">
  <title>AYZR mark — faceted gem</title>
  <defs>
    <linearGradient id="plate" x1="0" y1="0" x2="0" y2="1">
      <stop offset="0" stop-color="#070B18"/>
      <stop offset="0.45" stop-color="#0C1428"/>
      <stop offset="1" stop-color="#04070F"/>
    </linearGradient>
    <linearGradient id="gold" x1="0" y1="0" x2="1" y2="1">
      <stop offset="0" stop-color="#E6C766"/>
      <stop offset="1" stop-color="#A8862A"/>
    </linearGradient>
  </defs>
  <rect width="512" height="512" rx="112" fill="url(#plate)"/>
  <g fill="none" stroke="#22304F" stroke-width="6">
    <circle cx="256" cy="256" r="150"/>
    <circle cx="256" cy="256" r="196"/>
  </g>
  <g fill="none" stroke="url(#gold)" stroke-width="16" stroke-linejoin="round">
    <path d="M256 128 L400 218 L256 384 L112 218 Z"/>
    <path d="M168 205 L344 205"/>
    <path d="M168 205 L256 384"/>
    <path d="M344 205 L256 384"/>
  </g>
</svg>
"""

    files["splash_background.svg"] = """<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 1080 1920" width="1080" height="1920" role="img" aria-label="AYZR splash">
  <title>AYZR splash background — dark navy with gold motif</title>
  <defs>
    <linearGradient id="bg" x1="0" y1="0" x2="0" y2="1">
      <stop offset="0" stop-color="#070B18"/>
      <stop offset="0.45" stop-color="#0C1428"/>
      <stop offset="1" stop-color="#04070F"/>
    </linearGradient>
    <radialGradient id="bloom" cx="0.5" cy="0.4" r="0.55">
      <stop offset="0" stop-color="#D4AF37" stop-opacity="0.18"/>
      <stop offset="1" stop-color="#D4AF37" stop-opacity="0"/>
    </radialGradient>
    <pattern id="grid" width="60" height="60" patternUnits="userSpaceOnUse">
      <path d="M60 0 H0 V60" fill="none" stroke="#22304F" stroke-width="1" stroke-opacity="0.28"/>
    </pattern>
  </defs>
  <rect width="1080" height="1920" fill="url(#bg)"/>
  <rect width="1080" height="1920" fill="url(#grid)"/>
  <rect width="1080" height="1920" fill="url(#bloom)"/>
  <g transform="translate(540 768)" fill="none" stroke="#D4AF37" stroke-linejoin="round">
    <circle r="212" stroke="#22304F" stroke-width="3"/>
    <circle r="264" stroke="#22304F" stroke-width="3"/>
    <path d="M0 -120 L104 -32 L0 120 L-104 -32 Z" stroke-width="12"/>
    <path d="M-64 -46 L64 -46" stroke-width="7"/>
    <path d="M-64 -46 L0 120" stroke-width="7"/>
    <path d="M64 -46 L0 120" stroke-width="7"/>
  </g>
  <g fill="#D4AF37" opacity="0.55">
    <rect x="324" y="1536" width="432" height="10" rx="5"/>
    <rect x="378" y="1578" width="324" height="10" rx="5" opacity="0.75"/>
    <rect x="432" y="1620" width="216" height="10" rx="5" opacity="0.5"/>
  </g>
</svg>
"""

    files["app_background.svg"] = """<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 1080 1920" width="1080" height="1920" role="img" aria-label="AYZR background">
  <title>AYZR app background texture</title>
  <defs>
    <linearGradient id="bg2" x1="0" y1="0" x2="0" y2="1">
      <stop offset="0" stop-color="#070B18"/>
      <stop offset="1" stop-color="#0C1428"/>
    </linearGradient>
    <pattern id="grid2" width="81" height="81" patternUnits="userSpaceOnUse">
      <path d="M81 0 H0 V81" fill="none" stroke="#22304F" stroke-width="1" stroke-opacity="0.22"/>
    </pattern>
  </defs>
  <rect width="1080" height="1920" fill="url(#bg2)"/>
  <rect width="1080" height="1920" fill="url(#grid2)"/>
</svg>
"""

    glyph_svgs = {
        "ic_analysis.svg": (
            '<path d="M160 640 L320 480 L480 560 L640 360 L800 440 L960 240" '
            'fill="none" stroke="#D4AF37" stroke-width="72" stroke-linecap="round" '
            'stroke-linejoin="round"/>'
        ),
        "ic_news.svg": (
            '<path d="M200 700 L380 520 L560 600 L740 400 L920 480" '
            'fill="none" stroke="#D4AF37" stroke-width="72" stroke-linecap="round" '
            'stroke-linejoin="round"/>'
        ),
        "ic_history.svg": (
            '<rect x="256" y="176" width="640" height="800" rx="56" '
            'fill="none" stroke="#D4AF37" stroke-width="48"/>'
            '<g stroke="#D4AF37" stroke-width="28" stroke-linecap="round">'
            '<path d="M368 424 H784"/><path d="M368 592 H784"/><path d="M368 760 H784"/>'
            '</g>'
        ),
        "ic_market.svg": (
            '<g fill="#D4AF37">'
            '<rect x="240" y="640" width="128" height="320" rx="40"/>'
            '<rect x="500" y="440" width="128" height="520" rx="40"/>'
            '<rect x="760" y="256" width="128" height="704" rx="40"/>'
            '</g>'
        ),
        "ic_gem.svg": (
            '<path d="M576 160 L896 400 L576 896 L256 400 Z" fill="none" '
            'stroke="#D4AF37" stroke-width="64" stroke-linejoin="round"/>'
            '<path d="M400 360 L752 360" fill="none" stroke="#D4AF37" stroke-width="32"/>'
        ),
        "ic_alert.svg": (
            '<circle cx="576" cy="576" r="352" fill="none" stroke="#D4AF37" stroke-width="40"/>'
            '<path d="M576 384 L704 640 L448 640 Z" fill="none" stroke="#E6C766" '
            'stroke-width="48" stroke-linejoin="round"/>'
        ),
    }
    for name, body in glyph_svgs.items():
        files[name] = (
            '<?xml version="1.0" encoding="UTF-8"?>\n'
            '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 1152 1152" '
            'width="1152" height="1152" role="img">\n'
            f'  <title>{name[:-4]}</title>\n'
            f'  {body}\n'
            '</svg>\n'
        )

    for name, text in files.items():
        with open(os.path.join(svg_dir, name), "w", encoding="utf-8") as fh:
            fh.write(text)
    return sorted(files)


# --------------------------------------------------------------------------
# Build
# --------------------------------------------------------------------------

def write_android_xml(out_dir):
    """Write the Android XML resources that accompany the raster icons.

    These live under the agent's own asset tree; `install_android_res.py`
    copies them into the Flutter module's `android/app/src/main/res/`, which is
    Agent B's path, so the copy is a coordinated hand-off rather than a direct
    edit.
    """
    written = []

    def put(rel, text):
        path = os.path.join(out_dir, rel)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w", encoding="utf-8") as fh:
            fh.write(text)
        written.append(path)

    # Adaptive launcher icon (API 26+). `monochrome` needs API 33+ but is
    # ignored by older platforms inside the same adaptive-icon element.
    put("android-res/mipmap-anydpi-v26/ic_launcher.xml", """<?xml version="1.0" encoding="utf-8"?>
<!-- AYZR adaptive launcher icon. Layers are the generated 108dp PNGs. -->
<adaptive-icon xmlns:android="http://schemas.android.com/apk/res/android">
    <background android:drawable="@color/ic_launcher_background"/>
    <foreground android:drawable="@mipmap/ic_launcher_foreground"/>
    <monochrome android:drawable="@mipmap/ic_launcher_monochrome"/>
</adaptive-icon>
""")

    put("android-res/mipmap-anydpi-v26/ic_launcher_round.xml", """<?xml version="1.0" encoding="utf-8"?>
<!-- AYZR round adaptive launcher icon. -->
<adaptive-icon xmlns:android="http://schemas.android.com/apk/res/android">
    <background android:drawable="@color/ic_launcher_background"/>
    <foreground android:drawable="@mipmap/ic_launcher_foreground"/>
    <monochrome android:drawable="@mipmap/ic_launcher_monochrome"/>
</adaptive-icon>
""")

    # Flat background colour, matching AyzrColors.navyDeep in the Flutter theme.
    put("android-res/values/ic_launcher_background.xml", """<?xml version="1.0" encoding="utf-8"?>
<resources>
    <!-- AyzrColors.navyDeep (#070B18) — matches lib/core/theme.dart. -->
    <color name="ic_launcher_background">#070B18</color>
</resources>
""")

    # Launch (splash) background. The bitmap is centred at its intrinsic size
    # so Android never upscales it; the flat colour behind it matches the
    # splash gradient's top stop so there is no visible seam.
    put("android-res/drawable/launch_background.xml", """<?xml version="1.0" encoding="utf-8"?>
<!-- AYZR launch screen: navy backdrop with the centred brand mark. -->
<layer-list xmlns:android="http://schemas.android.com/apk/res/android">
    <item android:drawable="@color/ic_launcher_background"/>
    <item>
        <bitmap
            android:gravity="center"
            android:src="@drawable/ayzr_splash_mark"/>
    </item>
</layer-list>
""")

    put("android-res/drawable-v21/launch_background.xml", """<?xml version="1.0" encoding="utf-8"?>
<!-- AYZR launch screen (API 21+). -->
<layer-list xmlns:android="http://schemas.android.com/apk/res/android">
    <item android:drawable="@color/ic_launcher_background"/>
    <item>
        <bitmap
            android:gravity="center"
            android:src="@drawable/ayzr_splash_mark"/>
    </item>
</layer-list>
""")

    # Notification channel small icon tint.
    put("android-res/values/colors.xml", """<?xml version="1.0" encoding="utf-8"?>
<resources>
    <!-- AyzrColors.gold (#D4AF37). -->
    <color name="ayzr_gold">#D4AF37</color>
    <!-- AyzrColors.navyDeep (#070B18). -->
    <color name="ayzr_navy">#070B18</color>
</resources>
""")
    return written


def build(out_root, verbose=True):
    written = []

    def rec(p):
        written.append(p)
        if verbose:
            rel = os.path.relpath(p, out_root)
            print(f"  {rel:64s} {os.path.getsize(p):>8d} B")

    # 1. Legacy launcher icons (one per density).
    for d in DENSITY_ORDER:
        px = LEGACY_PX[d]
        p = os.path.join(out_root, "android-res", f"mipmap-{d}", "ic_launcher.png")
        rec(_save_png(legacy_icon(px), p))

    # 2. Adaptive icon layers (foreground / background / monochrome).
    for d in DENSITY_ORDER:
        px = ADAPTIVE_PX[d]
        base = os.path.join(out_root, "android-res", f"mipmap-{d}")
        rec(_save_png(adaptive_foreground(px),
                      os.path.join(base, "ic_launcher_foreground.png")))
        rec(_save_png(adaptive_background(px),
                      os.path.join(base, "ic_launcher_background.png")))
        rec(_save_png(adaptive_monochrome(px),
                      os.path.join(base, "ic_launcher_monochrome.png")))

    # 3. Play Store icon.
    rec(_save_png(play_store_icon(512),
                  os.path.join(out_root, "icons", "play_store_icon_512.png")))

    # 4. In-app brand mark + notification icon.
    rec(_save_png(brand_mark(256), os.path.join(out_root, "icons", "brand_mark_256.png")))
    rec(_save_png(brand_mark(96), os.path.join(out_root, "icons", "brand_mark_96.png")))
    for d in DENSITY_ORDER:
        px = NOTIF_PX[d]
        rec(_save_png(notification_icon(px),
                      os.path.join(out_root, "android-res", f"drawable-{d}",
                                   "ic_stat_ayzr.png")))

    # 5. Splash backgrounds.
    rec(_save_png(splash((1080, 1920)), os.path.join(out_root, "images", "splash_1080x1920.png")))
    rec(_save_png(splash((1440, 2560)), os.path.join(out_root, "images", "splash_1440x2560.png")))
    rec(_save_png(splash((1200, 1920)), os.path.join(out_root, "images", "splash_tablet_1200x1920.png")))

    # 5b. Centred splash mark used by the Android launch_background drawable.
    rec(_save_png(brand_mark(288), os.path.join(out_root, "android-res", "drawable",
                                                "ayzr_splash_mark.png")))
    rec(_save_png(brand_mark(432), os.path.join(out_root, "android-res", "drawable-xxhdpi",
                                                "ayzr_splash_mark.png")))
    rec(_save_png(brand_mark(576), os.path.join(out_root, "android-res", "drawable-xxxhdpi",
                                                "ayzr_splash_mark.png")))

    # 6. App background texture (PNG + WEBP).
    bg = app_background((1080, 1920))
    rec(_save_png(bg, os.path.join(out_root, "images", "app_background_1080x1920.png")))
    rec(_save_webp(bg, os.path.join(out_root, "images", "app_background_1080x1920.webp"),
                   quality=82))

    # 7. In-app glyphs at 3 sizes each (SVG is the source of truth).
    for name in sorted(GLYPH_BUILDERS):
        for px in (24, 48, 96):
            rec(_save_png(glyph(name, px),
                          os.path.join(out_root, "graphics", "png", f"{name}_{px}.png")))

    # 8. SVG sources.
    for name in write_svgs(out_root):
        rec(os.path.join(out_root, "graphics", "svg", name))

    # 9. Android XML resources (adaptive icon, launch screen, colours).
    for path in write_android_xml(out_root):
        rec(path)

    return written


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true",
                    help="build into a temp dir and verify instead of writing assets")
    ap.add_argument("--quiet", action="store_true")
    args = ap.parse_args()

    root = tempfile.mkdtemp(prefix="ayzr-assets-") if args.check else REPO_ASSETS
    print(f"AYZR asset build -> {root}")
    written = build(root, verbose=not args.quiet)
    total = sum(os.path.getsize(p) for p in written)
    print(f"\n{len(written)} files, {total} bytes total")

    if args.check:
        digest = hashlib.sha256()
        for p in sorted(written):
            with open(p, "rb") as fh:
                digest.update(fh.read())
        print("sha256(content of all files, path-sorted):", digest.hexdigest())
        shutil.rmtree(root)
        print("check build removed (no assets written)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
