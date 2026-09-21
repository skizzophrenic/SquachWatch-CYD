# Original M5Stack Cardputer

Experimental support for **K132 / Stamp-S3**, with a 240×135 display and the
original GPIO keyboard matrix. **Not Cardputer ADV:** that board has a different
keyboard controller. This is an opt-in board target with its own compact UI;
the CYD targets and default build list remain unchanged.

## Features and scope

- Shared Wi-Fi/BLE signature detection, confidence grades, live reception counts,
  detection log/details, optional microSD CSV logging and saved lifetime counts.
- Animated Squachy, speech bubbles, petting, earned shades/outfits, unlock banners,
  wardrobe, growth/reward progress diary and persistent 4×4 detection bingo.
- Keyboard settings: brightness, screen dimming, BLE scan mode, alert-confidence
  threshold, per-type detection filters, ignored devices and time zone.
- Per-device ignore (persistent) and snooze (until reboot). These suppress alert
  cards but keep detections in the log; disabling a detection type stops that
  type being logged or counted.
- Battery voltage from GPIO10, optional idle dimming with slower animation,
  and a wake key that is consumed so it cannot accidentally change a setting.
  Scanning continues while dimmed. Power saving defaults off.
- USB diagnostics, framebuffer capture and bounded console input. Startup does
  not wait for a serial monitor. No network credentials are needed.

BLE defaults to **passive**. K → BLE scan can choose AUTO (the upstream adaptive
policy) or ACTIVE to request scan responses, which some name-based signatures
need. Active scanning transmits BLE scan requests. There is no Wi-Fi association,
mesh advertising or update server in this target.

W and B in the title bar are Wi-Fi frames/second and BLE adverts/second.
Receiving packets is different from matching a known signature. A match is not
proof that the device is the named product. Battery voltage is an ADC estimate,
not a fuel-gauge percentage or charging indicator; USB power affects it.

Not included: touch UI, background mini-games and their event-only costumes,
manual raw scanner, hunt mode, audio/status-light effects, deep sleep, security
PIN UI, SquachMesh, black-box flash journaling, OTA or a Cardputer web flasher.
The wardrobe still lists the shared total number of outfits; only count-earned
and free outfits are attainable in this target.

## Controls

| Key | Action |
| --- | --- |
| S | Home; scanning runs on every page |
| L / Enter on home | Detection log |
| D | Radio, memory, SD and clock diagnostics |
| H | Keyboard help |
| K | Settings |
| J | Diary and next reward/growth milestones |
| P | Pet Squachy |
| O | Wardrobe |
| C | Cycle earned shades and open wardrobe |
| B | Bingo; Enter deals a new card only when the current card is full |
| Space | Start/stop the pose showcase; it grants no detections or rewards |
| ; / . | Move up/down; cycle outfits in wardrobe (Fn optional) |
| , / slash | Adjust selected setting; previous/next outfit |
| Enter | Choose setting, toggle detection type, open log detail, advance intro |
| I in alert/detail | Ignore device; in detail, press again to restore alerts |
| X in alert/detail | Snooze device until reboot |
| Enter in ignored list | Remove selected ignore |
| Grave / Fn-Esc / Backspace | Back; dismiss an alert |

Release a key before pressing it again; navigation does not auto-repeat.
The first control key after dimming wakes the display without activating it.
Match cards last eight seconds and only interrupt home. Enter opens their
details; other controls dismiss them. Detail views are snapshots: reopen one
from the log to refresh. Ignored devices can always be recovered via K.

## Build and upload

The pinned PlatformIO platform is espressif32 6.5.0 (Arduino-ESP32 2.0.14).
TFT_eSPI 2.5.43 and NimBLE-Arduino 2.5.1 are pinned for this target.

```sh
pio run -e cardputer
pio device list
pio run -e cardputer -t upload --upload-port YOUR_PORT
pio device monitor -e cardputer --port YOUR_PORT
```

The console is native USB at 115200 baud. If download mode is needed, switch
off, hold G0, connect USB/apply power, then release G0. Installation replaces
the installed firmware and partition table; back up anything needed first.

Use PlatformIO's upload recipe. The app at
`.pio/build/cardputer/firmware.bin` belongs at **0x10000**, not zero. S3 bootloader
offset is **0x0**, partitions **0x8000**, boot_app0 **0xe000**. The existing 4 MB
two-slot partition layout is retained inside the 8 MB flash; unused flash is
not repurposed. **Do not use CYD images, the CYD web flasher or CYD OTA updates.**
Builds do not upload automatically. CI compiles this target separately; release
packaging and the default three CYD/AWOK builds are unchanged.

## Clock and console

Commands are newline-terminated. Close other serial monitors before using the
bench tool. It requires Python and pyserial (also installed with PlatformIO).

