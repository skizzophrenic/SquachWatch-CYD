# Waveshare ESP32-S3-Touch-AMOLED-2.06 — BETA (build it yourself)

The T-Watch S3 build (`TWATCH_S3`: its 240×240 screens, crown, power saver,
radio duty, pocket mode) on Waveshare's 2.06" AMOLED watch. The watch UI is
drawn at 240×240 as on the T-Watch and scaled 5/3 to a 400×400 square in the
middle of the 410×502 panel. The black bands above and below the square show
the time (top), and the date, battery and wardrive row count (bottom), drawn
at the panel's own resolution.

| | LilyGo T-Watch S3 | Waveshare 2.06" |
|---|---|---|
| Chip | ESP32-S3, 16 MB flash, 8 MB PSRAM | ESP32-S3R8, **32 MB** flash (16 MB used), 8 MB OPI PSRAM |
| Screen | 240×240 ST7789, SPI | **410×502 CO5300 AMOLED, QSPI** |
| Touch | FT6336 | FT3168 (FT6336 registers), INT GPIO38, RST GPIO9 |
| I2C | power, clock, motion, haptics on their own bus (10/11) | **everything on one bus**, SDA 15 / SCL 14 |
| Power | AXP2101 | AXP2101 (factory voltages kept) |
| Clock chip | PCF8563 | **PCF85063** (time registers from 0x04) |
| Motion | BMA423 | **QMI8658** (0x6B, or 0x6A) |
| Alerts | DRV2605 vibration motor | **ES8311 + speaker** (no motor) |
| LoRa / GPS | SX1262; GPS on the S3 Plus | none |
| SD card | none | **microSD, SDMMC 1-bit** (CLK 2, CMD 1, D0 3) |

Vendor board support: [Waveshare wiki](https://www.waveshare.com/wiki/ESP32-S3-Touch-AMOLED-2.06).

## Build and install

```bash
pio run -e waveshare-206 -t upload --upload-port /dev/ttyACM0
```

Native USB, like the T-Watch: after a reset the port goes away and comes
back, so the first two seconds of the boot log are lost on most hosts. Not
on the web flasher yet.

The bootloader and partition table are the T-Watch's own
(`partitions_twatch.csv`, same board settings), so the S3 manifests could
share them.

## What is different from the T-Watch build

- **Display**: `co5300_qspi.*` drives the panel (SH8601-style command
  framing, column gap 22, even-aligned windows). `co5300_display.*` is the
  `TFT_eSPI`-typed seam, as `st77922_display.*` is on the Freenove 3.5". Every
  frame goes through FramePush's QSPI route, with a Waveshare branch that
  scales while converting (align 6 logical rows = 10 panel rows). Brightness
  is panel register 0x51; there is no backlight pin.
- **Touch**: no calibration. The FT3168 reports panel pixels, and the
  compiled-in ranges are the scaled square. Monitor mode off (0x86=0) and
  level interrupt (0xA4=0), and reads only while INT is low. Without that the
  chip stops answering about 12 s after the last touch and every poll times out.
- **Alerts**: `twatchBuzz()` plays sounds (`Sound::alert()`). BUZZ still
  switches alerts off and SOUND (OFF / ALERTS / ALL) says whether the speaker
  may play. SOUND defaults to ALERTS and VOLUME to 70% here. The ES8311 needs
  the analog bias registers and **0x31 = 0 (DAC unmute)**. It comes out of
  reset muted.
- **Corner clock** on the main screen is off: the top band shows the time.
- **SCANNING** (first row of WATCH SETTINGS): OFF makes it just a watch.
  WiFi, Bluetooth scanning and the squad's beacon are off whether the screen
  is on or not, and on the cable too. The screen, animations, clock and
  bands carry on, and the bottom band says `RADIOS OFF`. The radio
  self-heal does not count switched-off radios as deaf ones. A raw scan
  screen or an update still gets the radio while it is open. Console:
  `SCANNING OFF` / `SCANNING ON`.
- **Charge current and DC1 mode** are left as the board came.

## Wardriving without a GPS

WARDRIVE (WATCH SETTINGS) writes every access point and Bluetooth device to
`/wigle-YYYYMMDD-HHMM.csv` on the card, as a WigleWifi-1.6 file. A device is
written again 15 s after its last row. Where each row was heard comes from
one of two places:

1. **The phone's GPS over Bluetooth.** While WARDRIVE is on the watch
   advertises as `SquachWatch GPS` (connectable, one service, see
   `include/phone_gps.h`). The squad's beacon is off meanwhile.
   `web-flasher/wardrive/index.html`, opened in a Web Bluetooth browser,
   connects and writes the phone's position once a second. On an iPhone that
   browser is **Bluefy**: Safari and Chrome on iOS have no Web Bluetooth. Rows
   get the phone's position while it is under 5 s old. The page also sets the
   watch's clock and keeps a GPX track as a backup. iOS pauses the page when
   the screen locks, so keep it in front with Auto-Lock off.
2. **A GPX track recorded on the phone**, merged afterwards. Rows written
   without a position have 0,0.

```bash
python3 tools/wigle_merge.py wigle-20261016-1830.csv              # drop the 0,0 rows
python3 tools/wigle_merge.py track.gpx wigle-20261016-1830.csv    # place them from the track
```

Rows the phone link placed are kept as they are. The output uploads to
wigle.net unchanged. `--offset` corrects a watch clock that was off.

The bottom band shows `WD <rows>`, or `WD NO SD` / `WD NO CLK`.

## Console (bench)

| Command | |
|---|---|
| `SD LS` | the card's top directory, the tails of today's log and the wardrive file |
| `SD TEST` | 256 KB written, read back, checked and deleted, with speeds |
| `SD FORMAT` | **erases the card**: FAT32 with 32 KB clusters, then remounts |
| `SD MOUNT` | mount again after inserting a card |
| `SOUND REG` | the ES8311's registers |
| `SOUND VOL n` | volume 0–100 |
| `GPS STATUS` | the wardrive state, file, WiFi rows and phone link |
| `SCANNING OFF` / `ON` | the WATCH SETTINGS row's switch |

`SD FORMAT` exists because mounting with format-on-failure took minutes on a
64 GB card (one-sector clusters) and looked like a hang.

## Tested

On one board (2026-10-16): picture and colours (rotation 0), touch,
crown, pocket mode, battery readout, RTC across resets, motion,
WiFi and BLE detection, every sound, SD logging (61 GB SDHC, 1.3 MB/s write),
wardrive to the card. The phone GPS link was tested from a PC (bleak) acting
as the phone.

**Not yet:** the link from an actual iPhone in Bluefy; rotation 2 (upside down); the charging indicator; battery life; deep
power-off from the crown; the 0x6A motion-sensor address.
