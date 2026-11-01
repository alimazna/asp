# AYZR — Visual Design & Asset Pipeline

Owner: Agent D (Image Design & Editing).
Assets live under `ayzr-android/assets/**`. The Android resource copies are
handed to Agent B by `tools/install_android_res.py` (Agent B owns
`android/app/src/main/res/**`).

---

## 1. Honest capability boundary — read this first

This is the most important section. It states plainly what the workflow does
and does not do.

### What the workflow CAN do

| Capability | How | Evidence |
|---|---|---|
| Deterministic raster generation | Pillow 13.0.0 geometry at 4x supersample, LANCZOS downsample | `tools/generate_assets.py`; identical SHA-256 across runs |
| Exact colour control | Token palette shared with the app theme | `tools/ayzr_palette.py` == `lib/core/theme.dart` |
| Alpha / transparency | `RGBA` layers composited with `Image.alpha_composite` | verified per file (`alpha` extrema asserted) |
| Density-correct launcher icons | 48dp ladder: 48/72/96/144/192 px | 361 automated checks |
| Adaptive icons (API 26+) | 108dp fg/bg/monochrome layers, 72dp safe zone enforced | safe-zone radius check, mutation-tested |
| Vector output | Hand-authored SVG with explicit `viewBox` | 9 SVGs, all rasterise via CairoSVG |
| SVG ↔ raster cross-check | Two independent routes compared structurally | `tools/test_svg_parity.py` 36/36 |
| Format conversion | PNG, WEBP, JPEG, AVIF, TIFF, BMP (Pillow build) | see feature probe below |
| Deterministic editing | Crop, resize, blur, colour ops, compositing | used throughout |
| Arabic text rasterisation | Cairo font + raqm shaping engine, RTL | `direction="rtl"`, `language="ar"` |

### What the workflow CANNOT do

| Not available | Consequence |
|---|---|
| **No generative AI image synthesis** | There is **no** Stable Diffusion, no DALL·E, no Imagen, no Flux, no text-to-image model of any kind installed. Every asset here is procedurally drawn geometry. Nothing was "AI-generated". |
| **No GPU** | `ls /dev/nvidia*` → none. A diffusion model could not run at usable speed even if installed. |
| **No ImageMagick / GraphicsMagick** | `convert`, `magick` absent. Shell-pipeline image editing is unavailable; Pillow is the only raster tool. |
| **No Inkscape / librsvg / resvg** | SVG *authoring* and *rendering* cannot be done by those tools. CairoSVG works for rendering only; SVG must be written by hand as text. |
| **No photographic / illustrative content** | No stock photography, no hand-drawn art, no 3D renders. Only flat vector-style geometry. |
| **No font beyond what is present** | Only DejaVu (system) and the Cairo font fetched here. Cairo has no Latin-only display weights; heavier weights require the variable axis (Pillow renders the default instance). |
| **No perceptual colour management across tools** | `littlecms2` is available inside Pillow, but there is no ICC workflow between tools. |
| **No device/emulator screenshot testing** | Assets are verified by pixel assertions, not by launching an emulator. A launcher's real masking behaviour is asserted geometrically, not observed. |

> **Do not claim generative capability.** If a future task needs photorealistic
> or illustrative imagery, that is a gap to be raised with the owner, not
> something this pipeline can fake.

### Environment probe (recorded 2026-10-27)

```text
CPU      AMD EPYC 9B14, 4 cores visible (cgroup cpu.max = max 100000)
RAM      15 GiB total, 13 GiB available, no swap
Disk     25 GiB on /workspace, 1% used
GPU      none (/dev/nvidia* absent)
User     uid=10001(openhands), groups include sudo
Pillow   13.0.0  (zlib, webp, freetype2, raqm, libtiff, littlecms2, avif, jpg_2000)
CairoSVG 2.9.1 (libcairo 2 present)
Fonts    DejaVu family (system) + Cairo variable (fetched, OFL)
Absent   ImageMagick, Inkscape, rsvg-convert, pngquant, optipng, cwebp, GPU
```

---

## 2. Design tokens

Single source of truth for the shipped app is `lib/core/theme.dart`
(`AyzrColors`), owned by Agent B. `tools/ayzr_palette.py` mirrors those exact
values so bitmaps and the running UI cannot drift.

