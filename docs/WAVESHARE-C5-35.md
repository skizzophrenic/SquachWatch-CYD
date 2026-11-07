# Waveshare ESP32-C5-Touch-LCD-3.5 (`ws-c5-35`)

A 3.5" capacitive C5 board: ESP32-C5 (RISC-V, dual-band 2.4/5 GHz Wi-Fi + BLE),
ST7796 320×480 display, FT6336 touch, ES8311 audio, AXP2101 battery, all behind
a CH32V00x I/O expander. Reuses the NM-CYD-C5 C5 core and 5 GHz plumbing; the
differences live behind `-DWS_C5_35`.

## Build / flash

```
pio run -e ws-c5-35 -t upload
```

Download mode if it won't auto-enter: **hold BOOT, tap RST, release BOOT**, then
upload. The board self-powers over its own USB-C. Boot and panic logs come out
the **native USB** (USB-Serial-JTAG), read at 115200.

## What works

Display · touch · Wi-Fi (2.4 + 5 GHz) · BLE · SquachMesh · SD · **audio
(speaker)** · **battery (AXP2101)** — verified on hardware.

## Pin map

| Function | Detail |
|---|---|
| Display (ST7796) | SPI MOSI 7, SCLK 6, MISO 2, CS 8, DC 5; RST on the expander (`TFT_RST -1`) |
| Touch (FT6336) | I2C SDA 27 / SCL 26, addr `0x38`, 100 kHz |
| I/O expander | CH32V00x @ `0x24` on the touch bus — LCD reset, touch reset, backlight PWM |
| Audio (ES8311) | codec @ `0x18` on the touch bus → I2S BCK 23, WS 10, DOUT 25 (no MCLK) → NS4150B amp |
| Battery (AXP2101) | PMU on the touch bus; MX1.25 2-pin 3.7 V LiPo header (cell optional) |
| SD | CS 9 |

## Board-specific notes

- **Touch bus is shared** by the FT6336, the ES8311, the expander, and the
  AXP2101 (all on SDA 27 / SCL 26). The ES8311 register reads use a STOP-based
  read so a codec read can't wedge touch.
- **Audio** clocks the ES8311 off BCLK (no MCLK net): REG01 selects SCLK and
  REG02's pre-multiply is ×8 to reach ~4.096 MHz. The sound task is pinned
  chip-aware (`portNUM_PROCESSORS > 1 ? 1 : 0`) since the C5 is single-core.
- **Battery** readout is via `XPowersLib`; with no cell attached it reports
  `NONE` (the AXP2101 still answers and reports USB/charge state). An 80 MHz
  display push clock is the default (`-fast`-free); it's above the panel's rated
  60 MHz but holds on the units tested.
