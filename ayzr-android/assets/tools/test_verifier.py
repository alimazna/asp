#!/usr/bin/env python3
"""Self-test for the AYZR asset verifier.

A verifier that always passes is worthless. This script mutates a scratch
copy of the asset tree in ways that are genuinely wrong for Android, runs
verify_assets.py against each mutant, and asserts the verifier FAILS. If any
mutant still verifies clean, the verifier has a blind spot and this test fails.

Run from this directory:

    python3 test_verifier.py
"""

import os
import re
import shutil
import subprocess
import sys
import tempfile

from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
ASSETS = os.path.dirname(HERE)
VERIFY = os.path.join(HERE, "verify_assets.py")


def run_verifier(root):
    """Run the verifier against `root`; return (returncode, passed, failed, out)."""
    proc = subprocess.run(
        [sys.executable, VERIFY, "--root", root],
        capture_output=True, text=True,
    )
    out = proc.stdout + proc.stderr
    passed = failed = None
    m = re.search(r"checks:\s*(\d+)\s+passed:\s*(\d+)\s+failed:\s*(\d+)", out)
    if m:
        passed, failed = int(m.group(2)), int(m.group(3))
    return proc.returncode, passed, failed, out


# --- mutations: each is a genuine Android defect -------------------------

def mut_wrong_density(root):
    """mdpi launcher icon written at hdpi pixel size."""
    p = os.path.join(root, "android-res/mipmap-mdpi/ic_launcher.png")
    Image.new("RGBA", (72, 72), (0, 0, 0, 255)).save(p)
    return "mdpi ic_launcher at 72px instead of 48px"


def mut_foreground_opaque(root):
    """Adaptive foreground must have transparent corners; flatten it."""
    p = os.path.join(root, "android-res/mipmap-xxhdpi/ic_launcher_foreground.png")
    with Image.open(p) as im:
        flat = Image.new("RGBA", im.size, (7, 11, 24, 255))
        flat.alpha_composite(im.convert("RGBA"))
        flat.convert("RGB").save(p)
    return "adaptive foreground flattened opaque (no transparency)"


def mut_background_transparent(root):
    """Adaptive background layer must be fully opaque."""
    p = os.path.join(root, "android-res/mipmap-xhdpi/ic_launcher_background.png")
    with Image.open(p) as im:
        rgba = im.convert("RGBA")
        rgba.putalpha(128)
        rgba.save(p)
    return "adaptive background layer made semi-transparent"


def mut_notification_tinted(root):
    """Notification glyph must be pure white; Android tints it itself."""
    p = os.path.join(root, "android-res/drawable-xhdpi/ic_stat_ayzr.png")
    with Image.open(p) as im:
        rgba = im.convert("RGBA")
        gold = Image.new("RGBA", rgba.size, (212, 175, 55, 255))
        gold.putalpha(rgba.getchannel("A"))
        gold.save(p)
    return "notification icon filled gold instead of white"


def mut_play_icon_alpha(root):
    """Play Store icon must be opaque."""
    p = os.path.join(root, "icons/play_store_icon_512.png")
    Image.new("RGBA", (512, 512), (7, 11, 24, 128)).save(p)
    return "Play Store icon with an alpha channel"


def mut_splash_jpeg_named_png(root):
    """Wrong format for the extension / resource type."""
    p = os.path.join(root, "images/splash_1080x1920.png")
    with Image.open(p) as im:
        im.convert("RGB").save(p, "JPEG", quality=80)
    return "splash saved as JPEG bytes under a .png name"


def mut_svg_with_embedded_raster(root):
    """SVG must be vector, not a base64-wrapped bitmap."""
    p = os.path.join(root, "graphics/svg/ic_gem.svg")
    with open(p, "w", encoding="utf-8") as fh:
        fh.write(
            '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 8 8" '
            'width="8" height="8"><image width="8" height="8" '
            'href="data:image/png;base64,iVBORw0KGgo="/></svg>'
        )
    return "SVG carrying an embedded base64 raster"


