# AIPI Lite (experimental)

The `aipi-lite` environment targets the XORIGIN / AIPI Lite **XY006PL01**
configuration documented by the community: ESP32-S3, 16 MB flash, 8 MB octal
PSRAM, ST7735-compatible 128x128 SPI LCD, one usable function button and one
WS2812. It is a bring-up port, not a hardware-verified release. No PCB revision
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
| Board power control | 10 | Untouched; not sufficiently characterized |
| Charge pulse input | 21 | Untouched; no invented battery percentage |

The LCD's exact panel variant is not established. The port follows the
MicroPython red-tab setup with zero offsets and inversion off. ESPHome uses
inversion on; `INVERT` on the console toggles it and persists the choice. The
MicroPython driver's rotation 1 writes MADCTL `0x60`; **TFT_eSPI rotation 3**
writes the same bits. Copying the rotation number alone would turn the picture
the other way. Diagnostics includes RGB bars and a one-pixel outer border to
check order, polarity, orientation and offsets. All eight GPIO assignments live
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
power/sleep management. Mesh preferences default off on fresh settings. This
port does not initialize BlackBox or a filesystem. Detection lifetime counters
and normal upstream settings still use NVS. Audio and LittleFS are follow-on
work. Firmware updates for this milestone use USB.

## Compact interface

- **Tap:** cycle scan summary, RAM log and radio/memory diagnostics.
- **Hold 700 ms on the log:** advance to the next older row, wrapping around.
- **Hold elsewhere:** return home. A held release never also cycles pages.
- Allowed new sightings show a five-second type/vendor/RSSI card and use the
  existing status-light alert colors. A tap or hold dismisses the card.
- Console at nominal 115200: `LOG` dumps all RAM log entries; `RADIO` reports
  current reception and radio state; `LED` runs the existing light test;
  `INVERT` flips LCD polarity. End each command with a newline.
- Serial also reports the latest changed sighting and radio counters every
  ten seconds. A burst can replace `latest()` between frames, as in the full
  UI; use `LOG` to inspect all retained rows. No fabricated detections are
  injected by this build.

## Build, backup, flash and recover

Install PlatformIO as described in [BUILD.md](BUILD.md). From the checkout:

```sh
pio run -e aipi-lite
```

Expected output is `.pio/build/aipi-lite/firmware.bin`, plus the matching
bootloader and partition images. **Do not flash firmware.bin alone at offset
zero**; let PlatformIO use the ESP32-S3 offsets.

Before the first replacement, back up the complete stock flash. With the
battery disconnected, remove the four rear screws, hold the internal BOOT
button and connect a USB data cable. The community procedure places BOOT under
the display / near the ESP32-S3 module; the right function button is not BOOT.
Release BOOT after connection. The display should remain black. Identify the
new serial port (`pio device list`); do not select an unrelated connected board.
For the commands below replace `PORT` with that port (for example `COM3` or
`/dev/ttyACM0`). Use esptool 4.x for this underscore command syntax:

```sh
python -m pip install 'esptool>=4.5,<5'
python -m esptool --chip esp32s3 --port PORT flash_id
python -m esptool --chip esp32s3 --port PORT read_flash 0 0x1000000 aipi-stock.bin
python -m esptool --chip esp32s3 --port PORT read_flash 0 0x1000000 aipi-stock-check.bin
```

Confirm detected flash is 16 MB, both files are exactly **16,777,216 bytes**,
and their SHA-256 hashes match. Keep verified backups outside the checkout:
they can contain Wi-Fi credentials and device provisioning. Do not erase or
flash before that verification. Keep the saved stock image for this same unit;
firmware flash does not restore eFuses.

For the **first installation**, erase stock application/settings only after
backup, then write this target:

```sh
python -m esptool --chip esp32s3 --port PORT erase_flash
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
python -m esptool --chip esp32s3 --port PORT write_flash 0 aipi-stock.bin
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
