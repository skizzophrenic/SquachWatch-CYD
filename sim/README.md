# SquachWatch-CYD PC emulator

Runs the firmware's **real** UI code natively on a PC, so layout work and
navigation bugs don't need a build -> flash -> squint-at-the-device cycle.

Two binaries, for two different questions:

| | question it answers | how |
| --- | --- | --- |
| `squachsim` | *does this screen look right?* | calls one screen's `Tick()` directly and writes a PNG |
| `squachsim-live` | *does the UI actually work?* | runs the firmware's own `setup()`/`loop()` and takes simulated taps |

```
make -j8
./squachsim clear out.png
./squachsim alert out.png --portrait
```

The screens you see are drawn by the actual `theme.cpp`, `squachy.cpp`
and `ui_*.cpp` from `src/` -- not a reimplementation -- so what renders
here is what the device draws, and it can't drift out of sync with the
firmware. Nothing is vendored: point `SQUACHWATCH=` at any checkout and
it renders whatever that branch currently has.

## Requirements

`g++` and `make`. No SDL, no zlib, no other libraries -- PNGs are written
directly (uncompressed, so files are larger than a real encoder would
produce, which is irrelevant for debug screenshots).

Objects go under `build/` and only what an edit touches is recompiled,
so the second build is fast; `-j` with your core count makes the first
one fast too.

### Windows

Use the wrappers -- they build and run everything inside WSL for you:

```
squachgui                                # interactive GUI (start here)
squachsim clear                          # -> out\clear.png
squachsim alert out\alert.png --portrait
squachsim clear out\fire.png --bg 6
squachsim --list                         # screens and options
```

Or drive WSL directly if you prefer:

```
wsl -e bash -lc 'cd /mnt/<drive>/path/to/SquachWatch-Sim && make && ./squachsim clear out/clear.png'
```

## GUI

```
squachgui
```

Builds, starts a local server and opens the browser. Ctrl-C in the
console window stops it. Set `SQUACHSIM_PORT` to move it off 842.

It's a browser GUI rather than a native window for a specific reason:
Windows 10 has no WSLg, so an SDL window would need an X server or a
native Windows toolchain installed. A page the Windows browser opens
needs neither.

**Live** tab: a running device. Click the screen to tap it -- press,
drag and release go through the firmware's own touch mapping, so
long-presses (holding CLR for the outfit unlock), drag-scrolling and
edge-zone gestures behave the way they do on hardware. The firmware's
`AppState` is shown under the screen and its `Serial` output streams into
the panel beside it. *Trigger* posts a synthetic detection of the chosen
type (see below). *Reboot* restarts the process; *Factory reset* also
wipes the emulated NVS, which brings back the colour check and the
first-boot walkthrough.

**Gallery** tab: the one-shot renderer. Pick a screen, background, theme
or orientation and it re-renders immediately; animations play back at
the speed the device runs them, and *Save PNG* grabs the current frame.
Worth keeping around even with Live working, because it reaches screens
directly instead of by navigating to them, and lets you set a background
or palette without walking through Settings.

Frames cross the wire as raw RGB888 and go into a canvas via `ImageData`
-- no PNG encode on one side, no decode on the other.

## Interactive emulator

```
make live
./squachsim-live
```

`main_live.cpp` compiles the firmware's actual `src/main.cpp` and calls
its real `setup()` and `loop()`. Every screen transition, gesture
threshold, debounce timer and long-press rule is the firmware's own --
nothing about navigation is reimplemented, so it can't drift.

Line commands on stdin, raw frames on stdout (the firmware's `Serial`
goes to stderr, so it can never corrupt the frame stream):

| command | effect |
| --- | --- |
| `D <x> <y>` | press down at screen coordinates, and hold |
| `M <x> <y>` | move while held |
| `U` | release |
| `S [n]` | step `n` `loop()` iterations (default 1), then emit a frame |
| `T <type> [rssi]` | inject a synthetic detection (`T 6` or `T AIRTAG`) |
| `R` | report the current `AppState` on stderr |
| `C` | `PUSHES <n>` on stdout: how many frames have been pushed so far -- the panel's push count on the board |
| `L [view]` | open the LORA screen on `LoraView` N, as `enterLora()` would. The board's doors to it are CrowPanel-only and this binary is the CYD build |
| `Q` | quit |

Each frame is `FRM <w> <h> <bytes> <state>\n` followed by `<bytes>` of
row-major RGB888. The state rides in the header so a caller never has to
correlate a stdout frame against a stderr reply.

