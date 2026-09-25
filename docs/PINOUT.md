# Pinout — ESP32-2432S028R ("CYD")

Pin assignments used by SquachWatch-CYD. Verified against the most
common Sunton "USB-C" revision of the board. Other CYD revisions
may differ — check your board's silkscreen and the
[witnessmenow/ESP32-Cheap-Yellow-Display](https://github.com/witnessmenow/ESP32-Cheap-Yellow-Display)
repo for variants.

## TFT (ILI9341 / ILI9342C) — VSPI bus

| Signal | GPIO |
|---|---|
| MOSI   | 27   |
| SCLK   | 14   |
| CS     | 15   |
| DC     |  2   |
| RST    |  4   |
| BL (backlight, PWM) | 21 |

## Touch (XPT2046) — HSPI bus

| Signal | GPIO |
|---|---|
| CS     | 33   |
| IRQ    | 36   |
| MOSI   | 32   |
| MISO   | 39   |
| SCLK   | 25   |

## SD card — VSPI bus (shared with TFT)

| Signal | GPIO |
|---|---|
| CS     |  5   |
| MOSI   | 23   |
| MISO   | 19   |
| SCLK   | 18   |

The SD card shares `MOSI` and `SCLK` with the TFT. The firmware
selects between them via their respective `CS` lines. The SD card
is optional — if no card is inserted at boot, the firmware just
skips SD logging and everything else works.

## Unused GPIOs (free for future use)

GPIO 0, 1, 3, 12, 13, 16, 17, 22, 26, 34, 35, 37, 38 are exposed
on the CYD's GPIO header but not used by SquachWatch-CYD v1.0.

## Freenove ESP32-S3 Display 2.8" (FNK0104B) — `[env:freenove-s3]`

Not a CYD, though the screen is a CYD's: an **ESP32-S3R8** (8 MB octal PSRAM
in the package, 16 MB flash beside it) behind a 240x320 ILI9341, capacitive
touch, and a USB-C that is the S3's own USB. Pins from Freenove's sketches and
TFT_eSPI setup for the board, checked against its schematic, and confirmed on
an FNK0104B. The FNK0104A is the same board without the touch panel; it should
run with no touch (the probe finds nothing and says so) but has not been tried.

### TFT (ILI9341) — SPI

| Signal | GPIO |
|---|---|
| MOSI   | 11   |
| SCLK   | 12   |
| MISO   | 13   |
| CS     | 10   |
| DC     | 46   |
| RST    | — (power-on reset) |
| BL (backlight, PWM, HIGH = on) | 45 |

40 MHz is this panel's ceiling: 80 MHz garbles it, on HSPI and on FSPI's own
IO_MUX pins (which 10-13 are) alike.

### Touch (FT6336) — I2C, address 0x38

| Signal | GPIO |
|---|---|
| SDA    | 16   |
| SCL    | 15   |
| RST    | 18 (active low) |
| INT    | 17 (not used; the driver polls) |

The ES8311 audio codec shares the bus at 0x18.

### SD card — 4-bit SDMMC (not SPI)

| Signal | GPIO |
|---|---|
| CLK    | 38   |
| CMD    | 40   |
| D0     | 39   |
| D1     | 41   |
| D2     | 48   |
| D3     | 47   |

10K pull-ups on the board. The firmware mounts 4-bit and falls back to 1-bit.

### Everything else

| What | GPIO |
|---|---|
| Status light — one WS2812 | 42 |
| Battery — 200K/200K divider from the cell | 9 (ADC1) |
| BOOT button | 0 |
| ES8311 audio (I2S) — not used | MCK 4, BCK 5, WS 7, DOUT 8, DIN 6 |

The battery row reads the cell's voltage and a percent from it. There is no
fuel gauge, and the charger's CHRG line drives an LED rather than a GPIO, so
it cannot say "charging" -- on USB it reads the cell under charge, a little
high.

Not usable on an S3: GPIO 26-32 (flash/PSRAM) and 33-37 (octal PSRAM).

## Sources

- [witnessmenow/ESP32-Cheap-Yellow-Display](https://github.com/witnessmenow/ESP32-Cheap-Yellow-Display) (MIT)
- [Fr4nkFletcher/ESP32-Marauder-Cheap-Yellow-Display](https://github.com/Fr4nkFletcher/ESP32-Marauder-Cheap-Yellow-Display)
- Sunton schematic for the ESP32-2432S028R (publicly available in
  the witnessmenow wiki).
- [Freenove/Freenove_ESP32_S3_Display](https://github.com/Freenove/Freenove_ESP32_S3_Display):
  the FNK0104 sketches, TFT_eSPI setups and the 2.8" schematic.
