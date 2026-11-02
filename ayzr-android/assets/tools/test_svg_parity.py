#!/usr/bin/env python3
"""Cross-check: the SVG sources and the Pillow geometry must agree.

This is the test that proves the two generation routes describe the same
artwork. For each in-app glyph it rasterises the SVG with CairoSVG, renders
the Pillow equivalent, and compares the alpha silhouettes.

It does not require pixel equality — the two renderers antialias differently
and the SVG paths were authored at a slightly different scale. It requires
structural agreement: the ink must occupy the same region of the canvas and
cover a comparable fraction of it. A drifting SVG (wrong viewBox, broken
path, empty file) fails immediately.

Run from this directory:

    python3 test_svg_parity.py
"""

import io
import os
import sys

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ASSETS = os.path.dirname(HERE)
SVG_DIR = os.path.join(ASSETS, "graphics", "svg")

sys.path.insert(0, HERE)
import generate_assets as G  # noqa: E402

PAIRS = [
    ("ic_analysis.svg", "ic_analysis"),
    ("ic_news.svg", "ic_news"),
    ("ic_history.svg", "ic_history"),
    ("ic_market.svg", "ic_market"),
    ("ic_gem.svg", "ic_gem"),
    ("ic_alert.svg", "ic_alert"),
]

SIZE = 256
# Structural tolerance: the two routes need not match pixel-for-pixel, but the
# ink bounding box must not disagree by more than this fraction of the canvas,
# and the ink coverage fraction must be within this absolute delta.
BBOX_TOL = 0.22
COVERAGE_TOL = 0.20

results = []


def ink_profile(img):
    """Return (bbox, coverage) for the non-transparent region."""
    alpha = img.convert("RGBA").getchannel("A")
    bbox = alpha.getbbox()
    hist = alpha.histogram()
    opaque = sum(hist[128:])
    total = img.width * img.height
    return bbox, opaque / total


def check(name, ok, detail=""):
    results.append((name, bool(ok), detail))
    if not ok:
        print(f"FAIL  {name}  {detail}")


def main():
    import cairosvg

    for svg_name, glyph_name in PAIRS:
        svg_path = os.path.join(SVG_DIR, svg_name)
        if not os.path.exists(svg_path):
            check(f"{svg_name} exists", False, "missing")
            continue
        check(f"{svg_name} exists", True)

        png = cairosvg.svg2png(url=svg_path, output_width=SIZE,
                               output_height=SIZE)
        svg_img = Image.open(io.BytesIO(png))
        check(f"{svg_name} rasterises to {SIZE}x{SIZE}",
              svg_img.size == (SIZE, SIZE), f"actual {svg_img.size}")

        pillow_img = G.glyph(glyph_name, SIZE)

        svg_bbox, svg_cov = ink_profile(svg_img)
        pl_bbox, pl_cov = ink_profile(pillow_img)

        check(f"{svg_name} renders ink (non-empty)", svg_bbox is not None,
              "no opaque pixels")
        check(f"{glyph_name} Pillow renders ink (non-empty)",
              pl_bbox is not None, "no opaque pixels")
        if svg_bbox is None or pl_bbox is None:
            continue

        def norm(bb):
            return (bb[0] / SIZE, bb[1] / SIZE, bb[2] / SIZE, bb[3] / SIZE)

        sb, pb = norm(svg_bbox), norm(pl_bbox)
        drift = max(abs(sb[i] - pb[i]) for i in range(4))
        check(f"{glyph_name} :: SVG/Pillow ink bbox agree (<= {BBOX_TOL})",
              drift <= BBOX_TOL, f"max drift {drift:.3f} svg={sb} pillow={pb}")
        check(f"{glyph_name} :: SVG/Pillow coverage agree (<= {COVERAGE_TOL})",
              abs(svg_cov - pl_cov) <= COVERAGE_TOL,
              f"svg={svg_cov:.3f} pillow={pl_cov:.3f}")

    failed = [r for r in results if not r[1]]
    print(f"\nchecks: {len(results)}  passed: {len(results) - len(failed)}  "
          f"failed: {len(failed)}")
    if failed:
        return 1
    print("SVG sources and Pillow geometry agree structurally")
    return 0


if __name__ == "__main__":
    sys.exit(main())