Taps are injected in **raw driver space**, not screen space:
`screenToRaw()` inverts the arithmetic in `pollTouch()` so the firmware's
own mapping (rotation, axis swap, clamping) is what runs. Arduino's
integer `map()` is exactly invertible, which is what makes that clean.
The upside is that a rotation bug in `pollTouch()` shows up here as taps
landing in the wrong place -- a loud failure, not a silent one.

Time is virtual: `millis()` advances 33ms per step and `delay()` advances
it rather than sleeping, so a recorded sequence of taps replays
identically every time and the 4-second boot sequence passes in
microseconds. (That `delay()` behaviour isn't a convenience -- `setup()`'s
1.2s "hold to reset calibration" window is a busy-wait on `millis()`, and
without it the emulator hangs there forever.)

### Persistent settings

Set `SQUACHSIM_NVS=<dir>` and the `Preferences` shim persists to disk,
one file per namespace, instead of living in memory. The GUI does this
automatically (`.nvs/` beside the binary), so themes, pet counts and
"has seen the walkthrough" survive a restart exactly like they survive a
power cycle on hardware -- and "change a setting, reboot, check it
stuck" becomes a thing you can test. Deleting the directory is a factory
reset.

The one-shot renderer deliberately leaves this off: a fresh render should
start from `settings.cpp`'s own defaults.

## Usage

```
./squachsim <screen> [out.png] [options]
```

Screens: `clear log alert settings diary hunt rawscan watchalert
colorcheck boot lora`

| Option | Effect |
| --- | --- |
| `--portrait` | render 240x320 instead of 320x240 |
| `--size WxH` | render at another panel size, e.g. `400x240` for the CrowPanel |
| `--pitch N` | one pixel of glass in micrometres (2.8" 178, 2.4" 152, CrowPanel 381): the button bar is sized from it, as on the board (`SQW_PIXEL_PITCH_UM` in each `include/*_user_setup.h`). Defaults to the board with that `--size`; the 2.4" shares the 2.8"'s 240x320 and needs it said |
| `--bg N` | background style 0..9 (see `Settings::Background`) |
| `--theme N` | palette index |
| `--frames N` | animation warm-up frames before capture (default 90) |
| `--onboard` | let Squachy's first-boot walkthrough run |
| `--sequence N` | capture N consecutive frames instead of one |
| `--raw PATH` | write raw RGB888 frames instead of PNGs (what the GUI consumes) |
| `--tap F:X:Y` | tap at x,y on warm-up frame F -- the background on most screens, the LORA screen itself on that one |
| `--loraview N` | lora screen: open on `LoraView` N (0 LIST .. 9 PICK) |
| `--rawwifi` | rawscan screen: the WiFi side, with an SSID fixture behind it |

### The LORA screen, and the radio that is not there

`lora` is the CrowPanel 7's sniffer: ten views behind a three-slot button
bar, on a 400x240 canvas (`./squachsim lora out.png --size 400x240`).
There is no SX1262 here, so `sim/lora_sim.cpp` stands one up -- a dozen
fabricated frames through the firmware's own classifier, node table,
message store and survey. It says SIM on everything it invents; read that
file's header before treating anything it renders as a measurement.

`--tap` on this screen is a press on the screen, delivered through
`uiLoraTap` at the position the finger went down at, the way
`src/main.cpp`'s `AppState::LORA` case delivers one. It prints the view
the press left showing on stderr, which is what `test_lora_bar.sh`
asserts on -- that script is the regression pin for a bar whose buttons
had two millimetres of "opens a frame" above and below them.

### The raw scanner, which had no results in it either

`rawscan` answered "done, nothing found" on both halves, so the list has
never been rendered off the board with a device in it -- and it is one of
the two screens that prints a name at size 2 beside a right-aligned RSSI
on the same line, which is where a long name goes under the number. The
BLE side now gets five fabricated devices and `--rawwifi` renders the
WiFi side against `sim/detection_sim.cpp`'s SSID fixture. Both sets are
deliberately long and share a prefix, because that is the case a cut name
loses, and every one of them says SIM.

### The bottom button bar, through the real state machine

`test_button_bar.sh` drives `squachsim-live` -- the target that compiles
the firmware's own `main.cpp` -- and asserts on the `AppState` in each
frame header rather than on a hit test called in isolation. It holds two
things: that the bar owns every row from its top edge to the bottom of
the glass (the five rows under the drawn buttons used to belong to
nobody, and on the main screen to the background-cycling edge zone), and
that `[ CLR ]` asks before it erases the log. It primes the NVS once,
answers the first-boot colour check, and gives every case its own copy of
the store, because the firmware persists what a case did.

