# Original Cardputer: initial port

Experimental firmware for the **original M5Stack Cardputer (K132 / Stamp-S3)**.
This is a detector-first interface, not full CYD feature parity. Do not use
this target for the Cardputer ADV: its keyboard is an I2C controller, whereas
this target drives the original keyboard's GPIO matrix.

## Included

- The upstream Wi-Fi/BLE detection engine and signature tables, including
  confidence grades and Remote ID decoding (no dedicated Remote ID detail UI yet).
- A native 240x135 interface: animated Squachy home, five-row detection log, selected
  device details, and live reception/memory diagnostics.
  The title bar shows live W (Wi-Fi frames/s) and B (BLE adverts/s) counts;
  these include traffic that does not match any detection signature.
- Upstream Squachy reactions, growth, persisted pet counts and cosmetics,
  an outfit wardrobe, a pose showcase, and persistent 4x4 detection bingo.
  Three outfits are available immediately; detection milestones earn more.
  Petting earns shades. The showcase does not award fake detections.
- Eight-second match cards on the home screen. Browsing a log or diagnostics
  is not interrupted. A control key dismisses a card.
- Optional microSD CSV logging, and the engine's saved lifetime counters.
- Native USB CDC diagnostics at 115200 baud, with a statistics line every
  ten seconds. Firmware starts without a USB host connected.

Gamification build validation (2026-09-21): all four PlatformIO targets and
all 21 host test executables passed. Flashed and verified on the original
Cardputer. At 69 seconds the animated build reported 164,760 bytes free heap,
155,636 bytes in the largest block, 166 Wi-Fi frames, 6,150 BLE adverts, zero
dropped adverts and SD mounted. A `SCREEN` console readback verified the
actual 240x135 home framebuffer. `SCREEN` streams RGB565 hex rows while
briefly freezing the rendered frame; scanning continues, and a five-second
timeout resumes rendering if the reader disconnects. `STATUS` also reports
pet count, equipped outfit, unlocked outfits and bingo progress.
The animation clear uses `TFT_eSprite::fillSprite`: calling the base
`TFT_eSPI::fillScreen` through a sprite reference only cleared the native
135-pixel panel width, leaving old poses and text across the right side of
the 240-pixel framebuffer. `FRAMECHECK` fills the buffer with a test color,
runs the normal frame clear and checks all 32,400 pixels for stale content;
the next frame restores the UI. This check uses the actual hardware library.
The gamification build also passed a reset with no serial reader for 15
seconds: ready at 1,078 ms, with both radios receiving at the 20-second check.

The existing lifetime total of 11 is retained and unlocks Tinfoil alongside
the three free outfits. Bingo starts its own card; it does not manufacture
marks from historical lifetime counts. New gamification controls and visual
appearance still need the user's hands-on acceptance.

The first version pins BLE scanning to **passive** for a repeatable radio
baseline. Devices whose identifying names are only in scan responses may
not be identified in this mode. Wi-Fi promiscuous capture still runs alongside
BLE and hops through the upstream engine's channels. Both share one radio;
reception rates need comparison with a CYD on real hardware.

## Controls

| Key | Action |
| --- | --- |
| S | Squachy home (scanning continues on every page) |
| P | Pet Squachy |
| O | Wardrobe; ;/. or Enter cycles unlocked outfits |
| C | Cycle unlocked shades and open wardrobe |
| B | Detection bingo |
| Space | Start/stop the animation showcase |
| L | Detection log |
| D | Diagnostics |
| ; / . | Up / down in the log (Fn is optional) |
| Enter | Advance first-boot intro, open log, or inspect selected entry |
| Grave / Fn-Esc / Backspace | Back |

There is no key repeat; release and press again to move another row. Device
details are a snapshot captured when Enter is pressed. Return to the log and
reopen to refresh. SD files currently follow upstream SdLog behavior: names
and CSV timestamps use uptime, not wall-clock dates. A missing card is optional
and is shown explicitly on the overview.

