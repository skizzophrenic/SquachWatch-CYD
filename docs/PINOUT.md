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

## DIYmalls 2.8" — JC2432W328C

The JC2432W328C uses the shared `cyd` / `cyd-fast` firmware, but its
touchscreen and backlight wiring differ from the original resistive CYD.
The display pins are the ST7789 CYD pins above; the touchscreen and
backlight below are the JC2432W328C assignments used by the firmware. See
[the board guide](DIYMALLS-28.md) for support and battery limitations.

### Capacitive touch — CST816/CST820 family, I²C

| Signal | GPIO / value |
|---|---:|
| SDA | 33 |
| SCL | 32 |
| Reset | 25 |
| I²C address | `0x15` |
| Interrupt | Not used by firmware |

At startup, `cyd` and `cyd-fast` probe this I²C address. An answering
controller selects capacitive touch; otherwise the firmware initializes the
original CYD's XPT2046 resistive touchscreen on its separate SPI bus.

### Backlight

| Signal | GPIO |
|---|---:|
| Backlight, active-high PWM | 27 |

This replaces GPIO21 for the JC2432W328C only. The original CYD continues to
use GPIO21.

### microSD

The shared CYD firmware configures its SD card on the classic CYD's separate
SPI bus: SCK 18, MISO 19, MOSI 23, CS 5. The board has a microSD slot, but
mounting/logging on all JC2432W328C revisions still needs physical
verification.

### Battery, USB and other connectors

The board has a two-pin 1.25 mm BAT connector and a dedicated battery power
switch. The connector polarity and any battery-sense connection have not
been established from a JC2432W328C schematic or measurement. Do not copy the
Freenove S3 battery divider on GPIO9: GPIO6–11 are reserved for flash on this
classic ESP32. There is no JC2432W328C battery monitor in the current
firmware, and USB charging is not verified. A protected 1S LiPo only adds
cell protection; that is not evidence that the board provides compatible
charge management. Details are in [DIYMALLS-28.md](DIYMALLS-28.md).

The USB-C connector provides the board's USB/data connection and 5 V input.
The board also has a two-pin speaker connector, but its mapping and electrical
requirements have not been verified for this model; the shared CYD firmware
does not drive it.

## Spare GPIOs

The spare pins on the connectors are GPIO 22 and 35. GPIO 27 on CN1 is
also free, but the firmware drives it high at boot. GPIO 35 is input only.

The pins an older version of this list called unused are in use:

| GPIO | Used for |
|---|---|
| 0 | BOOT button |
| 1, 3 | USB serial console |
| 4, 16, 17 | status light (RGB LED) |
| 12, 13 | display bus |
| 21 | backlight |
| 26 | speaker |
| 34 | light sensor |
| 37, 38 | not on the connectors |

## GPS module (cyd-gps builds)

The `cyd-gps` and `cyd-ili9341-gps` builds read an ATGM336H GPS module at
9600 baud. Wire it like this:

| Module | CYD |
|---|---|
| TX  | GPIO 35 on P3 |
| VCC | 3.3V on CN1 |
| GND | GND on CN1 |
| RX  | not connected |

The module's RX is not connected, so the firmware cannot configure the
module. It runs on the module's defaults.

To check the build with no module, open the serial console at 2,000,000 baud:

- `GPS FAKE lat lon` sets a bench fix, for example `GPS FAKE -33.857 151.215`.
  The screens and the SD rows mark it FAKE, and it never sets the clock.
- `GPS STATUS` prints the sentence counts, the satellites and the position.
- `GPS OFF` clears the bench fix.

Wardriving: switch on WARDRIVE in Settings, or type `WARDRIVE ON` on the
console. With a real fix, the board writes every network and Bluetooth device
it hears to `/wigle-YYYYMMDD-HHMM.csv` on the SD card, named for the start of
the session in UTC. Upload that file to wigle.net as it is. A bench fix writes
nothing. Wardriving pauses when memory runs low, and detection keeps running.

## Freenove ESP32-S3 Display 2.8" (FNK0104B) — `[env:freenove-s3]`

Not a CYD, though the screen is a CYD's: an **ESP32-S3R8** (8 MB octal PSRAM
in the package, 16 MB flash beside it) behind a 240x320 ILI9341, capacitive
touch, and a USB-C that is the S3's own USB. Pins from Freenove's sketches and
TFT_eSPI setup for the board, checked against its schematic, and confirmed on
an FNK0104B. The FNK0104A is the same board without the touch panel; it should
run with no touch (the probe finds nothing and says so) but has not been tried.

The 4.0" (FNK0104S, `[env:freenove-s3-40]`) is this board with a 320x480
ST7796 in the ILI9341's place: every pin below is the same, per Freenove's
sketches, except that its TFT_eSPI setup leaves MISO undefined.

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
| ES8311 audio (I2S), the speaker | MCK 4, BCK 5, WS 7, DOUT 8, DIN 6 (mic in, not used) |
| Speaker amplifier enable (LOW = on) | 1 |

The 3.5" (FNK0104N) moves the audio pins: MCK 17, BCK 18, WS 21, DOUT 15,
DIN 16; the amplifier enable is GPIO1 on both. The firmware's sounds live in
src/sound.cpp: tones rendered at 16 kHz into the codec, the amplifier
switched on only while one plays.

The battery row reads the cell's voltage and a percent from it. There is no
fuel gauge, and the charger's CHRG line drives an LED rather than a GPIO, so
it cannot say "charging" -- on USB it reads the cell under charge, a little
high.

