# Waveshare ESP32-S3-Touch-LCD-3.5 / -C

Build: `pio run -e waveshare-s3-35 -t upload` (or `waveshare-s3-35-gps`).
First flash over USB-C; hold BOOT while plugging in if the port does not appear.

The screen runs at 80 MHz. If you see torn rows or speckle, build
`waveshare-s3-35-40mhz` instead (Waveshare's own firmware runs the panel at 40).

| Function | Wiring |
|---|---|
| Display | ST7796 320x480, SPI MOSI 1 / SCLK 5 / DC 3, no CS, backlight GPIO6, reset = TCA9554 EXIO1 |
| I2C (shared) | SDA 8 / SCL 7: AXP2101 0x34, TCA9554 0x20, FT6336 touch 0x38, ES8311 0x18, PCF85063 0x51, QMI8658 0x6B |
| SD card | SDMMC 1-bit: CLK 11 / CMD 10 / D0 9 |
| Audio | ES8311 I2S: MCLK 12, BCLK 13, LRCK 15, DOUT 16; amp enable = EXIO7 (held high) |
| Buttons | BOOT = GPIO0 (hold 3 s = POWER OFF), PWR = EXIO6 |
| GPS (`-gps` build) | module TX to the header's RXD (GPIO44), 5V, GND |

## What works
Detection (WiFi + BLE), touch, backlight, SD logging and wardriving, speaker, battery % and
charging read from the AXP2101, RTC (PCF85063, kept in UTC), BOOT power-off.

## Not on this board / not wired
LoRa (no radio), status light (no addressable LED), IMU shake (the shake code is BMI270-only;
the QMI8658 is not driven), camera.

## Notes
- Touch runs the five-target calibration on first boot. The compiled-in default is the
  panel's native 320x480 (identity), and SKIP is offered.
- Wrong colours: tap ORDER / INVERT in Settings (the color check screen).
- The clock has no battery by default: join WiFi (Settings > System > WIFI NETWORKS) to get
  the time over NTP. The RTC keeps it only if a battery is fitted to the RTC header.
- EXIO7 as speaker-amp enable is inferred from the schematic and Waveshare's firmware.
- The AXP2101 watchdog Waveshare's firmware arms is switched off here.
