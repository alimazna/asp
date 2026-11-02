#!/usr/bin/env python3
"""AYZR asset verifier — asserts every generated asset is Android-appropriate.

Checks, per file:
  * format is what the filename and Android resource type require
  * pixel dimensions match the exact Android density ladder
  * colour mode / alpha presence matches the requirement
    (adaptive foreground and notification icons MUST be transparent;
     Play Store icon MUST be fully opaque)
  * file size is inside a sane budget

Run from this directory:

    python3 verify_assets.py

Exit code 0 only when every check passes. Prints a pass/fail total.
"""

import argparse
import os
import sys

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ASSETS = os.path.dirname(HERE)

from ayzr_palette import DENSITY_ORDER, DENSITY_SCALE  # noqa: E402

LEGACY = {d: int(round(48 * DENSITY_SCALE[d])) for d in DENSITY_ORDER}
ADAPTIVE = {d: int(round(108 * DENSITY_SCALE[d])) for d in DENSITY_ORDER}
NOTIF = {d: int(round(24 * DENSITY_SCALE[d])) for d in DENSITY_ORDER}

# Max bytes per asset class. Launcher icons are tiny by nature; a full-screen
# splash is allowed more, but must stay far below the point where it would
# bloat the APK.
MAX_LEGACY_ICON = 64 * 1024
MAX_ADAPTIVE_LAYER = 128 * 1024
MAX_NOTIF = 8 * 1024
MAX_PLAY_ICON = 1024 * 1024   # Play limit for the 512 icon is 1 MB
MAX_SPLASH = 600 * 1024
MAX_BACKGROUND = 400 * 1024
MAX_GLYPH = 32 * 1024
MAX_SVG = 16 * 1024

results = []


def check(name, ok, detail=""):
    results.append((name, bool(ok), detail))


def _open(rel):
    return Image.open(os.path.join(ASSETS, rel))


def check_alpha(rel, require_alpha, require_opaque, max_bytes, expect_size=None):
    path = os.path.join(ASSETS, rel)
    try:
        im = _open(rel)
    except Exception as exc:
        check(rel, False, f"unreadable: {exc}")
        return
    fmt = im.format
    mode = im.mode
    nbytes = os.path.getsize(path)
    has_alpha = "A" in mode

    if expect_size is not None:
        check(f"{rel} :: dimensions {expect_size}", im.size == expect_size,
              f"actual {im.size}")
    check(f"{rel} :: PNG format", fmt == "PNG", f"actual {fmt}")
    check(f"{rel} :: size <= {max_bytes}B", nbytes <= max_bytes,
          f"actual {nbytes}B")
    if require_alpha:
        check(f"{rel} :: has alpha channel", has_alpha, f"mode {mode}")
        if has_alpha:
            lo, hi = im.getchannel("A").getextrema()
            check(f"{rel} :: transparency actually used (min alpha 0)",
                  lo == 0, f"alpha range ({lo}, {hi})")
    if require_opaque:
        if has_alpha:
            lo, hi = im.getchannel("A").getextrema()
            check(f"{rel} :: fully opaque (min alpha 255)", lo == 255,
                  f"alpha range ({lo}, {hi})")
        else:
            check(f"{rel} :: fully opaque (no alpha channel)", True, mode)


def check_adaptive_safe_zone(rel, px):
    """Adaptive layers may only draw inside the central 72dp of the 108dp canvas.

    The guaranteed-visible region is a circle of radius 36/108 of the canvas
    (0.3333). Anything outside can be masked off by the launcher.

    The metric is the true maximum distance from the canvas centre to any
    pixel with meaningful alpha — not the bounding-box corner, which would
    overestimate by up to sqrt(2) for a ring-shaped mark and report false
    failures.
    """
    im = _open(rel).convert("RGBA")
    alpha = im.getchannel("A")
    data = alpha.get_flattened_data()
    cx = cy = (px - 1) / 2.0
    # Ignore near-transparent antialiasing/bloom fringe; treat >=16/255 as ink.
    worst = 0.0
    for idx, a in enumerate(data):
        if a < 16:
            continue
        x = idx % px
        y = idx // px
        d = ((x - cx) ** 2 + (y - cy) ** 2) ** 0.5
        if d > worst:
            worst = d
    if worst == 0.0:
        check(f"{rel} :: adaptive layer draws something", False, "empty")
        return
    safe_r = px * (36.0 / 108.0)
    check(f"{rel} :: ink inside 72dp safe zone (max r {worst:.1f} <= {safe_r:.1f}px)",
          worst <= safe_r,
          f"max ink radius {worst:.1f}px exceeds safe radius {safe_r:.1f}px")