Not usable on an S3: GPIO 26-32 (flash/PSRAM) and 33-37 (octal PSRAM).

### GPS — `[env:freenove-s3-gps]`

The CYD GPS builds' features: a position on each SD row and alert card, the
clock set from GPS, and WARDRIVE's WiGLE file on the card. The module goes
on the **UART** port (5V, GND, TXD, RXD, beside the card slot):

| Module | Freenove UART port |
|---|---|
| TX  | RXD (GPIO44) |
| VCC | 5V |
| GND | GND |
| RX  | not connected |

The console is the USB, so UART0 is free. The firmware tries 115200, 9600 and
38400 baud until a sentence parses. The **GPS** label in the top bar shows,
in red, only while there is a fix.

## Marauder V8 (justcallmekoko) — `[env:marauder-v8]`

An ESP32-C5 (8 MB flash, 4 MB PSRAM) behind a CP210x. Marauder's own V8
display setup is not public, so the screen, touch and backlight pins were
found on the board (2026-10-12); the rest are from Marauder's `configs.h`.

| Part | Pins |
|---|---|
| SPI bus (screen, touch, SD) | SCK 6, MOSI 7, MISO 2 |
| ILI9341 2.8" 240×320 | CS 23, DC 24, reset on the board's; BGR; 40 MHz (80 glitches) |
| Backlight | GPIO 8, high = on |
| XPT2046 resistive touch | CS 9 |
| SD card (SPI) | CS 10 |
| MAX17048 battery gauge | I2C SDA 5, SCL 4, address 0x36 |
| GPS | its TX into GPIO 14, 115200 baud |
| Activity LED | GPIO 28, high = on (a single-colour status light) |

Not used: 27 (an unfitted WS2812 in Marauder's build, and a strapping pin),
15-22 (flash and PSRAM), 11/12 (the console), 13 (GPS RX, nothing to send).

## LilyGo T-Deck Plus — `[env:tdeck-plus]`

An ESP32-S3FN16R8 (16 MB flash, 8 MB PSRAM) handheld with a keyboard. Pins
from LilyGo's `utilities.h` (T-Deck repo, examples/UnitTest), confirmed on a
board 2026-10-12. The console is the native USB port.

| Part | Pins |
|---|---|
| SPI bus (screen, SD, LoRa) | SCK 40, MOSI 41, MISO 38 |
| ST7789 2.8" 240×320 | CS 12, DC 11, no reset line; inversion on; 40 MHz |
| Backlight | GPIO 42, a 16-step driver chip: HIGH is full, each pulse steps it down (not PWM) |
| GT911 capacitive touch | I2C SDA 18, SCL 8, INT 16, address 0x5D |
| Keyboard | the same I2C, address 0x55: one character per read, 0x0D enter, 0x08 backspace. Alt+B toggles its own backlight |
| SD card (SPI) | CS 39 |
| Speaker | MAX98357A on I2S: BCK 7, WS 5, DOUT 6 (no codec, no enable pin) |
| GPS (MIA-M10Q) | its TX into GPIO 44, 9600 baud, on Serial1 |
| Battery | GPIO 4 through a 1:2 divider |
| Power enable | GPIO 10, HIGH or nothing past the chip has power on battery |
| Trackball | GPIO 3, 2, 15, 1; centre button GPIO 0 (not used yet) |
| SX1262 LoRa | CS 9, DIO1 45, RST 17, BUSY 13, on the display's bus; TCXO 1.8 V. Its bytes go out through the panel's own SPI registers between frames (src/lora_radio.cpp) |

Not used: 43 (GPS RX), 46 (keyboard INT, polled instead), 48/21/47/14 (the
ES7210 microphones), 19/20 (USB).

## M5Stack Tab5 — `[env:tab5]`

An ESP32-P4 (no radio of its own) with an ESP32-C6 for WiFi and Bluetooth on
SDIO. Pins from M5GFX and M5Stack's reference board support; the board, the
radio link and what is proven are in [TAB5.md](TAB5.md).

| Part | Pins |
|---|---|
| MIPI-DSI panel, 720×1280 as wired | two DSI lanes, driven by M5GFX; backlight GPIO 22 |
| Touch (inside the ST7123 panel chip) | I2C SDA 31, SCL 32, INT 23, address 0x55 |
| I/O expander 0x43 | P4 panel reset, P5 touch reset, P1 speaker enable, P2 5 V out |
| I/O expander 0x44 | P0 C6 power, P3 USB 5 V, P7 charge enable |
| ESP32-C6 on SDIO | CLK 12, CMD 13, D0 11, D1 10, D2 9, D3 8 |
| ESP32-C6 reset | GPIO 15, **active-high** on this unit |

## Sources

- [witnessmenow/ESP32-Cheap-Yellow-Display](https://github.com/witnessmenow/ESP32-Cheap-Yellow-Display) (MIT)
- [Fr4nkFletcher/ESP32-Marauder-Cheap-Yellow-Display](https://github.com/Fr4nkFletcher/ESP32-Marauder-Cheap-Yellow-Display)
- Sunton schematic for the ESP32-2432S028R (publicly available in
  the witnessmenow wiki).
- [Freenove/Freenove_ESP32_S3_Display](https://github.com/Freenove/Freenove_ESP32_S3_Display):
  the FNK0104 sketches, TFT_eSPI setups and the 2.8" schematic.