| Token | Hex | Role |
|---|---|---|
| `navyDeep` | `#070B18` | scaffold background, icon plate, splash top |
| `navy` | `#0C1428` | surface / cards |
| `navyRaised` | `#131E3A` | raised sheen, lower band |
| `navyBorder` | `#22304F` | borders, grid lines, ring |
| `gold` | `#D4AF37` | primary accent, the mark |
| `goldSoft` | `#E6C766` | secondary accent, inner ring |
| `textPrimary` | `#EDF0F7` | body text |
| `textMuted` | `#97A0B5` | secondary text |

Cross-check against `docs/brand/ASTRA_VISUAL_IDENTITY_REFERENCE.jpg`
(median-cut quantised): dominant dark `#0A1823`, dominant light `#E1E7E7`,
mid gray `#BDC4CA`. These corroborate the navy/neutral family but are **not**
used for shipped surfaces — the Flutter theme is authoritative. No exact brand
hex codes are claimed from the reference image.

### The mark

A faceted gem ("brilliant" silhouette) with a table line and facet edges, inside
concentric precision arcs. It is abstract geometry — deliberately **not** a
candlestick or a price chart.

**Why no candlesticks.** A candlestick motif on an icon or background would
imply a specific price series. AYZR is shadow-only and must never appear to
assert market data. All decorative motifs (arcs, grids, bars, waveforms) are
abstract shapes that carry no numeric meaning. This is a deliberate design
constraint, not an aesthetic omission.

---

## 3. Files produced

81 files, 1 456 855 bytes (1422.7 KiB) total: 71 asset files (1 343.6 KiB)
plus 10 pipeline scripts under `tools/` (79.1 KiB). Full inventory:

### Launcher icons — legacy (48dp ladder)

| Density | Size | File | Bytes |
|---|---|---|---|
| mdpi | 48×48 | `android-res/mipmap-mdpi/ic_launcher.png` | 3284 |
| hdpi | 72×72 | `android-res/mipmap-hdpi/ic_launcher.png` | 5736 |
| xhdpi | 96×96 | `android-res/mipmap-xhdpi/ic_launcher.png` | 8526 |
| xxhdpi | 144×144 | `android-res/mipmap-xxhdpi/ic_launcher.png` | 14442 |
| xxxhdpi | 192×192 | `android-res/mipmap-xxxhdpi/ic_launcher.png` | 20286 |

RGBA, rounded-square silhouette, transparent corners.

### Adaptive icon layers (108dp)

Per density, three layers: `ic_launcher_foreground.png` (transparent, mark
inside the 72dp safe zone), `ic_launcher_background.png` (fully opaque navy
gradient), `ic_launcher_monochrome.png` (white silhouette for themed icons).

| Density | Size | fg | bg | mono |
|---|---|---|---|---|
| mdpi | 108×108 | 6018 | 1744 | 2314 |
| hdpi | 162×162 | 10123 | 2744 | 3471 |
| xhdpi | 216×216 | 14867 | 2819 | 4984 |
| xxhdpi | 324×324 | 24049 | 3703 | 7432 |
| xxxhdpi | 432×432 | 34468 | 4576 | 11310 |

### Adaptive icon XML

- `android-res/mipmap-anydpi-v26/ic_launcher.xml`
- `android-res/mipmap-anydpi-v26/ic_launcher_round.xml`
- `android-res/values/ic_launcher_background.xml` (`#070B18`)
- `android-res/values/colors.xml` (`ayzr_gold`, `ayzr_navy`)

### Play Store & in-app marks

| File | Size | Notes |
|---|---|---|
| `icons/play_store_icon_512.png` | 512×512, 55336 B | full-bleed square, **no** alpha (Play requirement) |
| `icons/brand_mark_256.png` | 256×256, 27711 B | gold mark, transparent |
| `icons/brand_mark_96.png` | 96×96, 7786 B | gold mark, transparent |

### Splash / launch

| File | Size | Bytes |
|---|---|---|
| `images/splash_1080x1920.png` | 1080×1920 | 73898 |
| `images/splash_1440x2560.png` | 1440×2560 | 105200 |
| `images/splash_tablet_1200x1920.png` | 1200×1920 | 74335 |
| `android-res/drawable/ayzr_splash_mark.png` | 288×288 | 32883 |
| `android-res/drawable-xxhdpi/ayzr_splash_mark.png` | 432×432 | 54014 |
| `android-res/drawable-xxxhdpi/ayzr_splash_mark.png` | 576×576 | 76547 |
| `android-res/drawable/launch_background.xml` | — | 389 |
| `android-res/drawable-v21/launch_background.xml` | — | 356 |