## Build and flash

The USB console also accepts newline-terminated, read-only commands:

- `STATUS`: print radio totals, boot duration, live record count and heap.
- `LOG`: stream the current RAM detection records, one per loop iteration.
- `SD`: reopen the current uptime-day CSV file and read its final 192 bytes.

Physical navigation keys print an action/page diagnostic when a USB reader is
present. Output has a bounded buffer and zero transmit wait; these diagnostics
do not require a serial monitor to be attached.

From the repository root, with PlatformIO installed:

```sh
pio run -e cardputer
pio run -e cardputer -t upload --upload-port /dev/cu.usbmodemYOUR_DEVICE
pio device monitor -e cardputer --port /dev/cu.usbmodemYOUR_DEVICE
```

Replace the port with the one listed by `pio device list`. For download mode,
switch the Cardputer off, hold G0, connect USB/apply power, then release G0.
The USB device may re-enumerate after flashing. An initial installation replaces
the installed firmware and partition table; save anything needed from the
previous firmware first. Uploading is deliberately not part of the build.

The application image is `.pio/build/cardputer/firmware.bin`. It is an **app
image at 0x10000**, not a complete image to write at address zero. PlatformIO
upload supplies the correct S3 bootloader and partitions. Never use the CYD
web flasher, CYD bootloader, or CYD OTA images with this target.

## Hardware decisions

| Function | Configuration |
| --- | --- |
| LCD | ST7789, native 135x240, landscape rotation 1, panel offsets enabled |
| LCD pins | MOSI 35, clock 36, CS 37, DC 34, reset 33, backlight 38 |
| SD pins | MOSI 14, clock 40, MISO 39, CS 12 |
| Keyboard | Selector outputs 8/9/11; inputs 13/15/3/4/5/6/7 |
| SPI ownership | TFT_eSPI owns SPI2/FSPI; SD owns a separate SPI3/HSPI instance |
| Display buffer | One 32,400-byte, 8-bit sprite, allocated before radios |
| Flash | 8 MB chip, existing two-slot 4 MB partition layout retained |

The ordinary TFT_eSPI transfer path is used. The CYD register-level frame
push and touch code are excluded by an explicit source filter. The existing
CYD environments remain the default builds and do not use this entry point.
The small matrix reader avoids bringing a second display driver into the
project. Keyboard geometry follows M5Stack's public reference; the decoder
and debounce logic have a host test.

## Not implemented in this target yet

Touch UI, background mini-games/event-only outfit unlocks, settings editor, manual raw scans, hunt mode,
ignore/snooze controls, audio/status LED effects, power saving, battery gauge,
PIN/security UI, SquachMesh and firmware updates. The shared engine
still links its normal support modules, but this entry point does not start
black-box flash recording. Bingo persists but automatic weekly rollover needs
a trusted clock, which this port does not yet provide. No Wi-Fi credentials are
used and no mesh transmitter or update server is started.

Existing saved detection filters are loaded by the shared Settings module;
there is no filter editor yet. Match cards display the signature's confidence
and are not confirmation that the device is the claimed product.

## Validation and hardware acceptance

### Reboot fix: USB serial must remain optional

The first hardware build could remain on "starting" after a reboot until a
serial monitor was opened. The two radio-startup Serial.flush() calls entered
Arduino-ESP32 2.0.14 HWCDC::flush(), which waits for its transmit ring buffer
to empty without a timeout. With no USB reader, that wait can be indefinite.
Opening the monitor in the initial smoke test concealed the problem.

The radio startup now skips these flushes for native USB CDC builds, retaining
them for the existing UART-based CYDs. Cardputer also sets the USB transmit
timeout to zero so disconnected diagnostics cannot delay the application.
Periodic statistics include ready_ms, the uptime at which engine startup
finished. To check for regression, reset with no monitor, wait 15 seconds,
then attach: ready_ms must be well below 15,000 and reception must continue.
Attaching a monitor immediately after flashing is not sufficient validation.

