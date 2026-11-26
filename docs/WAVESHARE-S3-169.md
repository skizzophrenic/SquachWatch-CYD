# Waveshare ESP32-S3-Touch-LCD-1.69 — BRING-UP

A small 1.69" ESP32-S3 board from Waveshare. It builds as a generic S3 board
(`SQW_S3`, the same route as the Freenove S3) plus `WS_S3_169` for what
differs. **Compiles; not yet confirmed on hardware.**

| | |
|---|---|
| Module | ESP32-S3R8: 16 MB flash, 8 MB **octal** PSRAM |
| Panel | ST7789V2, 240×280, SPI (20-row offset, inversion on) |
| Panel pins | MOSI 7, SCLK 6, CS 5, DC 4, RST 8 |
| Backlight | GPIO15 (HIGH is on), LEDC-dimmed |
| Touch | CST816T, I2C 0x15: SDA 11, SCL 10, RST 13, INT 14 (polled) |
| Battery | ETA6098 charger; cell → 200K/100K divider → GPIO1 (×3) |
| Power latch | SYS_EN GPIO41 (held HIGH at boot), power key sense GPIO40 |
| Not used yet | buzzer (GPIO42), QMI8658 IMU, PCF85063 RTC |
| SD card / status light | none |
| USB | native only (USB Serial/JTAG) |

Pins are from Waveshare's own
[`board_config/pin_config.h`](https://github.com/waveshareteam/ESP32-S3-Touch-LCD-1.69)
and their ESP-IDF ST7789 demo.

## Build and install

```bash
pio run -e ws-s3-169 -t upload
```

One USB-C port, the S3's own, and the build sets `ARDUINO_USB_CDC_ON_BOOT=1`,
so the serial console is that port too (`pio device monitor -e ws-s3-169`). If
an upload will not start, hold **BOOT**, plug in (or tap the power key),
release BOOT, and try again.

## First boot

- The screen starts in **portrait** (240×280). ROTATE in Settings turns it to
  280×240 landscape.
- The five-target **touch calibration** runs on first boot; tap each target.
- If the colours look inverted, toggle **INVERT** in Settings (the panel's
  inversion is Waveshare's default, but has not been seen here yet).
- **BATTERY** is on the SYSTEM page.

## Known caveats

- **Board revision.** Waveshare moved the power latch / key / buzzer from
  GPIO35/36/33 (first revision) to 41/40/42. GPIO33–37 are octal-PSRAM lines
  on the S3R8, so this build only drives **GPIO41**. On a first-revision board,
  run it from USB: on the battery it will switch off when the power key is
  let go.
- The panel's corners are rounded, so the extreme corners of the title bar are
  clipped by the glass.
- Frames go out through `pushSprite()` (like the T-Watch), not the DMA frame
  push the Freenove S3 uses; it can be switched over once measured.
- At 40 MHz SPI (Waveshare's own rate). Try `-DSPI_FREQUENCY=80000000` for a
  faster screen if the panel holds it.