def mut_oversized_icon(root):
    """File-size budget: a bloated launcher icon."""
    p = os.path.join(root, "android-res/mipmap-xxxhdpi/ic_launcher.png")
    im = Image.new("RGBA", (192, 192))
    px = im.load()
    for y in range(192):
        for x in range(192):
            px[x, y] = ((x * 7 + y * 13) % 256, (x * 31 + y) % 256,
                        (x + y * 17) % 256, 255)
    im.save(p, "PNG", optimize=False, compress_level=0)
    return "launcher icon over the file-size budget"


def mut_foreground_bleeds_outside_safe_zone(root):
    """Adaptive foreground ink pushed into the outer ring the launcher masks."""
    p = os.path.join(root, "android-res/mipmap-xxhdpi/ic_launcher_foreground.png")
    with Image.open(p) as im:
        rgba = im.convert("RGBA")
    d = ImageDraw.Draw(rgba)
    # A ring near the canvas edge: outside the 72dp guaranteed-visible circle.
    d.ellipse((4, 4, rgba.width - 5, rgba.height - 5),
              outline=(212, 175, 55, 255), width=8)
    rgba.save(p)
    return "adaptive foreground ink outside the 72dp safe zone"


def mut_icon_desaturated_to_gray(root):
    """The real defect this verifier was written for: gold bloomed to gray."""
    p = os.path.join(root, "android-res/mipmap-xxxhdpi/ic_launcher.png")
    with Image.open(p) as im:
        rgba = im.convert("RGBA")
    grey = rgba.convert("L").convert("RGBA")
    grey.putalpha(rgba.getchannel("A"))
    grey.save(p)
    return "launcher icon desaturated to gray (no gold left)"


def mut_mark_off_center(root):
    """Mark shifted out of the canvas centre."""
    p = os.path.join(root, "icons/brand_mark_256.png")
    with Image.open(p) as im:
        rgba = im.convert("RGBA")
    shifted = Image.new("RGBA", rgba.size, (0, 0, 0, 0))
    shifted.alpha_composite(rgba, (60, 60))
    shifted.save(p)
    return "brand mark shifted off centre"


MUTATIONS = [
    mut_wrong_density,
    mut_foreground_opaque,
    mut_background_transparent,
    mut_notification_tinted,
    mut_play_icon_alpha,
    mut_splash_jpeg_named_png,
    mut_svg_with_embedded_raster,
    mut_oversized_icon,
    mut_foreground_bleeds_outside_safe_zone,
    mut_icon_desaturated_to_gray,
    mut_mark_off_center,
]


def main():
    # 0. Baseline: the pristine tree must verify clean.
    code, passed, failed, out = run_verifier(ASSETS)
    if code != 0 or failed != 0:
        print("BASELINE FAILED — pristine assets do not verify clean")
        print(out)
        return 1
    print(f"baseline: {passed} checks passed, 0 failed")

    # 1. Each mutant must be caught.
    caught = 0
    for mut in MUTATIONS:
        scratch = tempfile.mkdtemp(prefix="ayzr-mutant-")
        try:
            shutil.copytree(ASSETS, scratch, dirs_exist_ok=True)
            desc = mut(scratch)
            code, passed, failed, out = run_verifier(scratch)
            if code != 0 and failed and failed > 0:
                print(f"  CAUGHT   {desc}  ({failed} failed checks)")
                caught += 1
            else:
                print(f"  MISSED   {desc}  (verifier still passed)")
                print(out)
        finally:
            shutil.rmtree(scratch, ignore_errors=True)

    total = len(MUTATIONS)
    print(f"\nmutations: {total}  caught: {caught}  missed: {total - caught}")
    if caught != total:
        print("VERIFIER HAS BLIND SPOTS")
        return 1
    print("ALL MUTATIONS CAUGHT — verifier is sensitive to real defects")
    return 0


if __name__ == "__main__":
    sys.exit(main())
