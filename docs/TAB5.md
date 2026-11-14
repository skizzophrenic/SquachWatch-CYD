# M5Stack Tab5 (ESP32-P4 + ESP32-C6): experimental

A 5" tablet: an **ESP32-P4** application processor with a 1280×720 MIPI-DSI
panel and capacitive touch, and an **ESP32-C6** beside it for the radios. It is
the first board here whose main chip has **no radio at all**. Every WiFi frame
and every Bluetooth advert the detector sees comes across an SDIO link from the
C6.

| | Classic CYD (ESP32-2432S028R) | M5Stack Tab5 |
|---|---|---|
| Main chip | Xtensa LX6, dual-core 240 MHz | **ESP32-P4**, RISC-V, dual-core 360 MHz |
| Radio | on the chip | **none on the P4**: an ESP32-C6 over SDIO |
| WiFi | 2.4 GHz | 2.4 GHz (the C6's), WiFi 6 |
| Bluetooth | BLE 4.2 + Classic | BLE 5 (the C6's), no Classic |
| Screen | 2.8" 320×240 SPI | **5" 1280×720 MIPI-DSI** (two lanes) |
| Touch | resistive XPT2046 | capacitive, in the panel chip |
| Flash / PSRAM | 4 MB / none | **16 MB / 32 MB** |
| USB | CH340 | the P4's own USB-serial-JTAG (USB-C) |

**Status: experimental, on the web flasher's test builds only** (squachwatch.com/?lab=1). It runs the whole UI with
WiFi and Bluetooth detection on one unit. The C6 needs its own firmware written
first (see [The C6 needs our firmware](#the-c6-needs-our-firmware)), which is
the main reason it is not on the public list yet. Flashing the Tab5 from the
browser never touches the C6: on a Tab5 whose C6 still has M5Stack's firmware,
the app runs with WiFi and Bluetooth off.

## What works and what does not

| | State |
|---|---|
| Screen, landscape 1280×720 | Works. 60 fps on a still background, about 19 fps on an animated one |
| Touch | Works. Capacitive, no calibration |
| WiFi detection | Works, through the C6 |
| Bluetooth detection | Works, through the C6 |
| SquachMesh | Built in. The Tab5 hears peers and advertises; a visit on its screen has not been checked yet |
| Portrait | Not yet. The picture is locked landscape |
| SD card | Not driven yet |
| Speaker, battery, power button | Not driven yet |
| Updates over WiFi or Bluetooth | Not yet. The radio sniffs; it does not connect |
| Web flasher | Test builds only (`?lab=1`), from the first lab run after 2026-10-19 |

## Build and flash

The P4 does not exist in the Arduino core every Xtensa board is pinned to, so
`[env:tab5]` carries its own `platform` line (pioarduino, Arduino 3.3.12 /
IDF 5.5.5), like the ESP32-C5 boards. Build it from its own PlatformIO home,
or its installer can break `pio` for every other board:

```bash
PLATFORMIO_CORE_DIR=~/.pioarduino ~/.pioarduino/penv/Scripts/pio.exe run -e tab5
```

Flash with `tools/flash_known.py <PORT>`, which reads the board's MAC first and
refuses anything it does not know. Plug into a **motherboard USB port on the
back of the PC**: on a front-panel port the Tab5 browned out after about 70
seconds with the radio sniffing.

The console is the P4's USB-serial-JTAG at 115200. Two things about it:

- **DTR and RTS cannot reset the P4.** Opening the port does nothing to the
  board, which is handy for reading a running board and useless for catching a
  boot log. Open the port first, then power-cycle or flash, to see one.
- **Prints never wait for a reader.** With the cable in and nothing reading the
  port, the core's USB serial blocked on every print, and the UI lagged and
  froze only while plugged in. `Serial.setTxTimeoutMs(0)` drops output nobody
  is reading instead.

Two bench builds sit beside it: `tab5-paneltest` (M5GFX alone: colour bars, the
panel's size, every touch as x,y) and `tab5-radiotest` (the C6 link alone:
power, SDIO, the hello, then 30 seconds of sniffing on channels 1, 6 and 11
with one line per frame).

## The screen

The app draws a **640×360** frame and the board doubles it to 1280×720, the
same arrangement as the CrowPanel 7. 640×360 at 8 bits is 225 KB, which fits
in the P4's internal SRAM; the full 1280×720 would be a 900 KB frame in PSRAM
and four times the drawing for the same layouts.

- **M5GFX owns the panel.** It probes the two I²C expanders, tells this
  unit's ST7123 apart from the earlier ILI9881C panel, brings up DSI, and hands
  over a 720×1280 RGB565 framebuffer in PSRAM that the DSI engine scans out
  by itself.
- **The glass is portrait as wired**, so `src/tab5_blit.cpp` turns the picture
  a quarter turn as it copies: logical (x, y) lands at native (719 − y, x).
  It writes one logical column as two native rows, in order (the fast direction
  for PSRAM), and only the columns that changed since the last frame, with a
  full frame every so often so a stale line cannot stick.
- **TFT_eSPI is kept as a type, not a driver.** Every UI function takes a
  `TFT_eSPI&`, so the class stays; `include/tab5_display.h` makes its bus calls
  inert and `extra_script.py` lets it compile for a chip it has never heard of.
- **Backgrounds fill the whole 640×360.** Four of them were drawn into
  buffers sized for the 3.5"'s 480 or the 2.8"'s 320, and left part of the
  Tab5 bare: the glyph rain and the fire covered the left half, the terminal
  left its right third and lower half empty, and the toasters were thin on the
  ground. `src/theme.cpp` now sizes those buffers from `SQW_BG_MAX_W` (640
  here) and draws twice the toasters, stars and flames (`SQW_BG_DENSITY`).
  Every other board's backgrounds render pixel-for-pixel the same as before
  (`sim/bg_before_after.sh`). On the board: terminal 31–44 fps, toasters 24,
  glyph rain 15, fire 11.
- **Its own starting sizes.** On a fresh Tab5 the FONTS page starts with
  button labels one step bigger and the counter tiles one step smaller,
  picked by comparing every step on the real screens
  (`sim/render_tab5_scale.sh`, which renders them with `squachsim-tab5` from
  `sim/build_tab5_sim.sh`). The owl, the wolf and the snow riders speak in
  the pets' text size on every board now, so FONTS > PETS moves them too.
- **Why animated backgrounds are slower:** the cost is the CPU writing into
  PSRAM while the DSI engine is reading the same memory, about 30 ms for a
  full 1.8 MB frame. The P4's 2D hardware (the PPA, which scales and rotates)
  is the next thing to try.

## The radio: an ESP32-C6 over SDIO

The P4 reaches the C6 through Espressif's **ESP-Hosted**: the host half is
built into the Arduino core, and the C6 runs the slave half. Stock ESP-Hosted
carries WiFi *connections* and Bluetooth HCI. It has **no promiscuous mode**,
and that is what the detector needs.

```
 ESP32-P4 (the app)                               ESP32-C6 (the radio)
 ───────────────────                              ────────────────────
 detection.cpp  ── WiFi frames ◄── custom msgs ◄── squach_sniff.c
                                     (SDIO)          (our extension)
 esp-nimble-cpp ◄─ NimBLE host ◄── HCI over SDIO ◄─ BT controller (stock)
```

### WiFi: our sniff extension

`tools/tab5-c6/squach_sniff.c` is added to the stock ESP-Hosted 2.12.13 slave.
It boots exactly like the stock image and does nothing until the P4 asks.
Commands and events travel on ESP-Hosted's custom-message channel:

| ID | Direction | What |
|---|---|---|
| `0x5100` SNIFF | P4 → C6 | promiscuous on/off |
| `0x5101` CHANNEL | P4 → C6 | park on one channel |
| `0x5102` HOP | P4 → C6 | channel list and dwell; the C6 hops on its own clock |
| `0x5103` HELLO | P4 → C6 | is the extension in this image? |
| `0x5180` ACK | C6 → P4 | command and its result |
| `0x5181` HELLO | C6 → P4 | `squach-sniff 1` and whether it is sniffing |
| `0x5200` FRAME | C6 → P4 | RSSI, channel, type, then the frame |
| `0x5201` STATS | C6 → P4 | seen / forwarded / dropped, once a second |

What gets forwarded:

- **Management frames** the detector reads (beacon, probe request, deauth,
  disassociation) go over **whole**, up to 600 bytes, so the P4 parses SSIDs,
  Remote ID, pwnagotchi payloads and RSN exactly as every other board does.
- **Data frames** go over as their **24-byte header only**, at most one per
  sender every two seconds. The detector only wants the two addresses, and a
  busy channel would otherwise flood the link.

On the P4, `src/tab5_radio.cpp` owns the link and queues frames, and
`detection.cpp` feeds them to the same parser every other board runs on its
own radio.

### Bluetooth: the C6's controller, NimBLE on the P4

Bluetooth needs no extension. The stock slave already runs the C6's Bluetooth
controller with its HCI on the same SDIO link, and the Arduino core for the P4
already includes the NimBLE host, built to talk to it.

- **NimBLE-Arduino cannot be used here.** It brings its own copy of the NimBLE
  host and expects a Bluetooth controller on the same chip, and the P4 has none.
- **esp-nimble-cpp 2.5.0 is used instead.** It has the same C++ API (same
  author, same version line) and runs on the host the core already has, so
  `detection.cpp`'s scanner is the code every other board runs. It needs four
  `CONFIG_NIMBLE_CPP_*` defines in `platformio.ini`, because an Arduino build
  never reads its Kconfig.
- **The order matters:** link up (`Tab5Radio::begin()`), then the C6's
  controller on (`Tab5Radio::bleBegin()`, which is
  `esp_hosted_bt_controller_init()` and `_enable()`), then
  `NimBLEDevice::init()`. Without the controller, `init()` waits for a sync
  that never comes, and it waits forever. So a boot whose link is down skips
  Bluetooth entirely, and every tick that would touch NimBLE checks first.

### The C6 needs our firmware

A Tab5 from the box has M5Stack's factory C6 firmware (ESP-Hosted 1.4.1). This
build needs **ESP-Hosted 2.12.13 plus our extension** on the C6. Without the
extension the P4's hello goes unanswered, and the board runs with WiFi and
Bluetooth both off and says so on the console.

The C6 is written **from the P4, over the SDIO link**, by a small tool
flashed to the P4 for the purpose. No wires are needed. How to rebuild it, the
images, and the patches are in [tools/tab5-c6/README.md](../tools/tab5-c6/README.md).

> **There is no rollback on the C6.** An image that boots and then crashes
> leaves the radio dead until it is rewritten over wires: six pads beside the
> C6 module (GND, G9, RST, RXD, TXD, 3V3) and a 3.3 V USB-serial adapter. M5Burner
> has the factory C6 image. This is why the extension does nothing at boot.

## Facts that each cost an evening

- **The versions must match.** The core's ESP-Hosted host is **2.12.13**, so
  the C6 slave must be 2.12.13 too, not the newest release.
- **The C6's reset is active-high on this unit.** GPIO15 resets it. ESP-Hosted's
  own Tab5 preset says active-low, and with that the C6 never answers (sdmmc
  `send_op_cond` 0x107). The core's host happens to be built active-high; the
  app sets the Tab5's SDIO pins at run time.
- **The link sometimes comes up on the second try.** The app retries once.
- **The C6's WiFi driver does not start itself.** Sniffing is refused
  (`ESP_ERR_WIFI_NOT_INIT`) until the P4 calls the remote `esp_wifi_init()`,
  `set_mode()` and `start()`.
- **An empty custom message is refused** by the host library
  (`ESP_ERR_INVALID_ARG`) before it is ever sent. The hello carries one byte.
  This one made the first C6 write tool loop, rewriting the C6 about eleven
  times, until it was stopped.
- **The core's host has room for three custom-message handlers.** ACK and
  FRAME stay registered; HELLO is registered for the handshake and then swapped
  for STATS.
- **The C6's power** is bit 0 of output register 0x05 on the I²C expander at
  0x44. M5GFX already sets it at panel init; the app checks it took.

## Pinout

From M5GFX and M5Stack's reference board support. The display, touch and
expanders are all M5GFX's; the firmware only names them.

| What | Pins |
|---|---|
| I²C (touch, expanders) | SDA 31, SCL 32 |
| Touch interrupt | 23 (touch controller at 0x55, inside the ST7123) |
| Backlight | 22 |
| C6 SDIO | CLK 12, CMD 13, D0 11, D1 10, D2 9, D3 8 |
| C6 reset | 15, **active-high** |
| Expander 0x43 | P4 LCD reset, P5 touch reset, P1 speaker enable, P2 5 V out |
| Expander 0x44 | P0 C6 power, P3 USB 5 V, P7 charge enable |

## Memory and flash

- **Partitions:** the 16 MB two-slot table (`partitions_ota_16mb.csv`) shared
  with the C5 boards: two 6 MB app slots, a core dump, and the black box at
  0xC20000.
- **The app is 1.77 MB**, a little under 30% of its slot. Static RAM is 81 KB of
  512 KB.
- **The frame** lives in internal SRAM on purpose (`TAB5_FRAME_INTERNAL`): the
  blit reads it column by column, and that is cheap in SRAM and slow in PSRAM.

## Validation

Ticked only from the board itself.

### Confirmed on the board (2026-10-19)
- [x] **Boots** and runs the full UI at 1280×720 landscape
- [x] **Touch** lands where it is pressed, with no calibration
- [x] **No lag or freeze on the cable** after the print fix
- [x] **The C6 link comes up** and answers `squach-sniff 1`
- [x] **WiFi detection:** SquachEmit walked through all 17 categories. Every
      WiFi-bearing category was caught (Flock, Axon, Meta, skimmer, Raven,
      drone Remote ID, ALPR, cameras, Ring, Evil Twin, the WiFi hacker kit)
- [x] **Bluetooth detection:** the same walk caught every Bluetooth tracker
      (AirTag, Samsung tag, Google tag, Tile) and the Bluetooth shots in the
      mixed categories, with WiFi running at the same time, about 35 adverts a
      second in a quiet room
- [x] **No brownout** with both radios running, on a back USB port
- [x] **The other boards still build** after `detection.cpp` turned its
      Bluetooth half on for the Tab5

Reading the walk's numbers: the emitter sends all its Bluetooth shots from one
address, and the log keeps one entry per device per type, so each category
shows **one** Bluetooth catch however many shots were fired. iBeacon shows none
because iBeacon is **off by default** in the detection filter on every board.
Evil Twin depends on which channel the Tab5 is on during the emitter's few
seconds; it was missed once and caught on a rerun.

### Still to confirm
- [ ] **A SquachMesh visit** shows on the Tab5's screen (peers never count as
      catches, so the walk cannot show it)
- [ ] **Two-board messages** with another SquachWatch
- [ ] **Soak:** an hour or more with both radios, heap flat, no resets
- [ ] **A boot with the link down** skips Bluetooth cleanly instead of hanging
- [ ] **Settings persist** across a power cycle

## Not done yet

SD card, speaker, battery and power button, portrait, the PPA for faster
animated backgrounds, updates over WiFi or Bluetooth, and a place on the web
flasher (which also needs the C6 firmware solved for someone who has never
opened a terminal).
