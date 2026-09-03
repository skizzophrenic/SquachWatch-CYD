# SquachWatch-CYD PC emulator

Renders the firmware's **real** UI code natively to a PNG, so layout and
sizing work doesn't need a build â†’ flash â†’ squint-at-the-device cycle.

```
cd tools/sim
make
./squachsim clear out.png
./squachsim alert out.png --portrait
```

The screens you see are drawn by the actual `theme.cpp`, `squachy.cpp`
and `ui_*.cpp` from `src/` â€” not a reimplementation â€” so what renders
here is what the device draws, and it can't drift out of sync with the
firmware.

## Requirements

`g++` and `make`. No SDL, no zlib, no other libraries â€” PNGs are written
directly (uncompressed, so files are larger than a real encoder would
produce, which is irrelevant for debug screenshots).

### Windows

Use the wrapper -- it builds and runs everything inside WSL for you:

```
squachsim clear                          # -> out\clear.png
squachsim alert out\alert.png --portrait
squachsim clear out\fire.png --bg 6
squachsim --list                         # screens and options
```

Given just a screen name it picks the output path for you. Or drive WSL
directly if you prefer:

```
wsl -e bash -lc 'cd /mnt/<drive>/path/to/SquachWatch-Sim && make && ./squachsim clear out/clear.png'
```

## GUI

```
squachgui
```

Builds, starts a local server and opens the browser. Pick a screen,
background, theme or orientation from the panel and it re-renders
immediately; animations play back at the speed the device runs them.
"Save PNG" grabs the current frame. Ctrl-C in the console window stops
it. Set `SQUACHSIM_PORT` to move it off 842.

It's a browser GUI rather than a native window for a specific reason:
Windows 10 has no WSLg, so an SDL window would need an X server or a
native Windows toolchain installed. A page the Windows browser opens
needs neither, and gets side-by-side comparison and instant orientation
toggling for free.

Frames cross the wire as raw RGB888 straight from the emulator's `--raw`
mode and go into a canvas via `ImageData` -- no PNG encode on one side,
no decode on the other. The warm-up is the expensive part (~2ms/frame)
and it's paid once per render, so 45 animation frames cost barely more
than one.

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
| `--sequence N` | capture N consecutive frames instead of one |
| `--raw PATH` | write raw RGB888 frames instead of PNGs (what the GUI consumes) |

`make shots` renders one PNG per screen into `out/`.

### Why the warm-up frames matter

Matrix rain, the starfield, the aquarium, Squachy's idle animation â€”
they all build state across frames. A single tick renders a half-empty
scene that looks nothing like the device. The harness ticks with
advancing time and captures the last frame; bump `--frames` if a slower
effect hasn't settled.

## How it works

The real TFT_eSPI library makes exactly six methods `virtual` â€”
`drawPixel`, `drawChar`, `readPixel`, `setWindow`, `pushColor` and the
`begin/end_nin_write` pair â€” specifically so `TFT_eSprite` can override
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
the UI renders. So the class is the same class from the same header â€”
every `ui_*.cpp` still takes the real `const DetectionEngine&`,
unchanged â€” but nothing is scanning. What the screens read back is
whatever `seedDetections()` in `main_sim.cpp` put there.

The practical consequence: this shows you a LOG screen full of
detections, but it is **not** exercising the logic that decides what
counts as a detection. Signature-matching changes still need hardware.

**No touch.** Touch methods are inert stubs, so this renders screens; it
doesn't drive them. Modal panels and button states are reachable by
passing the relevant flags in `main_sim.cpp`'s `tick()` lambda.

**Not pixel-exact — and this matters most where it looks most useful.**
The six virtual primitives are faithful, but the shape layer built on
them is *this* implementation, not upstream TFT_eSPI's. `drawWideLine`
is the sharpest example: it draws Squachy's arms, and here it's a
triangle-plus-circles approximation where upstream anti-aliases. So a
render that looks right is **not** proof the device looks right.
`fillRoundRect` corners, `fillEllipse` and `drawArc` (a filled wedge
here) differ too, and sprite colour depth is tracked but everything
composites as RGB565 regardless of `setColorDepth()`.

Trust this for layout, spacing, wrapping and legibility. Do not trust it
for pixel-level shape questions — check those on hardware.

**Blind to whole classes of real bugs.** No colour order, no panel
inversion, no touch, no real heap ceiling, no SPI bandwidth. A screen can
render beautifully here and run at 3fps on the device.
