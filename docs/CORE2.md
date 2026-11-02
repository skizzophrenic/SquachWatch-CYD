# M5Stack Core2 (SquachWatch-Core2)

Build: `pio run -e core2 -t upload`. Bring-up, not released, and **not yet run
on a unit**: it compiles, and every register value and pin comes from M5's own
sources (M5GFX's Core2 autodetect, M5Unified's power class), but nothing here
has been looked at on glass. First boot is the test.

## What works (by construction)

| Part | How |
|------|-----|
| Screen | ILI9342C/E on VSPI (MOSI 23, MISO 38, SCLK 18, CS 5, DC 15) via TFT_eSPI's `M5STACK` rotation table. Landscape upright, USB-C at the bottom. `include/core2_user_setup.h` |
| Power chip | AXP192 (first Core2) or AXP2101 (v1.1 and later), told apart by ID register 0x03 (0x03 / 0x4A), so one build serves both. `src/core2_power.cpp` |
| Backlight | The chip's own supply (AXP192 DCDC3, AXP2101 BLDO1), dimmed by voltage. No GPIO, no LEDC |
| Touch | FT6336 on I2C 21/22 at 0x38. Reports screen pixels, so first-boot calibration is skipped (SETTINGS can still run it). The three painted button areas below the screen are ignored |
| microSD | CS 4 on the display's bus, handed TFT_eSPI's SPI instance like the 3.5" CYD |
| Battery | Percent and cable from the chip, on the home screen's lava lamp and the SYSTEM page's BATTERY row |
| Vibration | Motor on AXP192 LDO3 / AXP2101 DLDO1, voltage-driven, played from the loop so nothing blocks. VIBRATE on the main settings list: OFF / HIGH / MED / LOW. Alert (double pulse, one per 10 s), watch-list hit (three long), message (one short) |
| Sound | NS4168 amplifier on I2S (BCLK 12, LRCK 0, DOUT 2), enabled through the power chip (AXP192 GPIO2 / AXP2101 ALDO3). Starts on ALERTS at 50%; SOUND and VOLUME rows in Settings |
| Buttons | The three painted areas under the glass. **A** home (from home: SETTINGS), **B** screen off/on (pocket mode), **C** brightness step |

## What is off

- **IMU (MPU6886), RTC (BM8563)**: not used yet.
- **BOOT button**: GPIO0 is the speaker's I2S clock here, so pocket mode is on the middle painted button instead.
- **Status light**: the green LED is on the power chip; the CYD's three PWM pins
  (4, 16, 17) are the microSD CS and unrelated lines here.

## Check on first boot

1. Console prints `[pmu] AXP192 up` or `[pmu] AXP2101 up`. If it prints no answer,
   nothing else will work.
2. Colours inverted or red/blue swapped: SETTINGS has INVERT and RGB SWAP. If
   either is needed, set `PANEL_NEEDS_INVERSION` / `TFT_RGB_ORDER` to match.
3. A **blank white screen** on a late unit means the panel is an ILI9342E, which
   M5GFX gives a different init list from the C. TFT_eSPI only has the
   ILI9341 one.
4. Touch lands mirrored or rotated: run CALIBRATE TOUCH from SETTINGS.

## Panel ID

The panel's SPI is 3-wire (the data pin reads back on MOSI), so TFT_eSPI cannot read its ID the way M5GFX does. C versus E is told by eye: a white screen means try the E init list.
