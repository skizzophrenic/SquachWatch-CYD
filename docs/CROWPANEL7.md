# Elecrow CrowPanel ADVANCE 7.0-HMI (experimental)

An ESP32-S3-WROOM-1-N16R8 (16 MB flash, 8 MB octal PSRAM) behind an
800×480 RGB-parallel IPS panel with a GT911 capacitive touch controller. It
is not a CYD: there is no SPI display bus at all, the backlight is a command
to a helper MCU, the USB-C socket reaches UART0 through a CH340K, and the
SD slot shares its pins with the audio amplifier through a switch on the
board. One unit tested, board revision V1.2 or later (the helper MCU at
I2C 0x30 identifies those).

## Build and install

```sh
pio run -e crowpanel7
pio run -e crowpanel7 -t upload
pio device monitor -b 115200
```

The console is a real UART at 115200; the port shows up as a CH340
(`/dev/cu.wchusbserial*`, `COMx`). macOS needs WCH's own driver for the
CH340K. No first-boot touch calibration: the GT911 reports panel pixels,
and the built-in mapping is exact.

## How it draws

The app renders its usual UI at **400×240 into an 8-bit sprite in internal
RAM**, and a dirty-row blit doubles that to the panel's 800×480 through the
ESP-IDF RGB driver (`esp_lcd_rgb_panel`) and its 750 KB framebuffer in
PSRAM. Rotation is locked: the panel is landscape as wired and an RGB panel
has no MADCTL to turn it with.

That is the arrangement that keeps the picture still, and the reason is
worth knowing before changing it. The panel's DMA reads the framebuffer
out of PSRAM continuously with about two microseconds of FIFO slack. The
app's own scattered writes into a PSRAM sprite starve it and the picture
steps sideways; a frozen test picture was rock steady alone and with both
radios scanning, and twitched the moment the UI drew into PSRAM. With the
sprite in internal RAM only the blit's large sequential writes touch the
bus, and those it tolerates. Native 800×480 (`crowpanel7-native`) exists
for a core with bounce buffers or faster PSRAM, and twitches on this one.

TFT_eSPI stays as the *type* the app draws through (`main.cpp` upcasts the
sprite to `TFT_eSPI*`, which only works because the sprite derives from
the device); `CrowPanelTFT` hides every method that would reach a bus and
nothing is ever sent down one. `include/crowpanel7_display.h` has the
full reasoning.

## Wiring and behavior

| Function | Configuration |
| --- | --- |
| Display | 800×480 RGB565 parallel, DE 42 / VSYNC 41 / HSYNC 40 / PCLK 39, porches 8/4/8 both axes, 16 MHz pixel clock (18 MHz and up run away on this core) |
| Data lines | B0..B4 = 21, 47, 48, 45, 38; G0..G5 = 9, 10, 11, 12, 13, 14; R0..R4 = 7, 17, 18, 3, 46 |
| Touch | GT911 at 0x5D on I2C SDA 15 / SCL 16; INT on GPIO1 (also its address strap); points read from 0x814F |
| Backlight | none on a GPIO: a byte to the STC8H1K28 helper at 0x30, 0 brightest … 244 dimmest, 245 off |
| Buzzer | the helper: 246 on, 247 off (the optional audible alert) |
| Clock chip | PCF8563 at 0x51, no backup cell on the tested unit (time is not held across power-off) |
| SD card | GPIO 6/4/5 through a CH486F switch shared with the I2S amplifier and the wireless header; **K1**, a two-position DIP switch on the board, selects — both open is the card. Not used by this build |
| Status LED | none (GPIO16 is the touch clock; the CYD's LED pins are left alone) |
| Serial | CH340K to UART0, 115200; no native USB |
| Partitions | `partitions_crowpanel7.csv`: two 4 MB app slots, the black box at 0x810000 |

Two board-specific behaviours, both measured rather than assumed:

- **Flash writes show.** Every flash write disables the cache the panel's DMA
  reads through, and the picture steps sideways for it. This board saves
  the lifetime counters once a minute rather than every five seconds, and
  drains the black box two records in a burst then one per thirty seconds,
  instead of six then one per three. What remains is the event-driven
  writers. The real fix is a core with bounce buffers.
- **The WiFi update flow dims.** Joining a network at full backlight browned
  the tested unit out into a power-on reset. The update flow turns the
  backlight down and transmits at 8.5 dBm on this board; the boot check
  already did. Run it from a supply that gives two amps, not a laptop port.

## Validation and remaining checks

Tested on one unit, through an afternoon of bench work rather than a
continuous soak: the panel and touch, WiFi sniffing and BLE scanning with
the mesh advertising, a first detection (an AirTag) four seconds after
boot, the WiFi network list and the password keyboard, and the WiFi update
boot check against the site. Frame rate 17–29 fps depending on the screen,
about 80 KB of internal RAM free with both radios up.

Not yet exercised: an actual over-the-air install (there is no published
build for this target), Bluetooth updates, the SD log (K1 on the tested
unit selects the amplifier), a multi-day soak, a second unit. The
`crowpanel7-paneltest` and `crowpanel7-probe` environments are bench
builds for the next person: a static test picture with the radios off or
on, and a report of the helper MCU, clock chip, card slot and a WiFi scan
through the sniffer's own driver.

## References

- Elecrow: CrowPanel Advance 7.0 (product, wiki, Eagle schematics per
  revision, examples for V1.0/V1.2/V1.3–1.5)
- The pin map and timing here match Elecrow's own examples and were
  measured on the unit; `include/crowpanel7_board.h` carries the numbers.
