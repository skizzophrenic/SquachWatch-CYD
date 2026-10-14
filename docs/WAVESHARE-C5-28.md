# Waveshare ESP32-C5-Touch-LCD-2.8 — BETA

A 2.8" ESP32-C5 board from Waveshare: the NM-CYD-C5's chip and screen size, a
different board around them. It builds as the NM-CYD-C5 plus `WAVESHARE_C5`
for what differs.

| | NM-CYD-C5 | Waveshare ESP32-C5-Touch-LCD-2.8 |
|---|---|---|
| Module | ESP32-C5, 16 MB flash, 8 MB PSRAM | ESP32-C5-WROOM-1, **32 MB** flash, 8 MB PSRAM |
| Panel | ST7789 240×320, SPI | ST7789 240×320, SPI |
| Panel pins | SCLK 6, MOSI 7, MISO 2, DC 24, CS 23 | SCLK 6, MOSI 7, MISO 8, **DC 9, CS 10** |
| Touch | resistive XPT2046, on the display's SPI | **capacitive CST3530, I2C 0x58** (SDA 0, SCL 1, INT 5) |
| Backlight | GPIO25 | **none**: the CH32V003 helper's PWM |
| Panel reset | board reset | **none**: the CH32V003 helper's EXIO1 |
| SD card | CS 10, on the display's SPI | **CS 23**, on the display's SPI |
| Status light | WS2812 on GPIO27 | **none** |
| Speaker | none | **ES8311 + NS4150B**, I2S BCK 24 / WS 25 / DOUT 26 |
| BOOT button | — | GPIO28 |
| USB | CH340 + native | **native only** (USB Serial/JTAG) |
| Extras | — | QMI8658 IMU, PCF85063 RTC, SHTC3, ES8311 codec + speaker, LiPo charger |

Vendor documentation, schematic and examples:
[docs.waveshare.com/ESP32-C5-Touch-LCD-2.8](https://docs.waveshare.com/ESP32-C5-Touch-LCD-2.8)
and [waveshareteam/ESP32-C5-Touch-LCD-2.8](https://github.com/waveshareteam/ESP32-C5-Touch-LCD-2.8).

## Build and install

```bash
pio run -e waveshare-c5-28 -t upload
```

Same pioarduino platform and TFT_eSPI C5 patch as the NM-CYD-C5
(`extra_script.py`). One USB-C port, the C5's own, and this build sets
`ARDUINO_USB_CDC_ON_BOOT=1`, so the serial console is that port too. If an
upload will not start, hold **BOOT**, tap **RESET**, release BOOT, and try again.

The 16 MB partition table (`partitions_ota_16mb.csv`) is used; the other 16 MB
of flash is left alone.

## The helper chip

Nothing appears on the panel until the CH32V003 at I2C 0x24 has been spoken
to: it holds the panel's reset and owns the backlight. `WsC5::begin()`
(`src/waveshare_c5_board.cpp`) runs before `tft.init()`, sets every EXIO as an
output, releases both resets, turns the speaker amplifier **off**, and pulses
the panel's reset. Brightness then goes through its PWM register. Register map
and timings are from Waveshare's `01_exio` and `06_lvgl_demo` examples:

| Register | Meaning |
|---|---|
| `0x02` | EXIO mode, one bit per pin (1 = output) |
| `0x03` | EXIO output levels |
| `0x05` | backlight PWM, 0..255 (Waveshare clamp at 97%; so does this) |

| EXIO | Signal |
|---|---|
| 0 | touch reset (active low) |
| 1 | panel reset (active low) |
| 3 | speaker amplifier enable |

## Touch

The CST3530 uses 32-bit register addresses: a 9-byte read from `0xD0070000`
gives the count (`b[3] & 0x0F`) and the first contact
(x = `(b[7] & 0x0F) << 8 | b[4]`, y = `(b[7] & 0xF0) << 4 | b[5]`), and an
address-only write to `0xD00002AB` acknowledges it. The five-target calibration
runs on first boot like every other capacitive board.

**The controller can be asleep at boot.** On some boots nothing answers at
0x58 on an untouched screen (an I2C scan shows only 0x18, 0x24, 0x51, 0x6B and
0x70), and the chip appears the moment a finger lands. So a failed boot probe
is not "no touch": the board is always treated as capacitive, and every poll
asks the chip again.

## Speaker

The ES8311 codec (I2C 0x18) and NS4150B amplifier drive the speaker plug
(Waveshare ship a speaker/microphone combo for it). Same sounds and SOUND /
VOLUME rows as the Freenove S3 boards (`src/sound.cpp`), with differences:

- **Data out is GPIO26, not 27.** Waveshare's BSP routes `.dout` to GPIO27
  (`BSP_I2S_DSIN`), and with that the board is silent; on GPIO26 it plays.
  Found by ear on the board. BCK 24, WS 25.
- **No MCLK line.** The codec takes its clock from BCK (reg 01 = 0xBF): 16-bit
  slots, BCK = 32 fs, multiplied x8 in the codec (reg 02 = 0x18) to 256 fs.
  The codec is set up with esp_codec_dev's own es8311 sequence, as the BSP
  does, and the I2S clocks never stop between sounds (with BCK as its clock
  the codec does not survive losing it).
- **IDF 5's I2S driver** (`i2s_std`), not the legacy one.
- **Amp enable is the helper's EXIO3**, set once at boot and left on, as the
  BSP does.
- The sound task is not pinned to core 1: the C5 has one core.

**EXIO6 is not a spare pin**: driven high, it reset the board.

## Status

**Tested on hardware 2026-10-13** (one board, USB-powered):

- [x] Panel lights and shows the splash (helper chip reset and backlight)
- [x] Colours correct with `PANEL_NEEDS_INVERSION = true` and RGB order
- [x] Touch works; the five-target calibration completes and is saved
- [x] SD card mounts (32 GB FAT32) and the log file is created
- [x] WiFi 2.4 + 5 GHz and BLE scanning, detections arrive (~30 fps on the main screen)
- [x] Brightness slider works (the helper chip's PWM)

A poor USB cable browned the board out mid-boot once (screen dark, board gone
from USB); a good cable fixed it.

- [x] Speaker: every sound, the SOUND and VOLUME rows, alerts from real detections

Not done here: the BOOT button (GPIO28; the firmware's button code assumes
GPIO0, which is SDA on this board, so it is switched off), the battery level
(the helper's ADC), the microphone, IMU and RTC.