The splash gradient's top stop is exactly `#070B18`, matching the
`launch_background` window background, so there is no visible seam when the
window background hands over to the Flutter first frame.

### App background

| File | Size | Bytes |
|---|---|---|
| `images/app_background_1080x1920.png` | 1080×1920 | 16169 |
| `images/app_background_1080x1920.webp` | 1080×1920 | 12592 |

WEBP is provided as the smaller runtime option (22% smaller here).

### Notification small icons (24dp, white + alpha only)

`android-res/drawable-{mdpi,hdpi,xhdpi,xxhdpi,xxxhdpi}/ic_stat_ayzr.png` —
24/36/48/72/96 px, 598/929/1320/1840/2801 B. Android tints these glyphs, so
every opaque pixel is asserted to be pure white.

### In-app vector glyphs

`graphics/png/<name>_{24,48,96}.png` for
`ic_analysis`, `ic_news`, `ic_history`, `ic_market`, `ic_gem`, `ic_alert`
(18 files, 536–6209 B). `ic_history` reuses the "document" glyph, consistent
with the app's السجل (History) screen.

### SVG sources — the vector originals

`graphics/svg/` (9 files, 327–1726 B):

| File | Bytes |
|---|---|
| `ayzr_mark.svg` | 1066 |
| `splash_background.svg` | 1726 |
| `app_background.svg` | 716 |
| `ic_analysis.svg` | 340 |
| `ic_news.svg` | 327 |
| `ic_history.svg` | 423 |
| `ic_market.svg` | 369 |
| `ic_gem.svg` | 372 |
| `ic_alert.svg` | 371 |

Hand-authored SVG text with explicit `viewBox`. Not raster traces. The verifier
rejects any SVG containing `base64` or an `<image>` element, so a vector file
cannot silently degrade into a wrapped bitmap.

### Font

- `fonts/Cairo-Variable.ttf` (599548 B) — Google Fonts Cairo, variable
  `slnt,wght`, fetched from the official `google/fonts` repository.
- `fonts/Cairo-OFL.txt` (4473 B) — SIL Open Font License 1.1. Required to ship
  the font; the verifier asserts it is present.

Cairo is an Arabic-first typeface. Pillow's `raqm` layout engine performs the
Arabic contextual shaping and RTL ordering, verified working.

---

## 4. The pipeline

```text
ayzr-android/assets/tools/
├── ayzr_palette.py        design tokens (mirror of lib/core/theme.dart)
├── ayzr_draw.py           deterministic drawing primitives
├── generate_assets.py     the generator: all rasters, SVG text, Android XML
├── verify_assets.py       361 assertions over every produced file
├── test_verifier.py       mutation-tests verify_assets.py (11 mutations)
├── test_svg_parity.py     SVG route vs Pillow route, structural comparison
├── validate_android_res.py runs the real aapt2 compiler over the installed res
├── svg_render.py          CairoSVG rasteriser (independent second route)
├── install_android_res.py hand-off of android-res/** to Agent B (dry-run default)
└── run_all.sh             generate + verify + self-test + parity + aapt2
```

Run everything:

```bash
cd ayzr-android/assets/tools
./run_all.sh
```