def _hsv(r, g, b):
    """Minimal RGB->HSV (h in degrees, s and v in 0..1)."""
    mx, mn = max(r, g, b), min(r, g, b)
    d = mx - mn
    if d == 0:
        h = 0.0
    elif mx == r:
        h = (60 * ((g - b) / d)) % 360
    elif mx == g:
        h = 60 * ((b - r) / d) + 120
    else:
        h = 60 * ((r - g) / d) + 240
    s = 0.0 if mx == 0 else d / mx
    return h, s, mx / 255.0


def check_gold_and_navy(rel, min_gold_frac, max_gold_hue_err=18.0,
                        min_gold_sat=0.45, max_mean_lum=110.0,
                        expect_navy=True):
    """Assert the artwork is genuinely navy + gold, not washed-out gray.

    This is the check that would have caught the earlier defect where a
    `screen`-blended bloom desaturated the gold into gray.

    `expect_navy=False` for assets that are a gold mark on transparency with no
    navy surface of their own (e.g. the in-app brand mark).
    """
    im = _open(rel).convert("RGBA")
    data = im.get_flattened_data()
    gold = 0
    lum_sum = 0
    opaque = 0
    for r, g, b, a in data:
        if a < 32:
            continue
        opaque += 1
        lum_sum += 0.299 * r + 0.587 * g + 0.114 * b
        h, s, v = _hsv(r, g, b)
        if s >= min_gold_sat and abs(h - 43.0) <= max_gold_hue_err and v > 0.35:
            gold += 1
    if opaque == 0:
        check(f"{rel} :: has opaque pixels", False, "fully transparent")
        return
    gold_frac = gold / opaque
    check(f"{rel} :: gold present (>= {min_gold_frac:.0%} of opaque px)",
          gold_frac >= min_gold_frac, f"actual {gold_frac:.1%}")
    if expect_navy:
        mean_lum = lum_sum / opaque
        check(f"{rel} :: surface is dark navy (mean lum <= {max_mean_lum:.0f})",
              mean_lum <= max_mean_lum, f"actual {mean_lum:.1f}")


def check_mark_centered(rel, tol=0.06):
    """The mark's ink centroid must sit near the canvas centre."""
    im = _open(rel).convert("RGBA")
    alpha = im.getchannel("A")
    px = im.width
    data = alpha.get_flattened_data()
    sx = sy = n = 0
    for i, a in enumerate(data):
        if a < 32:
            continue
        sx += i % px
        sy += i // px
        n += 1
    if n == 0:
        check(f"{rel} :: ink centroid", False, "no ink")
        return
    cx, cy = sx / n, sy / n
    dx = abs(cx - (px - 1) / 2) / px
    dy = abs(cy - (px - 1) / 2) / px
    check(f"{rel} :: mark centred (dx,dy <= {tol})",
          dx <= tol and dy <= tol, f"offset ({dx:.3f}, {dy:.3f})")


def check_svg(rel):
    path = os.path.join(ASSETS, rel)
    nbytes = os.path.getsize(path)
    with open(path, encoding="utf-8") as fh:
        text = fh.read()
    check(f"{rel} :: size <= {MAX_SVG}B", nbytes <= MAX_SVG, f"actual {nbytes}B")
    check(f"{rel} :: is SVG root element", "<svg" in text, "")
    check(f"{rel} :: has viewBox", "viewBox=" in text, "")
    check(f"{rel} :: has xmlns",
          'xmlns="http://www.w3.org/2000/svg"' in text, "")
    check(f"{rel} :: no embedded raster data",
          "base64" not in text and "<image" not in text, "")


