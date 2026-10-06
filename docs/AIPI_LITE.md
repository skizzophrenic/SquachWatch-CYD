# AIPI Lite (experimental)

The `aipi-lite` environment targets the XORIGIN / AIPI Lite **XY006PL01**
configuration documented by the community: ESP32-S3, 16 MB flash, 8 MB octal
PSRAM, ST7735-compatible 128x128 SPI LCD, one usable function button and one
WS2812. Initial USB bring-up passed on one connected unit, but this is not a fully
validated release. No PCB revision
has been verified here; check the label, flash capacity and memory configuration
of your unit before replacing its firmware. Other AIPI models are not covered.

## Hardware evidence

Inspected on 2026-10-06:

- [AIPI product page](https://aipi.com/products/aipi-lite-device-no-battery)
  and [vendor manual](https://aipi.com/pages/manual): product identity and controls;
  no complete vendor schematic was located.
- [sticks918 ESPHome configuration](https://github.com/sticks918/AIPI-Lite-ESPHome/blob/b5bd8b274130fb57d2779bbd6ffaecaf2b5c078c/aipi.yaml):
  traced GPIO assignments, 16 MB flash, inverted ST7735, GPIO42 active-low
  button and GPIO46 GRB WS2812. Its README distinguishes the left power/wake
  control from the right GPIO button and explains internal BOOT access.
- [bcarroll hardware specification](https://github.com/bcarroll/aipi-lite/blob/28e6bd1f73572386952a831995434ad64cfdcd2e/SPEC.md),
  [display implementation](https://github.com/bcarroll/aipi-lite/blob/28e6bd1f73572386952a831995434ad64cfdcd2e/src/lib/display.py),
  [pin constants](https://github.com/bcarroll/aipi-lite/blob/28e6bd1f73572386952a831995434ad64cfdcd2e/src/lib/pins.py),
  and [driver](https://github.com/bcarroll/aipi-lite/blob/28e6bd1f73572386952a831995434ad64cfdcd2e/src/lib/st7735/__init__.py):
  matching pins, OPI PSRAM, 20 MHz SPI, RGB order, zero-offset red-tab
  initialization and MicroPython rotation 1. That project's specification
  records a physical display run and its firmware plan records an I/O probe.
- [Robert Lipe's teardown](https://www.robertlipe.com/aipi-lite-ai-robot/):
  chip readout showing ESP32-S3 and 8 MB embedded PSRAM, stock telemetry
  reporting 16 MB flash, and a corroborating GPIO table. Its early backup
  example reads only 4 MB; **use the full 16 MB size below** for this target.
- [stock startup log](https://gist.github.com/0xD34D/761db04261df5276ab78f9b350c68195):
  additional context for GPIO10 power control and GPIO21 charging pulses.

These are community pin findings, not a vendor guarantee for every revision.

| Function | GPIO | Port behavior |
| --- | --- | --- |
| LCD SCLK | 16 | SPI output, 20 MHz |
| LCD MOSI | 17 | SPI output; no MISO |
| LCD CS | 15 | Active low |
| LCD D/C | 7 | Display command/data |
| LCD reset | 18 | Active low |
| LCD backlight | 3 | PWM, 5 kHz, conservative 25% duty |
| Right function button | 42 | Active low, pull-up, 25 ms debounce |
| WS2812 data | 46 | One GRB pixel, existing status-light driver |
| USB D-/D+ | 19 / 20 | Native USB CDC; never reused for SD |
| Audio I2C SCL/SDA | 4 / 5 | Deferred |
| Audio MCLK/DOUT/WS/DIN/BCLK | 6 / 11 / 12 / 13 / 14 | Deferred |
| Speaker enable | 9 | Untouched |
| Board power hold | 10 | Active high, asserted first at boot to retain battery power |
| Battery voltage ADC | 2 | Calibrated ADC1, 11 dB; stock voltage multiplier 2.5 |
| USB voltage ADC | 8 | Stock USB-present threshold 1300 mV at ADC |
| Charger status | 47 | Active low, qualified by USB voltage |
| Battery presence pulse input | 21 | Rising edges; stock treats six pulses as no battery |

The LCD's exact panel variant is not established. The port follows the
MicroPython red-tab setup with zero offsets and inversion off. ESPHome uses
inversion on; `INVERT` on the console toggles it and persists the choice. The
MicroPython driver's rotation 1 writes MADCTL `0x60`; **TFT_eSPI rotation 3**
writes the same bits. Copying the rotation number alone would turn the picture
the other way. Diagnostics includes RGB bars and a one-pixel outer border to
check order, polarity, orientation and offsets. All GPIO assignments live
in `include/aipi_lite_user_setup.h`; adjust that header if hardware proves a
revision difference.

## Implementation and scope

The environment inherits the pinned upstream ESP32 platform and libraries,
selects `qio_opi`, 80 MHz QIO flash, native USB CDC and the existing 16 MB
`partitions_twatch.csv` layout. It keeps normal NimBLE observer/broadcaster
flags and `SQUACH_MESH`, which shared upstream modules require. Default build
and release/flasher lists are unchanged; build CI adds only compile coverage.

`SQW_MINI` selects `MiniScanner::begin/tick` at the existing setup/loop entry
points. Existing detection/signature code is unchanged. The mini runtime
reuses settings, palette/type colors, RAM log, privacy masking, ignore/snooze
and confidence gates, flood suppression and the production Wi-Fi/BLE engine.
`SQW_WS2812_PIN` extends the existing status light; `SQW_NO_SD` prevents SD
initialization from touching unrelated pins. There are no AIPI-name branches
in application code.

Startup reports PSRAM size and performs a 1 KB external allocation/write/read
probe. The 128x128 RGB565 sprite uses TFT_eSPI's PSRAM allocator
(`CONFIG_SPIRAM_SUPPORT=1`), leaving internal radio memory available. A failed
probe or framebuffer allocation leaves an error on screen and does not start
radios. Wi-Fi promiscuous reception/channel hopping and NimBLE scanning then
run through the same engine as the other boards.

This first interface deliberately exposes a scanner subset. It has no full
settings/PIN/lock interface, OTA UI, mascot, raw scanner, mesh messaging UI or
automatic power/sleep management. Mesh preferences default off on fresh settings. This
port does not initialize BlackBox or a filesystem. Detection lifetime counters
and normal upstream settings still use NVS. Audio and LittleFS are follow-on
work. Firmware updates for this milestone use USB.

## Compact interface

- **Tap:** cycle scan summary, readable RAM log, dense RAM log and radio/memory diagnostics.
- **Hold 700 ms on the log:** advance to the next older row, wrapping around.
- **Dense log:** eleven rows at 8-pixel pitch. Each shows a nine-character
  name/vendor/type, last detection stamp and latest RSSI in dBm. Hold advances
  eleven entries at a time, wrapping at the end. Time follows the existing
  clock formatter: wall time when trusted, otherwise minutes:seconds since
  boot, changing to hours/minutes or days/hours for long uptimes. It retains the upstream
  newest-created order; repeat sightings update time/RSSI in place. Detection
  cards do not cover this page; status-light alerts continue.
- **Hold elsewhere:** return home. A held release never also cycles pages.
- Allowed new sightings show a five-second type/vendor/RSSI card and use the
  existing status-light alert colors. A tap or hold dismisses the card.
- Console at nominal 115200: `LOG` dumps all RAM log entries; `RADIO` reports
  current reception and radio state; `LED` runs the existing light test;
  `INVERT` flips LCD polarity; `BATTERY` prints measured voltage, USB ADC, pulse count and charge input. End each command with a newline.
- Serial also reports the latest changed sighting and radio counters every
  ten seconds. A burst can replace `latest()` between frames, as in the full
  UI; use `LOG` to inspect all retained rows. No fabricated detections are
  injected by this build.

## Battery indicator and evidence

The upper-right battery shows one/two/three bars for low/mid/full and a
lightning bolt while actively charging. An unknown or absent battery shows
`?`. These are **coarse voltage bands**, not a fuel-gauge percentage or an
estimated runtime. The stock linear 3000–4200 mV scale supplies the 20%/80%
voltage boundaries (3240/3960 mV); 60 mV hysteresis avoids flickering bars.
Eight calibrated samples are averaged once per second, outside interrupts.

The saved, verified stock firmware on the tested unit establishes the pin
mapping more precisely than the teardown's tentative GPIO table. In its
`YuanZhiESP32S3` constructor at `0x420308e0`, the `CustomPm` arguments are
21 (presence pulse), 47 (charge status), 8 (USB ADC), 10 (power control),
2 (battery ADC), and 1 (power-button ADC). The derived vtable at `0x3c212258`
retains the base battery/charge methods. Battery conversion at `0x4202b9ec`
multiplies the calibrated ADC millivolts by 2.5, clamps at 3000/4200 mV and
linearly maps the interval. `0x4202b99c` tests GPIO47 low for charging;
`0x4202b974` uses the 1300 mV USB threshold. `0x4202c2d0` transitions to
`NoBattery` after six rising GPIO21 edges. Public stock
[boot logs](https://gist.github.com/0xD34D/761db04261df5276ab78f9b350c68195)
corroborate ADC GPIO2/1/8 and the GPIO21 pulse input. No proprietary code or
stock images are included in this patch; these addresses document observations
from the privately retained backup and may differ across stock revisions.

GPIO10 is asserted high **before serial, display or radio startup** to retain
battery power when USB is unplugged and after the physical power button is
released. The stock `CustomPm` power-on routine at `0x4202b91c` configures
GPIO10 as an output and sets it high; the community
[voice-bridge configuration](https://github.com/noise754/AIPI-Lite-Voice-Bridge/blob/main/aipi.yaml)
also keeps `board_power` GPIO10 always on. The earlier port omitted this latch;
it could scan over USB but lost power immediately when USB was removed.

To start from the battery alone, use the **left/power button**, held for about
three seconds as described in the [vendor manual](https://static.aipi.com/AIPI_InstructionBook/AIPI_InstructionBook.html).
The right function button changes scanner pages and cannot start an unpowered
processor. USB insertion should start the scanner automatically. This milestone
keeps the scanner running continuously; it does not implement the stock
power-button shutdown or five-minute standby timeout.

This port observes the battery inputs; it does not reproduce the stock shutdown
state machine. The pulse count is checked in one-second windows, and invalid
voltage is also treated as unknown. Charge bars measure terminal voltage and
can change under radio load. Validation on the connected module measured
4190 mV, USB ADC 1976–1977 mV, no presence pulses, and GPIO47 low (charging).
Charging-to-full, unplug and a depleted module still need physical checks after the power-latch fix.

## Build, backup, flash and recover

Install PlatformIO as described in [BUILD.md](BUILD.md). From the checkout:

```sh
pio run -e aipi-lite
```

Expected output is `.pio/build/aipi-lite/firmware.bin`, plus the matching
bootloader and partition images. **Do not flash firmware.bin alone at offset
zero**; let PlatformIO use the ESP32-S3 offsets.

Before the first replacement, back up the complete stock flash. Automatic
loader entry through native USB worked on the connected unit: try `flash-id`
first without opening the case. If automatic entry fails, use this fallback:
with the battery disconnected, remove the four rear screws, hold the internal BOOT
button and connect a USB data cable. The community procedure places BOOT under
the display / near the ESP32-S3 module; the right function button is not BOOT.
Release BOOT after connection. The display should remain black. Identify the
new serial port (`pio device list`); do not select an unrelated connected board.
For the commands below replace `PORT` with that port (for example `COM3` or
`/dev/ttyACM0`). Use esptool 5.x for backup/recovery. The old 4.5.1 stub bundled with the
pinned PlatformIO platform reads this device extremely slowly over native USB
([Espressif issue #936](https://github.com/espressif/esptool/issues/936)). This
does not require changing the firmware platform. These commands use 5.x syntax:

```sh
python -m pip install 'esptool>=5.4,<6'
python -m esptool --chip esp32s3 --port PORT flash-id
python -m esptool --chip esp32s3 --port PORT read-flash 0 0x1000000 aipi-stock.bin
python -m esptool --chip esp32s3 --port PORT read-flash 0 0x1000000 aipi-stock-check.bin
```

Confirm detected flash is 16 MB, both files are exactly **16,777,216 bytes**,
and their SHA-256 hashes match. Keep verified backups outside the checkout:
they can contain Wi-Fi credentials and device provisioning. Do not erase or
flash before that verification. Keep the saved stock image for this same unit;
firmware flash does not restore eFuses.

For the **first installation**, erase stock application/settings only after
backup, then write this target:

```sh
python -m esptool --chip esp32s3 --port PORT erase-flash
pio run -e aipi-lite -t upload --upload-port PORT
```

Subsequent SquachWatch uploads normally omit the erase so NVS settings survive.
Upload uses nominal 115200 because switching baud on native USB is unreliable
on the other upstream S3 targets. Close any serial monitor before uploading.
Release BOOT and reconnect USB if the device remains in the ROM loader. The
application USB port may re-enumerate under a different name:

```sh
pio device monitor --port PORT --baud 115200
```

A blank/wrong-color screen does not prove the radios failed: read the console,
try `INVERT`, and inspect RGB bars/border on diagnostics. If no application port
appears, re-enter the internal BOOT procedure and upload again. Do not guess
power-control GPIO changes to fix it.

To recover stock, enter BOOT mode and restore the verified **same-unit** image:

```sh
python -m esptool --chip esp32s3 --port PORT write-flash 0 aipi-stock.bin
```

This writes the full saved flash, including its original partition table and
settings. Release BOOT and power-cycle. No stock image is distributed here.

## Hardware acceptance before upstream release

Local compile/test results are in [AIPI_LITE_VALIDATION.md](AIPI_LITE_VALIDATION.md).
Still required on a confirmed XY006PL01:

1. Record device label/PCB revision, flash ID, PSRAM size and PASS readback.
2. Check the complete border and red/green/blue bars, readable upright text,
   backlight and correct inversion; record any header changes.
3. Exercise repeated taps, holds, log wrapping and held-release suppression.
4. Run `LED`; verify its RGB order, then verify an actual detection's light.
5. Use `RADIO` to observe increasing Wi-Fi frames and BLE advert reception.
   Exercise known authorized Wi-Fi/BLE signature sources separately and
   compare the UI to `LOG`. Quiet rooms are not proof of deaf radios.
6. Soak with both radios active, inspect heap/counters, reconnect the console,
   reset, and test battery operation. Battery runtime has not been measured.
7. Verify BOOT-mode reflash and same-unit stock restore before recommending
   the device to others.

No PR should describe the port as hardware tested until this evidence exists.
