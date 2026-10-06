# AIPI Lite validation

Local checks on 2026-10-06 against upstream `6ef18479650b81945f4531951e69d3e66e1cfaff`
(v1.32.0), branch `feature/aipi-lite-support`. Implementation commit:
`507ef54` (later documentation commits do not change firmware behavior).

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
RAM report. PSRAM is 8 MB by target configuration; its actual size/readback
still requires device evidence.

The host lacked g++; Fedora's matching GCC 16 C++ frontend and headers were
extracted under `/tmp` and used through a local wrapper, without system
installation. Host tests and emulator used that compiler. Firmware used the
normal pinned PlatformIO toolchain. One overlapping PlatformIO invocation
caused an AWOK `.sconsign314.tmp` bookkeeping error; its isolated rerun passed.
Subsequent PlatformIO builds are run sequentially.

## Device status

Andy authorized flashing the connected AIPI on 2026-10-06. The candidate port
is `/dev/ttyACM0` (Espressif USB JTAG/serial). Initial access was denied by Linux
permissions. **No backup, erase or flash has occurred yet.** Hardware identity,
PSRAM, display, button, LED, both radios, battery operation and recovery remain
pending. Follow the acceptance checklist in [AIPI_LITE.md](AIPI_LITE.md).
