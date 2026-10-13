# Waveshare ESP32-C5-Touch-LCD-3.5 — experimental

A 3.5" 320×480 board on an **ESP32-C5**, so it scans **5 GHz** as well as
2.4 GHz, like the NM-CYD-C5. Product page:
[waveshare.com/esp32-c5-touch-lcd-3.5.htm](https://www.waveshare.com/esp32-c5-touch-lcd-3.5.htm).
Vendor demos and schematic:
[waveshareteam/ESP32-C5-Touch-LCD-3.5](https://github.com/waveshareteam/ESP32-C5-Touch-LCD-3.5).

| | |
|---|---|
| Module | ESP32-C5-WROOM-1U-N32R8 (32 MB flash, 8 MB PSRAM) |
| Display | 3.5" IPS, **ST7796**, 320×480, SPI |
| Touch | **FT6336** capacitive, I2C 0x38 |
| I/O expander | **CH32V003**, I2C 0x24: panel reset, touch reset, power enable, backlight PWM |
| Card | microSD on the display's SPI bus |
| USB | one USB-C, the C5's **native** USB (no CH340) |
| Power | AXP2101 PMIC: battery % and voltage on the SYSTEM page and the home screen |
| Clock | PCF85063 RTC: keeps the time across reboots once it has been set once |
| Also on board, not used yet | ES8311 codec + speaker, QMI8658 IMU, SHTC3, camera connector |

## Build and install

```bash
pio run -e waveshare-c5-touch-lcd-35 -t upload
```

Like the NM-CYD-C5 this environment carries its own `platform` line
(pioarduino, Arduino 3.3 / IDF 5.5). It reuses the NM-CYD-C5's TFT_eSPI C5
port (`extra_script.py` applies it) and its 16 MB partition table, which fits
the module's 32 MB.

The console is **USB CDC** on the one USB-C port (`ARDUINO_USB_CDC_ON_BOOT=1`
in `boards/waveshare-c5-touch-lcd-35.json`). If an upload will not start, hold **BOOT**,
tap **RESET**, release BOOT, and upload again.

The same rule as the NM-CYD-C5 applies to hand-flashing: write the four parts
(bootloader at 0x2000, partitions at 0x8000, boot_app0 at 0xe000, firmware at
0x10000), not `firmware.factory.bin` at 0x0, or settings are erased.

## Pinout

From Waveshare's BSP (`esp32_c5_touch_lcd_3_5.h`) and Arduino demos.

| Device | SCK | MISO | MOSI | CS | DC | Reset | Backlight |
|---|---|---|---|---|---|---|---|
| Display (ST7796) | 6 | 2 | 7 | 8 | 5 | EXIO1 | expander PWM |
| SD card | 6 | 2 | 7 | 9 | — | — | — |

| I2C (SDA 27, SCL 26) | Address | Notes |
|---|---|---|
| CH32V003 I/O expander | 0x24 | reg 0x02 direction, 0x03 outputs, 0x05 backlight PWM (255 brightest) |
| FT6336 touch | 0x38 | reset on EXIO0, INT on GPIO3 (not used: polled) |

EXIO5 is a power enable that Waveshare's code raises along with the two
resets; `WsC5::begin()` does the same.

## How the port is put together

The build defines `NM_CYD_C5` for everything that is about the chip (the
platform, the DMA frame push, core 3's LEDC API, PSRAM frame buffers) and
`WAVESHARE_C5` for what this board does differently:

- `src/ws_c5_board.cpp` talks to the CH32V003: it pulses the panel and touch
  resets and raises the power enable **before** `tft.init()` (the ST7796 has no
  reset line of its own), and sets brightness through the PWM register.
- Touch goes through the existing `CapTouch` driver at 0x38. The FT6336
  reports portrait panel pixels, so like the CrowPanel and the Freenove 3.5"
  there is no calibration at first boot. If touches land mirrored or swapped,
  run the calibration from **SETTINGS**: the five-target fit corrects that.
- `RLPHANTOM_R` is **not** defined: that flag is the NM-CYD-C5's resistive
  touch on the display's bus, which this board does not have.
- No status light. GPIO27, the NM-CYD-C5's WS2812, is I2C SDA here.

## Status

**Untested on hardware.** It compiles; nothing more is known. Things to look
at on first boot, in order:

1. **Blank or white screen**: check the serial console for
   `[ws-c5] I/O expander up`. If the expander did not answer, the panel is
   still held in reset.
2. **Colours**: inversion is on and colour order BGR, as Waveshare's init
   sends them. The first-boot colour check offers ORDER and INVERT if either is
   wrong.
3. **Torn or speckled picture**: the bus runs at 40 MHz. Waveshare run this
   panel at 60, so 80 is worth a try once the picture is right (drop the two
   `SQW_C5_*` lines in `platformio.ini`).
4. **Touch**: see above.