`make shots` renders one PNG per screen into `out/`.

### Why the warm-up frames matter

Matrix rain, the starfield, the aquarium, Squachy's idle animation --
they all build state across frames. A single tick renders a half-empty
scene that looks nothing like the device. The harness ticks with
advancing time and captures the last frame; bump `--frames` if a slower
effect hasn't settled.

## How it works

The real TFT_eSPI library makes exactly six methods `virtual` --
`drawPixel`, `drawChar`, `readPixel`, `setWindow`, `pushColor` and the
`begin/end_nin_write` pair -- specifically so `TFT_eSprite` can override
those and inherit every higher-level shape and text function from the
base class. `TFT_eSPI.h` here follows the same split: implement the six
against an in-memory RGB565 buffer, and the shape layer built on top of
them comes along for free.

Text uses the real Adafruit GLCD 5x7 table (`glcdfont_data.h`, copied
verbatim from the vendored TFT_eSPI package), so labels and counters are
actually readable rather than placeholder boxes.

`Arduino.h`, `Preferences.h`, `SPI.h`, `Wire.h` and
`XPT2046_Touchscreen.h` here are small shims for the same reason: they
make those includes resolve to something that exists on a PC. The sim
directory goes first on the include path, which is the whole mechanism.
`Wire`'s `endTransmission()` returns a NACK so `CapTouch::begin()` fails
and `pollTouch()` falls through to the resistive XPT2046 path -- which is
what the real board does, and its boot log says so.

## What this is *not*

**No detection engine.** `detection_sim.cpp` replaces `src/detection.cpp`
and `src/sd_log.cpp`, which are ~900 lines wired straight into `WiFi.h`,
`esp_wifi.h`, `NimBLEDevice.h`, `esp_bt.h` and `SD.h`. Stubbing that
surface faithfully is a large job on its own and none of it affects how
the UI renders. So the class is the same class from the same header --
every `ui_*.cpp` still takes the real `const DetectionEngine&`,
unchanged -- but nothing is scanning. What the screens read back is
whatever was put there: `seedDetections()` in `main_sim.cpp` for the
gallery, or the `T` command for the live emulator, which starts empty.

`sim_detections.h` builds those synthetic sightings from the vendor
strings and OUIs in `src/signatures.cpp`, so a triggered AirTag reads on
LOG and ALERT exactly the way a real one does, and repeated triggers of
one type come out as separate units of the same make rather than one
device seen twice.

The practical consequence: triggering exercises everything *downstream*
of a detection -- the ALERT screen and its MORE INFO panel, LOG rows and
counters, Squachy's reaction, the ALL CLEAR/expiry transitions. It does
**not** exercise the logic that decides what counts as a detection.
Signature-matching changes still need hardware.

One specific gap worth knowing: the stub's `postBle()` is a bare
`pushLog()` with no `Settings::typeEnabled()` guard, so the **TYPE FILTER
screen has no effect on triggered detections**. That's deliberate --
filtering is a property of the real engine, and mirroring it into the
stub would be testing the mirror rather than the firmware.

**Not pixel-exact -- and this matters most where it looks most useful.**
The six virtual primitives are faithful, but the shape layer built on
them is *this* implementation, not upstream TFT_eSPI's. `drawWideLine`
is the sharpest example: it draws Squachy's arms, and here it's a
triangle-plus-circles approximation where upstream anti-aliases. So a
render that looks right is **not** proof the device looks right.
`fillRoundRect` corners, `fillEllipse` and `drawArc` (a filled wedge
here) differ too, and sprite colour depth is tracked but everything
composites as RGB565 regardless of `setColorDepth()`.

Trust this for layout, spacing, wrapping and legibility. Do not trust it
for pixel-level shape questions -- check those on hardware.

**Touch is exact where it counts, and absent where it doesn't.** The live
emulator runs the firmware's real touch mapping and gesture handling, so
hit regions, debounce and long-presses are genuinely under test. What it
can't tell you is anything about the *panel*: no calibration drift, no
noisy or dead spots, no pressure threshold, no two-finger anything.

**Blind to whole classes of real bugs.** No colour order, no panel
inversion, no real heap ceiling, no SPI bandwidth. A screen can render
beautifully here and run at 3fps on the device.
