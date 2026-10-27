"""Deterministic drawing primitives for the AYZR asset pipeline.

Everything here is pure geometry: supersampled vector-style rasterisation with
Pillow. There is no generative model involved and no randomness, so every run
produces byte-identical output for a given Pillow version.

Deliberate constraint: the decorative motifs are abstract (facets, arcs,
grids, bars). No candlestick bodies/wicks are drawn, because a candlestick
motif could be misread as real market data. See docs/ayzr/VISUAL_DESIGN.md.
"""

from PIL import Image, ImageChops, ImageDraw, ImageFilter

from ayzr_palette import (
    CLEAR,
    GOLD_400,
    GOLD_500,
    GOLD_600,
    NAVY,
    NAVY_DEEP,
    NAVY_700,
    NAVY_800,
)

# Supersampling factor for antialiasing. Pillow has no vector renderer, so
# drawing at 4x and downsampling with LANCZOS is the standard technique.
SS = 4


def render(size, draw_fn, ss=SS):
    """Draw at `size` * ss then downsample to `size`.

    draw_fn(draw, side) receives an ImageDraw and the supersampled side length.
    Returns an RGBA image of exactly `size` x `size`.
    """
    side = size * ss
    img = Image.new("RGBA", (side, side), CLEAR)
    draw_fn(ImageDraw.Draw(img), side)
    return img.resize((size, size), Image.LANCZOS)


def render_rect(size, draw_fn, ss=SS):
    """Like render() but for non-square canvases. size is (w, h)."""
    w, h = size
    img = Image.new("RGBA", (w * ss, h * ss), CLEAR)
    draw_fn(ImageDraw.Draw(img), w * ss, h * ss)
    return img.resize((w, h), Image.LANCZOS)


def linear_gradient(size, stops, horizontal=False):
    """Vertical (default) or horizontal multi-stop gradient, RGB.

    stops: list of (position 0..1, (r, g, b)). Returned image is RGB.
    """
    w, h = size
    span = w if horizontal else h
    strip = Image.new("RGB", (span, 1) if horizontal else (1, span))
    px = strip.load()
    stops = sorted(stops, key=lambda s: s[0])
    for i in range(span):
        t = i / max(1, span - 1)
        lo, hi = stops[0], stops[-1]
        for j in range(len(stops) - 1):
            if stops[j][0] <= t <= stops[j + 1][0]:
                lo, hi = stops[j], stops[j + 1]
                break
        width = hi[0] - lo[0]
        f = 0.0 if width == 0 else (t - lo[0]) / width
        value = tuple(
            int(round(lo[1][c] + (hi[1][c] - lo[1][c]) * f)) for c in range(3)
        )
        if horizontal:
            px[i, 0] = value
        else:
            px[0, i] = value
    return strip.resize((w, h))


def rounded_rect_mask(size, radius, ss=SS):
    """Antialiased rounded-square mask (L mode)."""
    side = size * ss
    m = Image.new("L", (side, side), 0)
    ImageDraw.Draw(m).rounded_rectangle(
        (0, 0, side - 1, side - 1), radius=radius * ss, fill=255
    )
    return m.resize((size, size), Image.LANCZOS)


def circle_mask(size, ss=SS):
    side = size * ss
    m = Image.new("L", (side, side), 0)
    ImageDraw.Draw(m).ellipse((0, 0, side - 1, side - 1), fill=255)
    return m.resize((size, size), Image.LANCZOS)


def _facet_points(side, inset=0.5):
    """Normalised facet geometry for the AYZR gem mark.

    A rhombus ('brilliant' silhouette) with a table line and facet edges.
    Deterministic: no randomness, no data-derived numbers.
    """
    m = inset
    span = 1.0 - 2 * m
    top = (m + span * 0.5, m)
    right = (m + span, m + span * 0.34)
    bottom = (m + span * 0.5, m + span)
    left = (m, m + span * 0.34)
    table_y = m + span * 0.30
    table_l = (m + span * 0.24, table_y)
    table_r = (m + span * 0.76, table_y)
    return {
        "outline": [top, right, bottom, left],
        "top": top,
        "right": right,
        "bottom": bottom,
        "left": left,
        "table_l": table_l,
        "table_r": table_r,
    }


