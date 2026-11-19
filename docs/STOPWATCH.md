# M5Stack StopWatch (C152)

This is a source-build bring-up port, not a hardware-verified release. Build
environment: `stopwatch`; board macro: `M5_STOPWATCH`. It is separate from the
LilyGo T-Watch and the M5Stack StickS3. No release manifest is published.

## Sources and hardware

Researched on 2026-11-18 against M5Stack's primary sources:

- [StopWatch hardware, pin map, schematic and download-mode instructions](https://docs.m5stack.com/en/core/StopWatch).
- [M5GFX 0.2.26](https://github.com/m5stack/M5GFX/tree/0.2.26), especially
  `src/M5GFX.cpp` (`Panel_StopWatch`, board detection, power/reset sequence and QSPI wiring).
- [M5Unified 0.2.19](https://github.com/m5stack/M5Unified/tree/0.2.19), especially
  `src/M5Unified.cpp`, `src/utility/Power_Class.cpp` and its M5PM1/M5IOE1 classes.

| Component | Hardware / connections |
|---|---|
| MCU and memory | ESP32-S3R8, 240 MHz, 16 MB flash, 8 MB OPI PSRAM |
| Radio | 2.4 GHz WiFi and Bluetooth LE; no 5 GHz, onboard GPS or LoRa |
| Display | 1.75-inch round 466×466 AMOLED, CO5300 QSPI |
| Display pins | CS 39, CLK 40, D0 41, D1 42, D2 46, D3 45, TE 38 |
| System I2C | SDA 47, SCL 48 |
| Touch | CST820B, interrupt 13; M5GFX owns the driver |
| I/O expander | M5IOE1 at 0x4F; PYG5 OLED reset, PYG4 touch reset, PYG8 display supply |
| Buttons | Yellow A: GPIO2; blue B: GPIO1; separate hardware power button |
| Power | M5PM1; 450 mAh internal battery; PMG2 charge-status input |
| Haptic | Motor on M5IOE1 PYG9/PWM1 |
| Audio | ES8311 at 0x18; MCLK 18, BCLK 17, WS 15, speaker data 21, microphone data 16; PYG3 audio power, PYG10 amp enable |
| Sensors | BMI270 IMU at 0x68, RX8130CE RTC at 0x32 |
| Expansion | Grove GPIO10/11; no built-in microSD slot |

The driver libraries own the power sequence and panel initialization. In
particular, GPIO45 must never receive the ordinary S3 backlight PWM: it is
QSPI D3 on this board. Brightness is an AMOLED command. The port does not
change charging current or drive the rear expansion bus.

Hardware revisions differ at the rear connector: on v1.0, a pin mistakenly
labeled BAT on some units is actually 5V IN. Only v1.0.1 changes it to BAT.
Consult the official hardware page before connecting external power there.

## Port design

The existing Freenove 3.5-inch and Tab5 ports establish the relevant pattern:
keep TFT_eSPI's drawing API and sprite types, but delegate the physical panel
to a board-specific driver. StopWatch uses that pattern with M5GFX/M5Unified.
The Freenove adapter's text fallback is retained so boot messages and
allocation-failure drawing never touch TFT_eSPI's SPI registers.

Every page now renders directly at 466×466. Backgrounds fill the circular
face; there is no inscribed square viewport or pixel stretching. Menus use
rows sized to the available circle chord, scroll indicators follow the rim,
and headers and footers sit inside the glass. Keyboards reflow their actual
key rectangles, with matching touch targets. Alerts use a circular signal
gauge; squad, sensing, setup and detail pages have dedicated native layouts.

M5GFX 0.2.26 exposes 468×468 coordinates for the 466×466 glass. Drawing and
touch both remove the one-pixel driver padding. Rotation stays fixed at zero.
Touches outside the physical circle are rejected. The shared calibration
pipeline uses native pixel coordinates; manual calibration remains available.
Power saving defaults to dimming after 60 seconds; touch or either button wakes it.

The single 8-bit frame buffer uses 217,156 bytes of PSRAM. Backgrounds and
foreground content share this buffer. Hardware frame rate should be checked
with animation and radios active.

Frames convert through the project's RGB332 lookup into RGB565 rows and use
synchronous M5GFX QSPI writes. The existing optimized SPI-register/DMA push is
not used on this panel. There is no second owner of the display bus.

Implemented: display and text fallback, touch, brightness/dimming and charge
screen, WiFi/BLE detection through the shared S3 code, SquachMesh, native USB,
two-button navigation, battery/charging indicator, persistent settings, black-box
storage and the existing two-slot OTA layout. No SD mount is attempted.

### Two-button navigation

| Control | Action |
| --- | --- |
| Left / yellow A, short release | Back one level; cancel the current dialog |
| Left / yellow A, hold 500 ms | Home (subject to security/update guards) |
| Right / blue B, short release | Focus the next enabled item; wrap at the end |
| Right / blue B, hold 500 ms | Activate the focused item |
| Both buttons, short release | Open the main Settings menu after both are released |
| Both buttons, hold together 500 ms | Power off through M5Stack's power controller |

Focus has a cyan/white outline; selected rim controls fill purple. A small
ring beside the held button fills during the 500 ms hold. Long presses fire
once and never generate a short click on release. List navigation scrolls
additional rows into view. Focus is remembered separately for screens,
Settings subpages and dialogs, with a valid fallback when an item disappears.

Combined presses take priority over individual actions. Their hold timer starts
when both buttons are down, and both progress rings fill together. Releasing
one button ends the hold; the other button's eventual release cannot trigger
Back, Home, Next or Select. A long combined press fires power-off once and
never opens the menu. A button whose individual long action already fired
cannot join a new combined gesture until both buttons have been released.
The menu shortcut cannot bypass the PIN or interrupt an active update.
Power-off is also suppressed during firmware installation and calibration.

The first physical press while dimmed only wakes the screen. Its entire hold
and release are consumed. Touch remains available. Input is suppressed during
firmware installation and calibration; Home cannot bypass the lock or leave
an active update. The separate power button retains M5Stack's hardware
behavior (single click power-on/reset, double click off).

Bottom touch buttons are separated, round-ended arcs along the lower rim.
Their touch regions follow the arcs, including rounded ends; gaps do nothing.
The hamburger is a larger curved capsule at the top of the glass. Detection
counters follow concentric upper arcs. Phone keyboards reserve room above
the footer. These controls are compiled only for `M5_STOPWATCH`; other boards
retain their existing navigation and geometry.

Audio, microphone, IMU gestures, RTC synchronization and application haptic
patterns are not enabled in this initial port. M5Unified initializes the
power controller and keeps vibration off. Battery percentage is the library's
voltage-based estimate, not a measured fuel-gauge state of charge.

## Build and upload

Install PlatformIO, then from the repository root:

```sh
pio run -e stopwatch
pio run -e stopwatch -t upload --upload-port COMx
pio device monitor -e stopwatch --port COMx
```

Replace `COMx` with the actual device port (or the Linux/macOS serial path).
To enter download mode, connect a USB data cable, hold the power button until
the green LED blinks, then release it. No device is flashed automatically.

The environment uses Espressif32 6.12.0 / Arduino 2.0.17, matching M5Stack's
documented PlatformIO baseline without changing the other boards' pinned
platform. M5GFX and M5Unified are pinned to Git release tags because those
versions are not available in the PlatformIO registry. Their integrated
power drivers avoid a separate M5PM1/M5IOE1 dependency.

`partitions_twatch.csv` supplies two 4 MB app slots and the existing S3
black-box address. The build identity is `stopwatch`, so another device's
signed OTA image is not accepted. The public update service has no StopWatch
manifest; USB source builds are the supported installation route here.

## Validation before release

Host test `test/stopwatch_test.cpp` checks all native pixels for circle membership
and identity touch mapping, row bounds, and keyboard geometry and hit tests.
CI compiles `stopwatch` without adding it to release publishing.

Local validation on 2026-11-18: `pio run -e stopwatch` succeeded; all 14 host
checks passed, including the shared `TouchFit` pipeline. The initial image was
1,827,088 bytes; the linker reports 92,080 bytes static RAM and 1,826,725 bytes
program flash (43.6% of the 4 MB app slot). Runtime allocations are additional.
`git diff --check` passed. Other board firmwares were not rebuilt locally.
The TFT_eSPI `TOUCH_CS pin not defined` warning is expected: this port uses
M5GFX capacitive touch and intentionally supplies no resistive-touch CS pin.

On a physical C152, check:

1. Cold boot on USB and battery, with and without a serial monitor; verify
   PSRAM detection and the `[stopwatch]` initialization line.
2. Red/green/blue bars and boot text; labels along the curved edges; no QSPI corruption
   while changing brightness, charging, or saving settings.
3. Touch the full glass, keyboard keys, Settings scrolling and bezel rejection.
4. Both short releases and 500 ms holds; no duplicate action after a long
   release. Walk the complete Settings list, wrap, enter a submenu and return
   to its remembered row. Try keyboards and confirmations. Dim the screen,
   hold either button beyond 500 ms, then release: only wake should occur.
   Also check the hardware power button, calibration and charge mode.
5. Simultaneous WiFi/BLE scanning, alerts, SquachMesh and an extended soak;
   inspect heap, frame time, battery readings and crash records.
6. Persistence after reboot and a same-board update/rollback using a signed
   test build before enabling public OTA distribution.

A successful build cannot prove display colors, touch alignment, power
consumption, charge accuracy or runtime stability on real hardware.

## Native round-layout validation (2026-11-19)

The native layout replaces the earlier 320×320 content viewport. All 14
StopWatch geometry/touch checks passed, including exhaustive circle membership
and identity coordinate mapping. The existing rectangular-device QWERTY
regression suite also passed. Circular crops of desktop-rendered screens were
reviewed for layout, including menus, alerts, setup, keyboards, squad, sensing,
and detail pages. The Windows preview required a temporary host-only word-wrap
workaround; the firmware's existing word-wrap implementation is unchanged.

Physical visual validation and animation frame-rate checks remain necessary
on the connected device. Other device firmwares were not rebuilt locally.

Final native build: 1,827,712-byte firmware image, 92,336 bytes static RAM,
1,827,353 bytes program flash. Uploaded to the registered C152 on COM11;
esptool verified the flash hash and reset the device. Firmware SHA-256:
`3ffa520b22f022baae0a0e310404cc77ed79e7d1dbc6752835e54337a6b5ca04`.

## Two-button and curved-control checks (2026-11-19)

34 input/geometry assertions and 13 real-UI assertions passed. The existing
14 circular-display/keyboard checks and rectangular-board QWERTY suite also
passed. Host checks cover the 500 ms boundary, short-on-release behavior, long-press
one-shot/release suppression, whole-press wake consumption, startup-held and
update-blocked buttons, millisecond rollover, focus wrapping and per-context
focus memory. Arc hit regions are checked exhaustively for overlap and glass
bounds. The UI integration suite exercises the real Theme controls, modal
replacement, viewport offsets, Settings scrolling/return, font-slider targets,
and exclusion of a disabled password OK button.

Reproduce with a native C++ toolchain:

```sh
make -C test out/stopwatch_controls_test out/stopwatch_test out/qwerty_test
./test/out/stopwatch_controls_test
./test/out/stopwatch_test
./test/out/qwerty_test
make -C sim stopwatch-check
pio run -e stopwatch
```

The Windows checks used Zig 0.14.1 and the previously documented host-only
word-wrap workaround. Screen previews exercise rendering, not the ESP32 main
loop or real button/touch timing. Physical-device checks still required:
button A/B orientation, long-press responsiveness under simultaneous scans,
focus scrolling/wrapping through complete menus, PIN/update/calibration
boundaries, wake-only behavior, and touch accuracy near curved button ends.
This revision has not been flashed as part of these checks.

Final controls build: `pio run -e stopwatch` passed. Firmware image: 1850368 bytes; SHA-256 `425a16c028c3795820630e4270bf9c71ebe963be8f348ddbad898c6536a0d6b7`. `git diff --check` passed.

## Color transfer correction (2026-11-19)

The RGB332 lookup produces native RGB565 words. M5GFX 0.2.26's `uint16_t*`
image overload interprets them as native RGB565 only with `setSwapBytes(true)`;
`false` selects its already-swapped `swap565_t` representation. The previous
StopWatch push path used `false`, scrambling color bitfields while preserving
black and white. Both full-frame and partial-row transfers were corrected to use `true`.
Direct `fillRect` colors and the user's inversion/RGB-order preferences are
unchanged. The existing exhaustive 256-color conversion regression passed;
The user confirmed improved colors after uploading this correction.

### Avatar color audit (2026-11-19)

Full and partial transfers now use explicit `lgfx::rgb565_t` row pixels,
selecting M5GFX's native RGB565 converter independently of `setSwapBytes`.
The avatar uses this same transfer path. Its FastSprite RGB565-to-RGB332
conversion matches TFT_eSPI's expression; the lookup expansion matches
TFT_eSPI's `color8to16` including its four blue levels.

`make -C test stopwatch-color` checks all 256 framebuffer colors through the
installed M5GFX native/wire color types, plus fixed expected values for the
five base avatar colors. Expected native RGB565 values after quantization:
shadow `2000`, main fur `4920`, highlight `9360`, tan skin `FECB`, dark skin
`DB6B`. All passed. This verifies numerical colors, not optical panel accuracy.
The existing 8-bit framebuffer loses subtle shades; growth stages, outfits
and temporary shimmer intentionally alter the palette and remain unchanged.

## Combined-button checks (2026-11-19)

The input/geometry suite now passes 56 assertions, including simultaneous and
staggered combined presses, the 500 ms boundary, once-only shutdown, release
suppression, wake-only and update-blocked gestures, touch takeover, late
joining of an already-fired single press, timer rollover, and returning to
ordinary single-button controls. Shutdown uses `M5.Power.powerOff()` through
the board adapter. Verify actual power-off on battery and USB, button timing,
and the main-menu shortcut on hardware after upload.

## Home speech and Terminal Mode correction (2026-11-19)

The home screen reserves a separate speech band starting at y=212; its bubble
rise target now uses the circular badge geometry instead of the rectangular
counter-row calculation. Detection badges share a curved header rail, with
up to twelve types arranged in non-overlapping rows above the speech band.

StopWatch Terminal Mode replaces the rectangular window/grid with five large,
two-line rows following the glass chords. Sort tabs, scrolling, latest/reset,
and tap-to-select/tap-again-to-open remain available, with matching focus
rectangles. The former purple grid was an intentional theme, not another
RGB565 byte-order fault. The 18 real-UI checks pass, including circle bounds,
terminal scrolling and selection, and speech staying below the badges.

## Rim controls and menu sizing (2026-11-19)

Home detection counters now sit on the outer rim, with a second inset row
when needed. They have transparent interiors and outlined text for contrast;
the former black backing rail is removed. The smaller-radius hamburger
control sits underneath them. Terminal and submenu headers retain their
separate top placement. Device focus uses the same arc as the drawn control.
The decorative perimeter pulse and synthwave sun outline are omitted on
StopWatch. Menu text defaults to size 3 and settings rows are at least 44 px.

Settings geometry explicitly selects its measurement font so touch rows do
not depend on the previously rendered font. The sprite-backed UI regression
checks all six settings pages, brightness stepper hit targets, nested scroll
state, curved footer mapping, device focus and speech clearance. Touch feel
and readability still need confirmation on the physical glass after upload.

## Submenu touch audit (2026-11-19)

The audit follows physical input through the circular-to-logical mapping,
then checks drawn controls against each screen's hit handler. It covers the
six Settings pages; Power Saver, Status Light, Security, Detection Filter,
Ignored Devices and SquachMesh lists; saved/scanned WiFi and My Alerts;
password/text and phone/PIN keyboards; firmware/squad update controls;
confirmation dialogs; Outfit, Diary, Bingo, Dex, System Properties, Log/raw
scan details, and their shared footer/back geometry. LoRa-only board pages
are outside the StopWatch build. Security/OTA guards remain in their existing
handlers: this audit does not bypass lockout or trigger a real update/wipe.

Corrections:

- Ignore-list REMOVE hit regions now include the same circular row inset as
  drawing, instead of using the old rectangular right-edge column.
- Power, Light, Security, Detection Filter and Mesh menu row measurement
  selects a fixed font before calculating height, as Settings now does.
- WiFi, My Alerts and update network lists stop above the curved footer.
- Password/text entry redraws its control registration each frame on
  StopWatch, including after its initial settle period. Idle Back works.
- Firmware-switch and Bingo confirmations remove covered controls from
  focus/touch registration before drawing their own buttons.
- OTA full direct-panel repaints publish and draw their curved controls even
  after releasing the framebuffer; partial progress repaints retain them.
- A physical rim press keeps its original logical target until release.
  Starting in a footer gap consumes that whole press. Body gestures retain
  real motion for scrolling and keyboard slides.

The host UI suite checks drawn-control centers through the circular mapping,
all Settings page scroll positions, dependent-font row stability, ignored
Remove targets at multiple scroll positions, modal exclusivity, idle keyboard
Back, and rim press/release behavior. The simulator now matches the installed
TFT_eSprite driver when a datum viewport changes logical width/height.
Physical capacitive-touch feel, lockout/recovery and an actual OTA transfer
still require hardware interaction; host checks do not substitute for these.

The audit also found drawn hamburger controls without routes on WiFi lists,
My Alerts, Bingo, Dex and the idle Update screen. Their StopWatch handlers now
return to the parent menu (WiFi Add goes to saved networks; saved networks and
Update go to System). Header touches fire once per press. Active-update screens
hide that header rather than offer an inert or unsafe navigation target.

## Human Sensing return and WiFi selection (2026-11-19)

Settings clears its complete footer before redrawing it. When a handler changes
screen after drawing, the frame discards the old screen's pending curved
controls instead of painting them over the destination. A regression renders
Sensing then Settings and checks that both obsolete side buttons disappear.

The sensing plots now stop at y=284, and the status/WiFi card occupies y=292..334,
above the curved footer. Its explicit TAP TO SELECT WIFI action works with
or without a current network. Selecting a saved network with USE returns to
Sensing and honors that network instead of silently preferring another network
that joined previously. Back returns to Sensing without changing the preference.

The 61 UI checks pass. The engine is compiled for StopWatch and uses the native
ESP32 CSI callback and router-ping path. Actual live reception must be confirmed
from the device (LIVE, nonzero readings/second, completed calibration); a rendered
simulation is not proof of hardware sensing. USB diagnostics initially produced
no response, so live operation has not yet been verified in this check.

## Touch and keyboard follow-up

Rim buttons accept an eight-pixel margin beyond their curved outline. Exact
hits take priority; overlapping margins are rejected rather than selecting an
arbitrary neighbour. A held rim touch stays on its original target until release.
This helps near misses only when the touch controller reports a contact.

Footer buttons use a 179-pixel radius (14 pixels farther inward), leaving
25 pixels between their outline and the glass edge. Their narrower angular
spread preserves the gaps and keeps side buttons below the content. The
standard hamburger button is also inset; home retains its already-inset menu.

The password keyboard has ABC, 123 and symbol groups, with NEXT cycling within
each group. Five characters fit per row, with targets at least 44 pixels square.
SHIFT changes letter case; DEL deletes; OK confirms. Space is available in every
group. All printable ASCII password characters remain accessible.

Human Sensing shows reception rate and signal quality (Q), and asks for stillness
during calibration. LIVE indicates fresh data, not confirmed motion. Leave the
watch stationary, press RECAL, keep the room still until calibration completes,
then walk through the path between router and watch. Check for an increasing
motion score. Synthetic int8 CSI tests cover stillness, motion, settling and signal
loss; real-room sensitivity and edge contact reliability need device testing.

The follow-up passes 67 UI checks and the real CSI model's synthetic-signal test.
The user's device photo confirms LIVE reception at about 20 readings/second;
motion response in the room remains to be verified after stationary calibration.

### Sensing health diagnostics

StopWatch now computes the rate from nonempty CSI samples accepted by the model,
and caps quality below the usable threshold if no complete feature window has
arrived for two seconds. Packet arrival alone must not imply a healthy motion
detector. Regression tests cover zero payloads and incompatible sample windows;
both fail with the former quality logic and pass with this check.

The small line above the graphs shows `VAR` (current normalized amplitude
variation, multiplied by 1000), `BASE` (its calibration baseline in the same
units), and A/P/C (amplitude, phase and shape evidence scores). These are
diagnostic readings, not physical motion units. They help distinguish flat input
from a high baseline. No motion thresholds or sensitivity have been changed.

### Preserve channel-wide fading (StopWatch)

The former unit-mean CSI normalization discarded uniform signal attenuation:
even changing input amplitudes could produce VAR=0 and a zero motion score.
StopWatch now combines the normalized channel shape with the RSSI-derived
amplitude ratio relative to the first sample after reset. The same representation
is used during calibration. This retains received-power variation while rejecting
CSI receiver-gain changes that do not change RSSI. Motion thresholds are unchanged;
the input representation has changed and needs a fresh stationary calibration.

The StopWatch CSI setup uses fixed scaling (shift 1), LTF merge and channel
filtering, matching [Espressif's router CSI example](https://github.com/espressif/esp-csi/blob/master/examples/get-started/csi_recv_router/main/app_main.c).
Tests reproduce the old zero-VAR failure and cover its correction, gain-only
changes, calibrated RSSI noise, movement above that noise and return to stillness.
These synthetic tests establish the code correction; response in the user's room
still requires physical confirmation.

### Alert transition cleanup

The device-found card no longer draws a colored circle around the glass edge.
If a screen changes while its frame is being drawn, that incomplete frame is
discarded and the panel retains the last complete image. Touch controls are
invalidated until the incoming screen is rendered; focus appears only after
navigation restores its target for that screen. This removes the white/cyan
fallback square seen over the cleared transition frame. The 75 UI checks cover
discarding outgoing controls, waiting for focus restoration, curved highlights,
and the ring-free alert card. The initialization text remains on Theme::AMBER,
as requested by the color-change rollback.

### Early boot prompt readability

The early blue update-check prompt and orange touch-reset prompt are separate
from the animated splash. StopWatch now centers these prompts on black using
size-3 headings and size-2 instructions, instead of tiny text or a clipped
top-left line. The AMOLED uses brightness 96 during these prompts (previously
24 during the update check). The recovery window is two seconds; its existing
800 ms hold and calibration reset behavior are preserved. Radio startup still
uses its reduced brightness, and the saved brightness is restored afterward.
All 77 UI checks pass, including bounds and text-size checks for both prompts.


### Desktop notification placement

The small device notification is centered above the curved footer, with size-2
text and an inset rounded card. The complete card is registered for touch and
physical-button focus; its target disappears when the notification expires.
StopWatch UI checks cover placement, taps, focus, and expiry (82 checks).
