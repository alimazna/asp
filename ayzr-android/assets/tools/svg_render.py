#!/usr/bin/env python3
"""Rasterise the AYZR SVG sources with CairoSVG.

Two independent routes produce the AYZR artwork:

  * `generate_assets.py` — Pillow geometry (no SVG dependency)
  * this script         — CairoSVG from `graphics/svg/*.svg`

Keeping both is deliberate: if Pillow is ever unavailable the SVG route still
yields the artwork, and comparing the two is how the vector sources are proven
to be real (see `test_svg_parity.py`).

Usage:
    python3 svg_render.py --out /tmp/ayzr-svg
    python3 svg_render.py --out DIR --size 512
"""

import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ASSETS = os.path.dirname(HERE)
SVG_DIR = os.path.join(ASSETS, "graphics", "svg")

# Full-screen SVGs keep their authored portrait canvas instead of a square.
PORTRAIT_CANVAS = {
    "splash_background": (1080, 1920),
    "app_background": (1080, 1920),
}


def render(svg_path, out_path, size):
    import cairosvg

    name = os.path.basename(svg_path)[:-4]
    if name in PORTRAIT_CANVAS:
        w, h = PORTRAIT_CANVAS[name]
    else:
        w = h = size
    cairosvg.svg2png(url=svg_path, write_to=out_path,
                     output_width=w, output_height=h)
    return out_path, (w, h)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True)
    ap.add_argument("--size", type=int, default=256)
    args = ap.parse_args()

    os.makedirs(args.out, exist_ok=True)
    names = sorted(f for f in os.listdir(SVG_DIR) if f.endswith(".svg"))
    for f in names:
        src = os.path.join(SVG_DIR, f)
        dst = os.path.join(args.out, f[:-4] + ".png")
        path, size = render(src, dst, args.size)
        print(f"{f:26s} -> {str(size):14s} {os.path.getsize(path):>8d} B")
    print(f"\n{len(names)} SVGs rasterised into {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
