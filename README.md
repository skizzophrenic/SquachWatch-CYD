# SquachWatch-CYD PC emulator

Renders the firmware's **real** UI code natively to a PNG, so layout and
sizing work doesn't need a build → flash → squint-at-the-device cycle.

```
cd tools/sim
make
./squachsim clear out.png
./squachsim alert out.png --portrait
```

The screens you see are drawn by the actual `theme.cpp`, `squachy.cpp`
and `ui_*.cpp` from `src/` — not a reimplementation — so what renders
here is what the device draws, and it can't drift out of sync with the
firmware.

## Requirements

`g++` and `make`. No SDL, no zlib, no other libraries — PNGs are written
directly (uncompressed, so files are larger than a real encoder would
produce, which is irrelevant for debug screenshots).

On Windows, build it inside WSL:

```
wsl -e bash -lc 'cd /mnt/<drive>/path/to/SquachWatch-Sim && make'
```

## Usage

```
./squachsim <screen> [out.png] [options]
```

Screens: `clear log alert settings diary hunt rawscan watchalert
colorcheck boot`

| Option | Effect |
| --- | --- |
| `--portrait` | render 240x320 instead of 320x240 |
| `--bg N` | background style 0..9 (see `Settings::Background`) |
| `--theme N` | palette index |
| `--frames N` | animation warm-up frames before capture (default 90) |
| `--onboard` | let Squachy's first-boot walkthrough run |

`make shots` renders one PNG per screen into `out/`.

### Why the warm-up frames matter

Matrix rain, the starfield, the aquarium, Squachy's idle animation —
they all build state across frames. A single tick renders a half-empty
scene that looks nothing like the device. The harness ticks with
advancing time and captures the last frame; bump `--frames` if a slower
effect hasn't settled.

## How it works

The real TFT_eSPI library makes exactly six methods `virtual` —
`drawPixel`, `drawChar`, `readPixel`, `setWindow`, `pushColor` and the
`begin/end_nin_write` pair — specifically so `TFT_eSprite` can override
those and inherit every higher-level shape and text function from the
base class. `TFT_eSPI.h` here follows the same split: implement the six
against an in-memory RGB565 buffer, and the shape layer built on top of
them comes along for free.

Text uses the real Adafruit GLCD 5x7 table (`glcdfont_data.h`, copied
verbatim from the vendored TFT_eSPI package), so labels and counters are
actually readable rather than placeholder boxes.

`Arduino.h` and `Preferences.h` here are small shims for the same
reason: they make `<Arduino.h>` and `<Preferences.h>` resolve to
something that exists on a PC. The sim directory goes first on the
include path, which is the whole mechanism.

## What this is *not*

**No detection engine.** `detection_sim.cpp` replaces `src/detection.cpp`
and `src/sd_log.cpp`, which are ~900 lines wired straight into `WiFi.h`,
`esp_wifi.h`, `NimBLEDevice.h`, `esp_bt.h` and `SD.h`. Stubbing that
surface faithfully is a large job on its own and none of it affects how
the UI renders. So the class is the same class from the same header —
every `ui_*.cpp` still takes the real `const DetectionEngine&`,
unchanged — but nothing is scanning. What the screens read back is
whatever `seedDetections()` in `main_sim.cpp` put there.

The practical consequence: this shows you a LOG screen full of
detections, but it is **not** exercising the logic that decides what
counts as a detection. Signature-matching changes still need hardware.

**No touch.** Touch methods are inert stubs, so this renders screens; it
doesn't drive them. Modal panels and button states are reachable by
passing the relevant flags in `main_sim.cpp`'s `tick()` lambda.

**Not pixel-exact � and this matters most where it looks most useful.**
The six virtual primitives are faithful, but the shape layer built on
them is *this* implementation, not upstream TFT_eSPI's. `drawWideLine`
is the sharpest example: it draws Squachy's arms, and here it's a
triangle-plus-circles approximation where upstream anti-aliases. So a
render that looks right is **not** proof the device looks right.
`fillRoundRect` corners, `fillEllipse` and `drawArc` (a filled wedge
here) differ too, and sprite colour depth is tracked but everything
composites as RGB565 regardless of `setColorDepth()`.

Trust this for layout, spacing, wrapping and legibility. Do not trust it
for pixel-level shape questions � check those on hardware.

**Blind to whole classes of real bugs.** No colour order, no panel
inversion, no touch, no real heap ceiling, no SPI bandwidth. A screen can
render beautifully here and run at 3fps on the device.