The fix was flashed and verified through three hardware resets with no serial
reader for the first 15 seconds of each boot. Startup completed at 1,054,
1,059 and 1,052 ms respectively. At 20 seconds each run had received Wi-Fi
frames and BLE adverts, with SD mounted and zero dropped adverts. All three
existing CYD build targets still compiled. A physical battery power-cycle is
separate from these USB-connected reset tests.

Initial results, 2026-09-21:

Follow-up prototype validation: SD readback found a 361-byte CSV file with
AirTag detection records from the earlier run (latest timestamp 2,805,281 ms),
confirming actual signature classifications were written and survived reboot.
The latest read-only diagnostic commands were exercised on hardware. At
65 seconds the current build had received 135 Wi-Fi frames and 6,086 BLE
adverts, with 166,800 bytes free heap, a 155,636-byte largest block, and zero
dropped adverts. It had no new signature matches in that observation window;
the SD records above are from earlier, not newly generated test detections.

- Cardputer firmware compiled with PlatformIO 6.5.0 / Arduino-ESP32 2.0.14.
- All 21 host test executables passed, including the new keyboard test.
- The original cyd, cyd-ili9341 and awok environments compiled successfully.
- Uploaded to an original Cardputer reporting ESP32-S3 revision 0.2 and 8 MB
  quad flash; esptool verified the written data hashes.
- Boot allocated the framebuffer and mounted the inserted SD card (29,820 MB).
- The initial CYD slim-Wi-Fi path showed no received frames in the short
  observation. The Cardputer target now uses normal Arduino Wi-Fi startup.
  At uptime 41 seconds it reported 102 Wi-Fi frames and 2,736 BLE adverts,
  168,604 bytes free heap, a 159,732-byte largest block, and zero dropped
  adverts. This is a short smoke test, not an RF performance comparison.
- The user confirmed the original controls work well. The new gamification
  screens and keys await user feedback. Battery operation and extended soak
  testing remain to be confirmed.

The BLE callback also now retains the std::string returned by NimBLE's
getName() while using its c_str() pointer. Previously that pointer outlived
the temporary string in both normal detection and raw BLE scanning. All four
build environments and the 21 host tests passed after the correction. The
host tests do not emulate NimBLE callback execution; this fix follows the
actual library's return-by-value API and C++ object lifetime rules.

An optional full readback of the previously installed firmware was aborted
because USB readback was very slow. No recovery backup was produced.

Run the source tests with `make -C test`. They cover existing decoders and
protocol logic plus all 56 keyboard coordinates, debounce, held keys, release
and timer rollover. A successful firmware build only establishes toolchain
compatibility; it does not establish electrical or radio behavior.

Before calling this hardware-supported, verify on an original Cardputer:

1. Boot on USB and battery without a serial monitor. Confirm orientation,
   panel offsets and red/green/blue colors; check all documented keys.
2. Boot with no SD card, then with a FAT32 card. Confirm CSV events and that
   screen updates still work while SD writes occur.
3. Use known Wi-Fi and BLE test devices. Compare per-second counts and time
   to detect with a CYD in the same location. Verify confidence/name/MAC.
4. Browse and open records while new detections arrive. Verify selection and
   alert dismissal, including long key holds.
5. Run at least an hour in a busy RF environment. Capture the ten-second
   serial statistics; watch minimum heap, largest block, dropped adverts,
   resets and reception starvation. Then measure battery runtime.

## References

- [M5Stack original Cardputer hardware](https://docs.m5stack.com/en/core/Cardputer)
- [M5Stack keyboard matrix reference](https://github.com/m5stack/M5Cardputer/blob/f1392858b9994c3547120e602a57d3553d16ab01/src/utility/Keyboard/KeyboardReader/IOMatrix.cpp)
- [M5Stack display configuration](https://github.com/m5stack/M5GFX/blob/master/src/M5GFX.cpp)
- [Espressif S3 radio coexistence](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-guides/coexist.html)
