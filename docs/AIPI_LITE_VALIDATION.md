# AIPI Lite validation

Local checks on 2026-10-06 against upstream `6ef18479650b81945f4531951e69d3e66e1cfaff`
(v1.32.0), branch `feature/aipi-lite-support`. Initial implementation commit: `507ef54`. The subsequent bring-up change
reports usable PSRAM in KB and adds the device evidence below.

| Check | Result |
| --- | --- |
| `pio run -e aipi-lite` | PASS, ESP32-S3 image generated |
| `pio run -e cyd` | PASS |
| `pio run -e cyd-ili9341` | PASS |
| `pio run -e awok` | PASS when run independently |
| `pio run -e freenove-s3` | PASS |
| `make -C test` | PASS, 43 test programs including the new single-button test |
| `make -C sim` | PASS, CLI and live emulator |
| Emulator CLEAR smoke render, 60 frames | PASS, 320x240 PNG produced |
| Mini UI layout inspection | Scan, log, diagnostics and alert pages rendered at 128x128 with existing TFT shims; readable/clipped to border |
| `git diff --check` | PASS |

The mini layout preview used the actual drawing function extracted into a
local harness, the upstream simulated detection engine and seeded sample data.
The shim lacks `drawString`, so its equivalent cursor/font/print path was used.
This validates layout only, not physical LCD polarity, offsets or RF reception.

Toolchain: PlatformIO 6.2.0, espressif32 6.5.0, Arduino ESP32 2.0.14,
Xtensa ESP32-S3 GCC 8.4.0. Libraries resolved to TFT_eSPI 2.5.43,
NimBLE-Arduino 2.5.1, XPT2046 1.4.0 (`f956c5d`). Existing no-touch TFT and
framework warnings remain. No compiler errors were accepted as passing.

AIPI size report: **60,460 bytes static RAM** of 327,680; **1,013,141 bytes
flash** of the 4,194,304-byte application slot. The 32 KB RGB565 framebuffer
and radio allocations are runtime memory and are not included in the static
RAM report. PSRAM is 8 MB by target configuration; its actual usable size/readback
is recorded below.

The host lacked g++; Fedora's matching GCC 16 C++ frontend and headers were
extracted under `/tmp` and used through a local wrapper, without system
installation. Host tests and emulator used that compiler. Firmware used the
normal pinned PlatformIO toolchain. One overlapping PlatformIO invocation
caused an AWOK `.sconsign314.tmp` bookkeeping error; its isolated rerun passed.
Subsequent PlatformIO builds are run sequentially.

## Device bring-up

Andy authorized flashing the attached unit on 2026-10-06 and reported the
interface working well after installation. Native USB identified an ESP32-S3
revision 0.2, 16 MB quad flash and 8 MB embedded PSRAM (AP_3v3). Secure boot and
flash encryption are disabled. Linux serial access needed a per-port ACL; no
system group or global configuration was changed.

Two independent full stock reads with esptool 5.4.0 produced 16,777,216-byte
images with identical SHA-256 hashes. The old 4.5.1 stub was too slow for native
USB reads; switching the backup tool solved it. Verified recovery images and
raw serial logs are stored privately outside the repository. Flash erase and
PlatformIO upload both succeeded; the uploader verified written hashes.

Serial evidence after installation:

- PSRAM: 8,386,295 usable bytes, external allocation/write/read **PASS**.
- 128x128 RGB565 framebuffer allocated; both radios initialized.
- At 5 seconds: 38 Wi-Fi frames and 281 BLE advertisements.
- At 40 seconds: 388 Wi-Fi frames and 3,051 BLE advertisements; BLE scanning
  active and Wi-Fi sniffer on with advancing channels.
- `LOG` returned four real RAM-log records at 20 seconds. No synthetic
  detections were injected. These are ambient matches, not verified fixtures.
- Physical right-button taps produced page 1, page 2 and page 0 serial events.
- `LED` was sent to exercise the existing light test. Andy reported the first
  pass working; exact RGB order/border and long-hold behavior still need
  individually recorded observations before declaring the whole checklist done.

Diagnostics reports usable PSRAM in KB rather than truncating the usable-byte
count to 7 MB. Chip-reported installed capacity is 8 MB; allocator metadata
makes the usable count slightly smaller.

PCB/model-label identification, controlled signature-fixture checks, extended
soak/battery runtime and actual stock restore remain unverified. BOOT/recovery
instructions are documented, but a backup is not proof of a completed restore.
The target stays experimental and outside release/flasher publishing.

## Battery and dense log follow-up

- Added eleven-row log page with lastSeen/RSSI, hold-to-page, and uninterrupted
  reading while alerts continue on the LED. Rendered the real drawing code
  with fifteen simulated records; all eleven rows and header/footer fit.
- Verified stock CustomPm ADC/divider/charge mapping (details in AIPI_LITE.md).
- Flashed battery/dense build with image hash verification. Real readings:
  battery 4190 mV, USB ADC 1976–1977 mV, GPIO47 low, zero GPIO21 pulses.
  Icon reports active charging; Wi-Fi hopping and BLE scanning remain active.
- Battery boundary, hysteresis, missing-module and USB qualification host tests
  pass, together with the original host suite.
- Low/mid/full transition and charging completion remain physical test items.