| Command | Effect |
| --- | --- |
| STATUS | Radio totals, heap, boot duration, game state, voltage and settings |
| LOG | Current RAM detections, one row per loop |
| SD | Last 192 bytes of the current uptime-day CSV |
| SCREEN | Stable 240×135 RGB565 framebuffer; rendering resumes after at most 5 s |
| FRAMECHECK | Check all 32,400 pixels clear; redraw the UI afterwards |
| TIME epoch | Set UTC Unix seconds (2025–2038); K selects the display time zone |
| KEY character | Same controls as keyboard; e = Enter, grave = Back, space = showcase |

KEY can change persisted settings, just like the physical keys. No console
command injects sightings or unlocks rewards. LOG and SD may contain device
identifiers; redact them before attaching diagnostic logs to an issue.

```sh
python tools/cardputer_bench.py --port YOUR_PORT --command FRAMECHECK
python tools/cardputer_bench.py --port YOUR_PORT --key k --screenshot settings.png
python tools/cardputer_bench.py --port YOUR_PORT --seconds 600 --log soak.log
```

The clock stays trusted across a software reset. After power loss, it may use
the upstream saved estimate; set TIME again for a trustworthy date. Weekly
bingo rollover requires trusted time. Without it, the current card stays in
place and can be replaced after completion. SD CSV names/timestamps retain
the upstream uptime-based format.

## Hardware and implementation

| Function | Configuration |
| --- | --- |
| LCD | ST7789V2, native 135×240, landscape rotation 1, panel offsets |
| LCD pins | MOSI 35, CLK 36, CS 37, DC 34, RST 33, BL 38 |
| SD pins | MOSI 14, CLK 40, MISO 39, CS 12 |
| Keyboard | Selector outputs 8/9/11; inputs 13/15/3/4/5/6/7 |
| Battery | ADC GPIO10, equal 100k divider; calibrated mV × 2 |
| SPI | Display on SPI2/FSPI; SD on separate SPI3/HSPI |
| Buffer | One 32,400-byte 8-bit sprite allocated before radio startup |
| Brightness | GPIO38 LEDC, 5 kHz, 8 bit |

The source filter excludes the CYD entry point, touch and register-level
frame push. Small board/input modules avoid initializing a second display
stack. The shared detector uses ordinary S3 Wi-Fi initialization; the CYD
memory-saving restart stays unchanged. Radio startup skips blocking
Serial.flush on native USB, and transmit timeout is zero.

Frame clearing uses fillSprite, not the inherited fillScreen: TFT_eSPI's
non-virtual fillScreen reads native panel dimensions and otherwise leaves
105 columns of the landscape sprite uncleared. FRAMECHECK guards this on
the actual library and hardware.

## Validation

Tested on one original K132, ESP32-S3 rev 0.2 / 8 MB flash:

- Display, keyboard, Squachy animation and speech bubbles confirmed by the user.
- SD mount and actual AirTag CSV records confirmed during prototype testing.
- No-host startup verified after multiple resets; ready in about 1.1 seconds.
- All-pixel frame-clear check passed; screenshots verified home, wardrobe,
  diary, bingo, settings, help and diagnostics.
- Dimming after 30 seconds and wake-key consumption verified while reception
  continued. Brightness, power settings, pet count, outfit and bingo state
  survived a software reset. A trusted serial-set clock survived it too.
- Existing cyd, cyd-ili9341 and awok builds pass, as do the desktop emulator,
  host tests and fresh-process Squachy reward/persistence checks.

```sh
make -C test
make -C sim -j4
make -C sim progress-check
pio run -e cardputer -e cyd -e cyd-ili9341 -e awok
```

Host tests cover keyboard coordinates/debounce, timer rollover, packet-rate
counter wrap, epoch parsing, sighting identity, alert filtering, the shared
decoders and bingo rules. Reward tests use desktop NVS files and never modify
the hardware's earned progress.

Remaining hardware coverage: battery-only cold boot/runtime measurement,
no-card boot/card-removal behaviour, controlled RF comparison against a CYD,
and long-duration busy-environment testing. These limitations are why this
target is experimental rather than a claim of feature or hardware parity.

## References

- [M5Stack original Cardputer hardware](https://docs.m5stack.com/en/core/Cardputer)
- [Original Cardputer schematic](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/481/Sch_M5Cardputer.pdf)
- [M5Stack MIT-licensed keyboard matrix reference](https://github.com/m5stack/M5Cardputer/blob/f1392858b9994c3547120e602a57d3553d16ab01/src/utility/Keyboard/KeyboardReader/IOMatrix.cpp)
- [Espressif S3 radio coexistence](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-guides/coexist.html)