def draw_gem(draw, side, inset=0.5, stroke=None, outline_w=None,
             facets=True, fill=None):
    """Draw the faceted gem mark. All coordinates derive from `side`."""
    stroke = stroke or GOLD_500
    p = _facet_points(side, inset)
    pts = [(x * side, y * side) for x, y in p["outline"]]
    lw = outline_w or max(1, int(round(side * 0.035)))
    if fill is not None:
        draw.polygon(pts, fill=fill)
    draw.line(pts + [pts[0]], fill=stroke, width=lw, joint="curve")
    if facets:
        tl = (p["table_l"][0] * side, p["table_l"][1] * side)
        tr = (p["table_r"][0] * side, p["table_r"][1] * side)
        top = (p["top"][0] * side, p["top"][1] * side)
        left = (p["left"][0] * side, p["left"][1] * side)
        right = (p["right"][0] * side, p["right"][1] * side)
        bot = (p["bottom"][0] * side, p["bottom"][1] * side)
        fw = max(1, int(round(side * 0.018)))
        draw.line([tl, tr], fill=stroke, width=fw)
        draw.line([tl, left], fill=stroke, width=fw)
        draw.line([tr, right], fill=stroke, width=fw)
        draw.line([tl, bot], fill=stroke, width=fw)
        draw.line([tr, bot], fill=stroke, width=fw)
    return p


def draw_arcs(draw, side, center=None, radii=None, color=None, width=None):
    """Concentric precision arcs — abstract 'instrument dial' motif."""
    color = color or GOLD_600
    cx, cy = center or (side * 0.5, side * 0.5)
    radii = radii or [0.34, 0.42, 0.50]
    w = width or max(1, int(round(side * 0.012)))
    for i, r in enumerate(radii):
        r_px = r * side
        start = 200 + i * 14
        end = 340 - i * 10
        draw.arc(
            (cx - r_px, cy - r_px, cx + r_px, cy + r_px),
            start=start, end=end, fill=color, width=w,
        )


def draw_grid(draw, side, spacing=0.08, color=None, width=None):
    """Faint measurement grid over a square canvas."""
    draw_grid_rect(draw, side, side, spacing=spacing, color=color, width=width,
                   ref=side)


def draw_grid_rect(draw, w, h, spacing=0.08, color=None, width=None, ref=None):
    """Faint measurement grid over a w x h canvas.

    `spacing` is a fraction of `ref` (defaults to the smaller dimension) so a
    non-square canvas keeps square grid cells.
    """
    color = color or (24, 38, 52, 90)
    ref = ref or min(w, h)
    w_line = width or max(1, int(round(ref * 0.004)))
    step = spacing * ref
    x = step
    while x < w:
        draw.line([(x, 0), (x, h)], fill=color, width=w_line)
        x += step
    y = step
    while y < h:
        draw.line([(0, y), (w, y)], fill=color, width=w_line)
        y += step


def draw_bars(draw, side, color=None, origin=None):
    """Three ascending bars — abstract 'level/scale' glyph, not a price chart."""
    color = color or GOLD_500
    w = side * 0.10
    gap = side * 0.055
    heights = (0.24, 0.38, 0.54)
    base = side * 0.76
    total = len(heights) * w + (len(heights) - 1) * gap
    x0 = (side - total) / 2
    r = w * 0.30
    for i, h in enumerate(heights):
        x = x0 + i * (w + gap)
        draw.rounded_rectangle(
            (x, base - side * h, x + w, base), radius=r, fill=color
        )


def draw_waveform(draw, side, color=None, points=None):
    """Abstract polyline motif (news/analysis glyph). Not price data."""
    color = color or GOLD_500
    pts = points or [0.10, 0.34, 0.22, 0.46, 0.30, 0.62, 0.44, 0.78]
    w = max(2, int(round(side * 0.055)))
    coords = [
        (side * (0.16 + i * 0.10), side * (0.72 - v * 0.46))
        for i, v in enumerate(pts)
    ]
    draw.line(coords, fill=color, width=w, joint="curve")