Hand assets to the Flutter module (Agent B's path):

```bash
python3 install_android_res.py            # dry run: shows the plan
python3 install_android_res.py --apply    # copies new files only
```

The installer **never overwrites silently**. Files that differ from the
generated version are listed as conflicts and skipped unless `--overwrite` is
passed, because some of them are Agent B's Flutter template defaults.

### Hand-off status (observed 2026-10-27)

Agent B has already run the installer. All **34** `android-res` files are
present under `android/app/src/main/res/` and **byte-identical** to the
generated originals (0 differing, 0 missing). Agent B's
`values/colors.xml` defines `ayzr_gold`/`ayzr_navy` and its
`launch_background.xml` consumes `@color/ic_launcher_background` and
`@drawable/ayzr_splash_mark`, so the two sides agree. All 12 resource
references in the installed tree resolve.

---

## 5. Verification

Four independent suites, all green.

| Suite | Result |
|---|---|
| `verify_assets.py` | **361 checks, 361 passed, 0 failed** |
| `test_verifier.py` (mutation testing) | **11 mutations, 11 caught, 0 missed** |
| `test_svg_parity.py` | **36 checks, 36 passed, 0 failed** |
| `validate_android_res.py` (real aapt2) | **36 resources compiled, 0 errors; linked; 8 resource groups confirmed** |

`verify_assets.py` asserts, per file: PNG/format correctness, exact pixel
dimensions against the density ladder, alpha presence *and* that transparency
is genuinely used, opacity where required, file-size budget, adaptive safe-zone
radius, notification-glyph whiteness, gold hue presence, dark-navy mean
luminance, and mark centring. It also parses the Android XML resources and
checks every referenced drawable exists.

`test_verifier.py` is the part that makes the first suite trustworthy. A
verifier that always passes is worthless, so it mutates a scratch copy of the
asset tree in eleven genuinely-wrong ways and asserts the verifier fails each
time. The mutations include the two real defects found during development:

- `mut_icon_desaturated_to_gray` — gold bloomed into gray
- `mut_foreground_bleeds_outside_safe_zone` — adaptive ink outside the 72dp zone

`validate_android_res.py` is the strongest evidence, because it uses Android's
own resource compiler rather than any assertion this agent wrote. It runs
`aapt2 compile` over the installed `res/` tree (36 resources, exit 0), then
`aapt2 link` with a synthetic manifest (exit 0, 360 563-byte APK), then
`aapt2 dump resources` to confirm the density configurations that actually
landed:

```text
mipmap/ic_launcher              6 configs (mdpi..xxxhdpi + anydpi-v26)
mipmap/ic_launcher_foreground   5 configs
mipmap/ic_launcher_monochrome   5 configs
mipmap/ic_launcher_background   5 configs
drawable/ic_stat_ayzr           5 configs
drawable/ayzr_splash_mark       2 configs
color/ayzr_gold                 1 config
color/ic_launcher_background    1 config
```

Because Gradle uses the same tool, a resource that survives this cannot fail
at Android build time for a resource reason.

---

## 6. Defects found and fixed during development

Recorded because they were real bugs in generated output, not hypotheticals.

1. **Pillow `ImageDraw` replaces alpha; it does not blend.** Drawing a
   semi-transparent grid line or rule straight onto an opaque plate overwrote
   the plate's alpha, leaving the launcher icon 60% transparent. Fixed by
   drawing every translucent element on its own transparent layer and
   compositing with `Image.alpha_composite`. Documented in `plate()`.

2. **A `screen`-blend bloom desaturates gold into gray.** Screening a gold
   stroke (high R/G, low B) with its own blur lifts blue far more than red, so
   the mark rendered gray. Fixed by using blurred luminance as an alpha mask on
   a flat gold layer composited *behind* the source. Now guarded by
   `check_gold_and_navy`.

3. **Adaptive foreground overflowed the 72dp safe zone.** The mark plus bloom
   reached r=196px on a 432px canvas against a 144px safe radius, so launchers
   that mask aggressively would clip it. Fixed by tightening the geometry to
   r=126px (17.9px margin) and adding `check_adaptive_safe_zone`, which
   measures the true maximum ink radius rather than the bounding-box corner
   (the corner metric reported false failures for a ring-shaped mark).

4. **A wide low-alpha halo over dark navy reads as gray, not gold.** Sampled
   pixel (86,77,44) — desaturated. Fixed by making the halo smaller than the
   mark so it glows through the translucent facets and dies before reaching the
   plate.

---

## 7. Constraints honoured

- **No invented data.** No price, candle, news, indicator, level or probability
  value appears in any asset. The motif set is deliberately abstract; no
  candlesticks, no numbers, no axis labels.
- **No hardcoded secrets.** Nothing here authenticates to anything.
- **Out of scope respected.** `astra-frontend/` was never read or modified. The
  Flutter app's `android/app/src/main/res/**` and `pubspec.yaml` are Agent B's
  paths; this agent stages into `assets/**` and provides an installer rather
  than editing them directly.
- **No deployment.** No push, no release, no store upload.
- **Font licensing.** Cairo ships under the SIL OFL 1.1; the licence text is
  included next to the font and asserted present by the verifier.
