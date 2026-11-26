# DIYmalls 2.8" — JC2432W328C

The JC2432W328C is a classic ESP32 board with a 2.8-inch, 240×320
capacitive-touch display. SquachWatch supports it through the existing `cyd`
and `cyd-fast` firmware configurations; it does not need a separate
PlatformIO environment or a duplicate firmware build.

The unit covered by this guide is sold as the **DIYmalls 2.8"**. The model
code identifies the hardware more reliably than the seller name. The board
has an ESP32, 4 MB flash, a USB-C connector, a microSD slot, a speaker
connector, a two-pin 1.25 mm BAT connector, and a dedicated battery power
switch. Its display is an ST7789 at 240×320; its touch panel is capacitive.

## SquachWatch support

Choose **DIYmalls 2.8" — JC2432W328C (Capacitive)** in the web flasher, or
type `JC2432W328C` into the board-code finder. It uses the same
`manifest-cyd.json` firmware as the original ST7789 CYD option, and the same
`manifest-cyd-fast.json` when the 80 MHz checkbox is on. The named option
keeps board identification clear without creating another build.

The shared firmware detects the touch hardware at startup. It probes for the
capacitive controller at I²C address `0x15` on GPIO33 (SDA) and GPIO32 (SCL),
with a reset pulse on GPIO25. If a CST816/CST820-family controller answers,
the firmware uses capacitive touch. If it does not, the same `cyd` build falls
back to the original CYD's resistive XPT2046 SPI path. The exact CST816-family
part fitted can vary; the runtime probe is for the supported I²C device, not a
model-number check.

The display and backlight use the ST7789 CYD path. The display pins match the
classic CYD configuration; this board's backlight is on GPIO27 rather than
the original CYD's GPIO21. The firmware drives GPIO27 as an active-high PWM
backlight and leaves the existing classic CYD display behavior intact.

## Firmware pin assignments

These are the pins used by the shared CYD firmware. The display, touch and
backlight assignments for the JC2432W328C were confirmed during support work.
The SD entries describe the firmware's classic CYD SD configuration; mount
behavior on every JC2432W328C revision has not been confirmed here.

### Display — ST7789, SPI

| Signal | GPIO |
|---|---:|
| MOSI | 13 |
| MISO | 12 |
| SCLK | 14 |
| CS | 15 |
| D/C | 2 |
| Reset | No GPIO reset line is used by this configuration |
| Backlight | 27, active high, PWM |

### Capacitive touch — CST816/CST820 family, I²C

| Signal | GPIO / value |
|---|---:|
| SDA | 33 |
| SCL | 32 |
| Reset | 25, active-low pulse during startup |
| I²C address | `0x15` |
| Interrupt | Not used by this firmware |

### microSD — firmware's classic CYD SPI bus

| Signal | GPIO |
|---|---:|
| SCLK | 18 |
| MISO | 19 |
| MOSI | 23 |
| CS | 5 |

The board has a speaker connector, but SquachWatch does not use it on the
shared CYD build. Its connector-to-GPIO mapping and electrical drive
requirements have not been verified for this model. Do not infer them from
the original CYD or Freenove board.

## Battery and USB power

The physical BAT connector and battery switch are present on this board. A
protected 3.7 V, 3000 mAh 1S LiPo is an example of a pack being used with a
unit; that use does **not** establish that the board's charging circuit is
compatible with that pack or that USB charging is supported.

Battery protection and charge management are different functions. Protection
electronics in a battery pack may guard the cell against some over-current,
over-discharge or over-charge conditions; they do not provide a charger. A
charger or power-management circuit must implement a suitable charging
profile. The board's charger IC, battery power path and USB-to-battery charge
behavior have not been identified or verified. Treat USB-C as the board's
USB/data and 5 V input; do not assume that connecting USB charges a battery.
Use the board and battery only in ways supported by their respective
manufacturer documentation.

SquachWatch currently has no battery percentage or voltage reading for this
board. The battery row and voltage-to-percentage code are compiled only for
the Freenove ESP32-S3 boards, Marauder V8 and T-Deck Plus; the classic CYD
configuration has no battery-monitor implementation. Although the board
provides a BAT connector for battery power, no verified JC2432W328C schematic
or measurement establishes a divider to an ESP32 ADC input or a fuel-gauge IC.
The Freenove S3's GPIO9 divider must not be copied here: this board uses a
classic ESP32, where GPIO6–11 are reserved for flash. Until a JC2432W328C
revision's schematic or a safe measurement identifies a valid sensing path,
the firmware deliberately makes no battery-percentage claim and does not
guess a pin.

The CYD **CHARGE MODE** setting only quiets firmware activity to reduce draw;
it is not a charger control and does not verify that a cell is charging.

## How it differs from other boards

| Board | Processor / display | Touch | Battery and extras |
|---|---|---|---|
| Original 2.8" CYD (ESP32-2432S028R) | Classic ESP32; 2.8" 240×320 panel, ST7789 or ILI9341 by batch | Resistive XPT2046 on a separate SPI bus | Backlight GPIO21; no verified BAT connector in the shared configuration |
| DIYmalls 2.8" (JC2432W328C) | Classic ESP32, 4 MB flash; 2.8" 240×320 ST7789 | Capacitive CST816/CST820-family I²C, detected at runtime | Backlight GPIO27; microSD, speaker connector, BAT connector and battery switch; battery monitor/charging not verified |
| Freenove ESP32-S3 2.8" (FNK0104B) | ESP32-S3; 2.8" 240×320 ILI9341 | FT6336 I²C on different pins | SDMMC, speaker/audio codec, WS2812 and a documented battery divider on GPIO9; distinct firmware environment |

The DIYmalls board shares the classic CYD firmware path, but it is not
electrically identical to the original resistive CYD. It is also not the
Freenove S3: its processor, display driver, touch wiring, SD interface and
battery-sensing evidence differ. Keep the firmware's runtime touch probe and
the board-specific GPIO27 backlight handling intact.

## What still needs physical verification

- Confirm SD mounting and logging across the JC2432W328C revisions in use.
- Identify the fitted touch-controller revision and verify its reset and I²C
  behavior on more than one unit.
- Find a board schematic or safely measure whether BAT voltage reaches a
  suitable ADC input, and identify any charger/power-management IC before
  documenting charging support or adding battery telemetry.
- Verify the speaker connector pinout and electrical requirements before
  enabling audio on this board.

## References

- [DIYmalls product listing for model JC2432W328C](https://www.amazon.se/dp/B0FF1XFSCV)
- [DIYmalls JC2432W328C manual](https://manuals.plus/asin/B0FF1XFSCV)
- [JC2432W328C board test notes](https://github.com/pay191/DIY_Malls-JC2432W328C_Tests)
- [Classic CYD pinout and firmware notes](PINOUT.md)