def draw_doc(draw, side, color=None, fill=None):
    """Abstract document/ledger glyph (history). No readable text invented."""
    color = color or GOLD_500
    w = max(2, int(round(side * 0.035)))
    x0, y0, x1, y1 = side * 0.22, side * 0.16, side * 0.78, side * 0.84
    if fill is not None:
        draw.rounded_rectangle((x0, y0, x1, y1), radius=side * 0.05, fill=fill)
    draw.rounded_rectangle(
        (x0, y0, x1, y1), radius=side * 0.05, outline=color, width=w
    )
    for i in range(3):
        y = y0 + (y1 - y0) * (0.34 + i * 0.22)
        draw.line([(x0 + side * 0.10, y), (x1 - side * 0.10, y)],
                  fill=color, width=max(1, w // 2))


def radial_mask(size, falloff=2.0, ss=2):
    """Radial alpha mask: opaque at the centre, 0 at the edge, smooth falloff.

    Used for soft halos. `falloff` > 1 keeps the bright core small.
    """
    side = size * ss
    m = Image.new("L", (side, side), 0)
    px = m.load()
    c = (side - 1) / 2.0
    inv = 1.0 / c
    for y in range(side):
        dy = (y - c) * inv
        for x in range(side):
            dx = (x - c) * inv
            r = (dx * dx + dy * dy) ** 0.5
            if r >= 1.0:
                continue
            px[x, y] = int(round(255 * (1.0 - r) ** falloff))
    return m.resize((size, size), Image.LANCZOS)


def add_glow(img, radius=None, color=None, strength=0.55):
    """Gold bloom around bright pixels, without desaturating them.

    A `screen` blend is wrong here: screening a gold stroke (high R/G, low B)
    with its own blur lifts the blue channel far more than red/green and turns
    the stroke gray. Instead the blurred luminance is used as an alpha mask on
    a flat gold layer, composited *behind* the source. Colour is preserved.
    """
    color = color or GOLD_500
    radius = radius or max(2, img.size[0] // 40)
    rgba = img.convert("RGBA")
    # Luminance weighted by the source alpha, so transparent areas contribute
    # no bloom at all.
    r, g, b, a = rgba.split()
    lum = Image.merge("RGB", (r, g, b)).convert("L")
    lum = ImageChops.multiply(lum, a)
    blurred = lum.filter(ImageFilter.GaussianBlur(radius))
    if strength != 1.0:
        blurred = blurred.point(lambda v: int(v * strength))
    glow = Image.new("RGBA", rgba.size, color + (0,))
    glow.putalpha(blurred)
    out = Image.new("RGBA", rgba.size, CLEAR)
    out.alpha_composite(glow)
    out.alpha_composite(rgba)
    return out


def make_mark(size, gold=GOLD_500, glow=False, inset=0.18):
    """The AYZR mark: faceted gem inside a gold ring. RGBA, transparent bg."""
    def _fn(draw, side):
        draw_gem(draw, side, inset=inset, stroke=gold,
                 outline_w=max(1, int(round(side * 0.028))))
        draw_arcs(draw, side, radii=[0.30, 0.38], color=GOLD_600,
                  width=max(1, int(round(side * 0.010))))
    img = render(size, _fn)
    return add_glow(img) if glow else img


def navy_background(size, stops=None):
    """Dark-navy vertical gradient canvas (RGB).

    Default stops follow the shipped theme: navyDeep at the top through navy
    to a slightly darker floor, so the bitmap matches scaffoldBackgroundColor.
    """
    stops = stops or [
        (0.0, NAVY_DEEP),
        (0.45, NAVY),
        (1.0, (4, 7, 15)),
    ]
    return linear_gradient(size, stops)


__all__ = [
    "SS", "render", "render_rect", "linear_gradient", "rounded_rect_mask",
    "circle_mask", "draw_gem", "draw_arcs", "draw_grid", "draw_bars",
    "draw_waveform", "draw_doc", "add_glow", "make_mark", "navy_background",
    "GOLD_400", "GOLD_500", "GOLD_600", "NAVY", "NAVY_DEEP", "NAVY_700",
    "NAVY_800",
]
