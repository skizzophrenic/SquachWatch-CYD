# RockBase NM-CYD-C5 (ESP32-C5) — BETA

A 2.8" CYD built on an **ESP32-C5** instead of an Xtensa ESP32. Same glass as
the classic board (ST7789 240×320, resistive XPT2046, microSD), same footprint
and mounting holes, different silicon underneath:

| | ESP32-2432S028R (classic CYD) | NM-CYD-C5 |
|---|---|---|
| Core | Xtensa LX6, **dual**-core 240 MHz | RISC-V, **single**-core 240 MHz |
| WiFi | 2.4 GHz, 802.11 b/g/n | **2.4 + 5 GHz**, 802.11 a/n/ac/**ax** |
| Bluetooth | BLE 4.2 + **Classic** | BLE 5.3, **no Classic** |
| 802.15.4 | none | **Zigbee 3.0 / Thread** |
| Flash | 4 MB | **16 MB** |
| PSRAM | none | **8 MB** |
| Backlight | GPIO21 | **GPIO25** |
| Status light | 3 PWM pins (4/16/17) | **one WS2812 on GPIO27** |
| Touch bus | its own SPI | **shares the display's SPI** |

Vendor board support, including the schematic, is at
[RockBase-iot/NM-CYD-C5](https://github.com/RockBase-iot/NM-CYD-C5).

## Build and install

```bash
pio run -e nm-cyd-c5 -t upload
```

This environment carries **its own `platform` line**. The ESP32-C5 did not
exist when Arduino core 2.0.14 was cut, so it builds on Arduino 3.3.12 /
IDF 5.5.5 while every Xtensa board stays on the pinned espressif32@6.5.0.
Nothing about any other board changes; building every environment in
`platformio.ini` is the check for that.

The board has **two USB-C ports**: the ESP32-C5's native USB, and a CH340
USB-to-UART. Either will flash it, but they are **not** interchangeable for the
serial console, and getting this wrong costs an hour.

This build sets `ARDUINO_USB_CDC_ON_BOOT=0`, so `Serial` is UART0 on GPIO
11/12, which is the **CH340** port. That is the port most people plug into (it
is the one that enumerates as a COM port with the CH340 driver) and the one the
web flasher talks to. Plugged into the *native* port instead, the board runs
perfectly and prints nothing at all, which is indistinguishable from a board
that failed to boot.

If you want the console on the native port, flip that flag in
`boards/nm-cyd-c5.json` to 1 and rebuild.

### Do not flash the merged image at 0x0

`pio run -e nm-cyd-c5 -t upload` is safe and is what you should use. If you
flash by hand with esptool, flash the **four parts**, not
`firmware.factory.bin`:

```
esptool --port COM5 --baud 460800 --chip esp32c5 write-flash \
  0x2000 bootloader.bin  0x8000 partitions.bin \
  0xe000 boot_app0.bin   0x10000 firmware.bin
```

`firmware.factory.bin` is those same four padded into one contiguous image
from 0x0, so it also covers **0x9000–0xe000, which is NVS**. Flashing it wipes
settings, the touch calibration, the PIN and its duress twin, mesh pairings and
Squachy's stats. The symptom is the board running the five-target calibration
again as though it were brand new, which reads as "the calibration did not
save" rather than as "the flash erased it". The four-part write leaves the gap
alone and settings survive.

## Pinout

From RockBase's `Demos/Platformio/nm-cyd-c5/`. Display, touch and SD all share
one SPI bus.

| Device | SCK | MISO | MOSI | CS | DC | RST | BL |
|---|---|---|---|---|---|---|---|
| Display (ST7789) | 6 | 2 | 7 | 23 | 24 | chip RST | 25 |
| Touch (XPT2046) | 6 | 2 | 7 | 1 | — | — | — |
| SD card | 6 | 2 | 7 | 10 | — | — | — |

There is **no touch IRQ line**. The classic CYD wires one to GPIO36; this board
does not have one at all, which is why the environment takes `-DRLPHANTOM_R=1`
for the raw shared-bus touch path rather than the classic CYD's.

RGB LED: one WS2812 on GPIO27 (GRB). I²C is on 8/9. The GPS header is a
low-power UART on 4/5.

> RockBase ship two `pins_arduino.h` variants that **disagree** about I²C:
> `Demos/Platformio/nm-cyd-c5/pins_arduino.h` says SDA 4 / SCL 5, while
> `pinouts/nm-cyd-c5.h` and `connections.md` say SDA 9 / SCL 8. This firmware
> uses no I²C on this board, so it does not matter here — but check the
> schematic before adding a sensor.

## What this board cannot do, and what it does better

**Cannot: Bluetooth Classic.** The C-series has no Classic radio. Today that
costs nothing, because `SKIMMER` detection is a BLE advertised-name match on
every board and BT Classic inquiry is not switched on anywhere (see
[DETECTIONS.md](DETECTIONS.md) — it conflicts with NimBLE on a single radio).
If it ever is, **this board will not be able to run it.** That is silicon, not
software.

**Cannot: an on-device crash backtrace.** RISC-V has no windowed register ABI,
so a backtrace cannot be walked on the device; the IDF stores a raw stack dump
for a host to decode instead. The crash screen shows task, PC, cause and fault
address here, and no frames. Nothing is broken when that section is empty.

**Better: 5 GHz.** Since v1.31.0 (PR #27, by quietradio) this board listens
on 5 GHz as well, which no Xtensa board can do. The 2.4 GHz sweep is left as it
was; after each one the board takes a short 5 GHz slice: every 5 GHz channel
that has been busy lately, plus two quiet ones in turn, so a new access point
turns up within a few sweeps. A slice never runs past 1.5 s, so 2.4 GHz keeps
about three quarters of the radio's time. Which 5 GHz channels it may tune is
up to the radio's country setting, and the boot log says how many it accepted.
5 GHz channels are shown on screen, and **WIFI BANDS** in Settings switches
between `2.4+5` (the default) and `2.4`.

**Still unused: 802.15.4.** The chip can hear Zigbee/Thread traffic; nothing in
the firmware listens for it yet.

## Validation

A box under "Confirmed on the board" is only ticked from the board itself.
It is on the web flasher as a BETA. HUMAN SENSING is not on the ESP32-C5
boards yet.

### Built, not yet run on hardware
- [x] `pio run -e nm-cyd-c5` links a valid ESP32-C5 image
- [x] All 22 environments still build (no Xtensa board regressed)
- [x] `make -C test` — the host suite passes
- [x] `make -C test crypto` — the **real** `meshcrypto.cpp` against the host's
      mbedtls 3 reproduces the golden vectors, and fails under three mutations

### Confirmed on the board
Flashed and captured over the CH340 port on 2026-09-28.

- [x] **esptool identifies it**: `ESP32-C5 (revision v1.0)`, 16MB flash,
      "Wi-Fi 6 (dual-band), BT 5 (LE), IEEE802.15.4, Single Core + LP Core"
- [x] **Boots**, and prints the SquachWatch banner on the CH340 port
- [x] **PSRAM works**: `PSRAM found: yes (8388608 bytes)`. Worth stating
      explicitly because the bootloader prints
      `E MSPI Timing: Failed to allocate dummy cacheline for PSRAM memory barrier!`
      on every single boot. That is
      [arduino-esp32 #12587](https://github.com/espressif/arduino-esp32/issues/12587),
      open since 2026-05-12, present on stock sketches, and nothing to do with
      this firmware. All 8 MB is there afterwards. The print above is how this
      board answers that for itself rather than trusting the issue thread; no
      CYD build asks for PSRAM anyway.
- [x] **No stray GPIO errors.** The first flash produced three per boot --
      `Invalid IO 32 selected`, `perimanGetPinBus(): Invalid pin: 32`,
      `IO 25 is not set as GPIO` -- from the shared pre-init block driving 27
      and 32 HIGH. Fixed with an `NM_CYD_C5` branch that drives only `TFT_BL`,
      and confirmed gone by re-capture.
- [x] **Reaches touch calibration**: `Touch: no five-target calibration yet`,
      which means the display stack initialised and the board is asking for
      input.
- [x] **No ADC errors.** `randomSeed(analogRead(34))` fails here -- GPIO34 does
      not exist on a C5 -- so the seed was 0 and the digital rain opened on the
      same frame every boot. `NM_CYD_C5` now joins the boards that seed from
      `esp_random()`, which is the fix already in the tree for the S3s.
- [x] **The WiFi sniffer is NOT deaf.** This was the one to worry about (see
      the checklist item below, and PR #10): `wifi 581` frames and climbing,
      alongside `ble 69/s` and 1,908 adverts, in a single 75-second capture.
      The `delay(10)` in `detection.cpp` holds on this chip and core.
- [x] **Mesh crypto self-test passes on the device**: `[meshtalk] crypto
      self-test PASS`. That is the mbedtls 2 -> 3 migration confirmed by the
      board itself, against frames built by an independent implementation.
- [x] **Detections fire.** `det 5` in one capture, from ambient traffic alone.
- [x] **Performance**: 67-72 fps, `loop 69/s`, heap flat around 80 KB free
      across the capture, largest block 8.2 MB.
- [x] **SD card mounts and logs**: `[sd] card mounted: 30436 MB` on a 32 GB
      FAT32 card. It needed two fixes -- `SD_CS_PIN` was 5 (this board's card
      is on 10, and GPIO5 here is the GPS header's UART), and `-DRLPHANTOM_R`
      sent it to the branch written for the original CYD's *dedicated* SD bus,
      which runs `SPI.begin(18, 19, 23, 5)`. GPIO23 is this board's `TFT_CS`.
- [x] **Display confirmed by the panel itself.** `RDDPM = 0x9C` (booster on,
      sleep out, normal mode, display on), `RDDCOLM = 0x05` (16 bits/pixel),
      `RDDMADC = 0x60` matching `setRotation(1)` -- read back over MISO, so
      the controller is genuinely initialised rather than presumed so.
- [x] **Touch works, including at 80 MHz SPI**, which is the check that proves
      the clock-source restore holds (see Performance below).
- [x] **80 MHz SPI: picture clean, no tearing or speckles.**

### Still to confirm on the board
Ordered so that a failure early explains the failures after it.

- [ ] **Display**: picture, right way up, correct colours, no inversion
- [ ] **Display**: picture, right way up, correct colours, no inversion
- [ ] **Backlight**: the brightness slider actually dims (proves GPIO25)
- [ ] **Touch**: registers, and lands where you press after calibration
- [ ] **Status light**: the boot sweep runs (proves the WS2812 on 27, and that
      it is not silently doing nothing)
- [ ] **WiFi AP count matches a phone's.** The sniffer is confirmed alive
      above; this is the stricter version -- that it sees as many APs as a
      phone standing in the same place, not merely more than zero. Not
      optional and not obvious: on the ESP32-S3
      a race between `esp_wifi_deinit()` and the sniffer's `esp_wifi_init()`
      brought the driver up **with the receiver off** — scans returned zero
      networks and the sniffer saw no frames while BLE worked perfectly and
      the board's own AP was visible from a phone. The `delay(10)` that fixes
      it is already in `detection.cpp`, but it was tuned on an S3 on core 2.
      This is a new chip on a new core. **Check the AP count against a phone
      standing in the same place.**
- [ ] **BLE scan sees adverts** (any phone with Bluetooth on will do)
- [ ] **Mesh crypto self-test passes**: the console prints
      `[meshtalk] crypto self-test PASS`. This is the on-device proof of the
      mbedtls 2→3 migration.
- [ ] **A real detection fires.** Per [BUILD.md](BUILD.md): any BLE device
      named `HC-05`/`HC-06` trips `SKIMMER`; walking past a Ring/Wyze/Nest/Eufy
      camera trips `CAMERA`; an AirTag trips `AIRTAG`.
- [ ] **SD card**: a FAT32 card gets `squachwatch-<day>.log` written to it
      (proves SD on the shared display bus)
- [ ] **Settings persist** across a power cycle (proves the 16 MB partition
      table kept NVS where the bootloader expects it)
- [ ] **SquachMesh**: two boards see each other. Needs a second device.
- [ ] **OTA**: an update applies and the board still boots (proves both 6 MB
      app slots and the ported ECDSA signature check)
- [ ] **Soak**: left running for an hour without a reset or a heap collapse

## Performance: why it was slow, and what fixed it

As first ported, a full 320x240 frame took **82 ms (12 fps)**. It now takes
**20 ms (49 fps)** on the bench, and the firmware runs at 20 fps with the UI
on top. Everything below was measured on hardware, and the cause was found in
the TRM rather than by trying numbers.

### `SPI_FREQUENCY` does nothing on this path

Built at 20, 26.6, 40 and 80 MHz, the board came up with
`SPI_CLOCK_REG = 0x00002001` every time and the same frame took 88.8, 86.6,
83.2 and 88.3 ms. Two reasons, and the second is the one that wasted time:

- RockBase's C5 processor port names `SPI_FREQUENCY` exactly once, inside
  `initDMA()`. `frame_push.cpp` drives the peripheral registers itself and
  never calls it.
- TFT_eSPI's `begin_tft_write()` calls
  `spi.beginTransaction(SPISettings(SPI_FREQUENCY, ...))`, which **rewrites**
  `SPI_CLOCK_REG`. Anything set before `tft.startWrite()` is overwritten before
  a pixel moves. The divisor has to be set *inside* the transaction.

### Where the 0.343 ms per row actually went

| | per 320-pixel row |
|---|---|
| LUT pixel conversion | 0.011 ms |
| All 16 `SPI_W` register writes (75 ns each) | 0.012 ms |
| **Waiting for the wire** | **0.320 ms** |

93% wire, and the CPU nearly idle — moving the inner loop into IRAM changed
nothing at any clock, twice measured.

### The two knobs, both in the TRM

`f_SPI = f_clk_spi_mst / ((SPI_CLKCNT_N + 1)(SPI_CLKDIV_PRE + 1))` (ch.33.7),
and `PCR_SPI2_CLKM_SEL` (ch.9, bits [21:20] of `PCR_SPI2_CLKM_CONF_REG`)
chooses the module clock: `0 = XTAL_CLK` (the 48 MHz default), `1 =
PLL_F160M_CLK`. The bus ships at N=2 on the crystal: 48/3 = 16 MHz, which is
exactly the 14.9 Mbit/s measured.

Same frame, same loop, divisor set inside the transaction:

| Source | SPI clock | Frame | fps | Wire |
|---|---|---|---|---|
| XTAL, N=3 | 12 MHz | 107.9 ms | 9.3 | 11.4 Mbit/s |
| XTAL, N=2 *(as shipped)* | 16 MHz | 82.2 ms | 12.2 | 14.9 Mbit/s |
| XTAL, N=1 | 24 MHz | 56.6 ms | 17.7 | 21.7 Mbit/s |
| XTAL, N=0 | 48 MHz | 30.9 ms | 32.4 | 39.8 Mbit/s |
| **PLL_F160M/2, N=0** | **80 MHz** | **20.5 ms** | **48.8** | **60.0 Mbit/s** |

Linear in the divisor, which is the proof the wire was the ceiling. PLL/1 with
N=1 is also 80 MHz and gave an identical time, which cross-checks the number.

### The clock source is restored before the push returns

`PCR_SPI2_CLKM_SEL` is **global to SPI2**, and on this board the XPT2046 touch
controller and the SD card sit on that same peripheral. Arduino's
`beginTransaction()` computes its divisors believing the source is the crystal,
so leaving the PLL selected would silently run every later transaction at 3.3x
its requested frequency: touch at ~8 MHz against a rated 2.5 MHz, SD at 13 MHz
instead of 4. Nothing would log an error -- touch would simply start missing
presses. So `frame_push.cpp` raises the clock after `tft.startWrite()` and puts
it back before `tft.endWrite()`. Verified on hardware: picture clean and touch
responsive at 80 MHz.

Two build-time escapes, both in `src/frame_push.cpp`:

- `-DSQW_C5_SPI_PLL=0` stays on the 48 MHz crystal. Still 3x stock, and the
  fallback if a panel dislikes 80 MHz.
- `-DSQW_C5_SPI_DIV=n` divides the module clock by `n+1`.

Too fast shows as torn or speckled rows, never a blank screen.

### What is the limit now

`bg 20.0 ms` against `push 19.8 ms`: the digital-rain background now costs as
much as the whole SPI transfer. Past this point the work is in the renderer,
on a single core, not on the bus.

## Deliberately not claimed
- Display overclock. There is no `-fast` variant for this board on purpose:
  an out-of-spec panel clock is a poor thing to discover on hardware nobody
  has soaked. The vendor's own setup asks for 20 MHz; this build uses it.