def main():
    global ASSETS
    ap = argparse.ArgumentParser(description="Verify AYZR Android assets.")
    ap.add_argument("--root", default=ASSETS,
                    help="asset root to verify (used by test_verifier.py)")
    args = ap.parse_args()
    ASSETS = os.path.abspath(args.root)
    results.clear()

    print(f"verifying assets under {ASSETS}\n")

    # --- Legacy launcher icons -------------------------------------------
    for d in DENSITY_ORDER:
        check_alpha(
            f"android-res/mipmap-{d}/ic_launcher.png",
            require_alpha=True, require_opaque=False,
            max_bytes=MAX_LEGACY_ICON, expect_size=(LEGACY[d], LEGACY[d]),
        )

    # --- Adaptive icon layers -------------------------------------------
    for d in DENSITY_ORDER:
        px = ADAPTIVE[d]
        check_alpha(
            f"android-res/mipmap-{d}/ic_launcher_foreground.png",
            require_alpha=True, require_opaque=False,
            max_bytes=MAX_ADAPTIVE_LAYER, expect_size=(px, px),
        )
        check_alpha(
            f"android-res/mipmap-{d}/ic_launcher_background.png",
            require_alpha=False, require_opaque=True,
            max_bytes=MAX_ADAPTIVE_LAYER, expect_size=(px, px),
        )
        check_alpha(
            f"android-res/mipmap-{d}/ic_launcher_monochrome.png",
            require_alpha=True, require_opaque=False,
            max_bytes=MAX_ADAPTIVE_LAYER, expect_size=(px, px),
        )
        # Adaptive layers must respect the 72dp guaranteed-visible safe zone.
        check_adaptive_safe_zone(
            f"android-res/mipmap-{d}/ic_launcher_foreground.png", px)
        check_adaptive_safe_zone(
            f"android-res/mipmap-{d}/ic_launcher_monochrome.png", px)

    # --- Notification small icons ---------------------------------------
    for d in DENSITY_ORDER:
        px = NOTIF[d]
        rel = f"android-res/drawable-{d}/ic_stat_ayzr.png"
        check_alpha(rel, require_alpha=True, require_opaque=False,
                    max_bytes=MAX_NOTIF, expect_size=(px, px))
        im = _open(rel).convert("RGBA")
        nonwhite = [p for p in im.get_flattened_data()
                    if p[3] > 0 and p[:3] != (255, 255, 255)]
        check(f"{rel} :: opaque pixels are pure white",
              not nonwhite, f"{len(nonwhite)} non-white opaque pixels")

    # --- Play Store icon ------------------------------------------------
    check_alpha("icons/play_store_icon_512.png", require_alpha=False,
                require_opaque=True, max_bytes=MAX_PLAY_ICON,
                expect_size=(512, 512))

    # --- In-app brand marks ---------------------------------------------
    check_alpha("icons/brand_mark_256.png", require_alpha=True,
                require_opaque=False, max_bytes=MAX_GLYPH * 4,
                expect_size=(256, 256))
    check_alpha("icons/brand_mark_96.png", require_alpha=True,
                require_opaque=False, max_bytes=MAX_GLYPH * 2,
                expect_size=(96, 96))

    # --- Splash / backgrounds -------------------------------------------
    check_alpha("images/splash_1080x1920.png", require_alpha=False,
                require_opaque=True, max_bytes=MAX_SPLASH,
                expect_size=(1080, 1920))
    check_alpha("images/splash_1440x2560.png", require_alpha=False,
                require_opaque=True, max_bytes=MAX_SPLASH,
                expect_size=(1440, 2560))
    check_alpha("images/splash_tablet_1200x1920.png", require_alpha=False,
                require_opaque=True, max_bytes=MAX_SPLASH,
                expect_size=(1200, 1920))
    check_alpha("images/app_background_1080x1920.png", require_alpha=False,
                require_opaque=True, max_bytes=MAX_BACKGROUND,
                expect_size=(1080, 1920))

    webp = os.path.join(ASSETS, "images/app_background_1080x1920.webp")
    with Image.open(webp) as im:
        check("images/app_background_1080x1920.webp :: WEBP format",
              im.format == "WEBP", f"actual {im.format}")
        check("images/app_background_1080x1920.webp :: dimensions (1080, 1920)",
              im.size == (1080, 1920), f"actual {im.size}")
    check(f"images/app_background_1080x1920.webp :: size <= {MAX_BACKGROUND}B",
          os.path.getsize(webp) <= MAX_BACKGROUND,
          f"actual {os.path.getsize(webp)}B")

    # --- In-app glyphs ---------------------------------------------------
    for name in ("ic_analysis", "ic_news", "ic_history", "ic_market",
                 "ic_gem", "ic_alert"):
        for px in (24, 48, 96):
            rel = f"graphics/png/{name}_{px}.png"
            check_alpha(rel, require_alpha=True, require_opaque=False,
                        max_bytes=MAX_GLYPH, expect_size=(px, px))

    # --- SVG sources -----------------------------------------------------
    svg_dir = os.path.join(ASSETS, "graphics/svg")
    for name in sorted(os.listdir(svg_dir)):
        check_svg(f"graphics/svg/{name}")

    # --- Splash mark drawables ------------------------------------------
    for rel, px in (("android-res/drawable/ayzr_splash_mark.png", 288),
                    ("android-res/drawable-xxhdpi/ayzr_splash_mark.png", 432),
                    ("android-res/drawable-xxxhdpi/ayzr_splash_mark.png", 576)):
        check_alpha(rel, require_alpha=True, require_opaque=False,
                    max_bytes=MAX_GLYPH * 8, expect_size=(px, px))

    # --- Android XML resources -------------------------------------------
    xml_checks = {
        "android-res/mipmap-anydpi-v26/ic_launcher.xml": [
            "<adaptive-icon", 'xmlns:android="http://schemas.android.com/apk/res/android"',
            "@mipmap/ic_launcher_foreground", "@mipmap/ic_launcher_monochrome",
            "@color/ic_launcher_background",
        ],
        "android-res/mipmap-anydpi-v26/ic_launcher_round.xml": [
            "<adaptive-icon", "@mipmap/ic_launcher_foreground",
        ],
        "android-res/values/ic_launcher_background.xml": [
            "<resources", 'name="ic_launcher_background"', "#070B18",
        ],
        "android-res/drawable/launch_background.xml": [
            "<layer-list", "@drawable/ayzr_splash_mark", "@color/ic_launcher_background",
        ],
        "android-res/drawable-v21/launch_background.xml": [
            "<layer-list", "@drawable/ayzr_splash_mark",
        ],
        "android-res/values/colors.xml": ["<resources", "ayzr_gold", "#D4AF37"],
    }
    for rel, needles in xml_checks.items():
        path = os.path.join(ASSETS, rel)
        if not os.path.exists(path):
            check(f"{rel} :: exists", False, "missing")
            continue
        check(f"{rel} :: exists", True)
        with open(path, encoding="utf-8") as fh:
            text = fh.read()
        for needle in needles:
            check(f"{rel} :: contains {needle!r}", needle in text, "")

    # --- Every referenced drawable/mipmap must actually exist ------------
    referenced = [
        "android-res/drawable/ayzr_splash_mark.png",
        "android-res/drawable-xxhdpi/ayzr_splash_mark.png",
        "android-res/drawable-xxxhdpi/ayzr_splash_mark.png",
    ]
    for rel in referenced:
        check(f"{rel} :: referenced by launch_background.xml",
              os.path.exists(os.path.join(ASSETS, rel)), "")

    # --- Colour / geometry sanity ---------------------------------------
    # The icon must read as dark navy with a real gold mark. These bounds are
    # deliberately loose: they assert "gold and navy", not a specific design.
    check_gold_and_navy("android-res/mipmap-xxxhdpi/ic_launcher.png",
                        min_gold_frac=0.02)
    check_gold_and_navy("icons/play_store_icon_512.png", min_gold_frac=0.02)
    check_gold_and_navy("icons/brand_mark_256.png", min_gold_frac=0.20,
                        expect_navy=False)
    for d in ("mdpi", "xxxhdpi"):
        check_gold_and_navy(f"android-res/mipmap-{d}/ic_launcher_foreground.png",
                            min_gold_frac=0.10, max_mean_lum=200.0)
    for rel in ("android-res/mipmap-xxxhdpi/ic_launcher.png",
                "icons/play_store_icon_512.png",
                "icons/brand_mark_256.png",
                "android-res/mipmap-xxxhdpi/ic_launcher_foreground.png"):
        check_mark_centered(rel)

    # The splash must be dark overall, with the mark's warm core near the
    # intended position (40% down the canvas).
    with Image.open(os.path.join(ASSETS, "images/splash_1080x1920.png")) as sp:
        rgb = sp.convert("RGB")
        w, h = rgb.size
        top = rgb.crop((0, 0, w, h // 8))
        px = list(top.get_flattened_data())
        mean = sum(0.299 * r + 0.587 * g + 0.114 * b for r, g, b in px) / len(px)
        check("images/splash_1080x1920.png :: top band is dark navy (<70)",
              mean < 70, f"mean lum {mean:.1f}")
        core = rgb.getpixel((w // 2, int(h * 0.40)))
        check("images/splash_1080x1920.png :: mark core is warm (R > B)",
              core[0] > core[2] + 10, f"centre pixel {core}")

    # --- Font licence present -------------------------------------------
    check("fonts/Cairo-OFL.txt :: licence file present",
          os.path.exists(os.path.join(ASSETS, "fonts/Cairo-OFL.txt")), "")

    # --- Report ----------------------------------------------------------
    failed = [(n, d) for n, ok, d in results if not ok]
    for name, ok, detail in results:
        if not ok:
            print(f"FAIL  {name}  {detail}")
    print(f"\nchecks: {len(results)}  passed: {len(results) - len(failed)}  "
          f"failed: {len(failed)}")
    if failed:
        print("\nfailed checks:")
        for n, d in failed:
            print(f"  - {n} ({d})")
        return 1
    print("ALL CHECKS PASSED")
    return 0


if __name__ == "__main__":
    sys.exit(main())
