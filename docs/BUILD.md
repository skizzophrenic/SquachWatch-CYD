# Building SquachWatch-CYD

The friendliest possible walkthrough. If you have a CYD board and
a computer, you can flash this in about ten minutes.

Just want it on a board? The web flasher at
[squachwatch.com](https://squachwatch.com/) does it from a browser, no
toolchain needed. This page is for building it yourself. It walks
through the classic 2.8" CYD; every other board is the same steps with
a different environment name (see [Which build?](#which-build)).

## What you need

- **ESP32-2432S028R** (the "Cheap Yellow Display" / CYD) — about $15
  on Amazon.
- A **USB-C cable** that supports data (some cables are charge-only —
  those won't work).
- A computer running **Windows, macOS, or Linux**.
- About **250 MB of free disk space** for the toolchain.

That's it. No soldering, no extra components.

## Install PlatformIO

**Option A — VS Code (recommended for beginners):**
1. Install [VS Code](https://code.visualstudio.com/).
2. Open VS Code → Extensions panel (`Ctrl+Shift+X` / `Cmd+Shift+X`).
3. Search for `PlatformIO IDE` and install it.
4. Restart VS Code when prompted.

**Option B — Command line:**
```sh
# macOS
brew install platformio

# Linux / WSL
pipx install platformio
# (or: python3 -m pip install --user platformio)

# Windows
pipx install platformio
```

## Get the code

```sh
git clone https://github.com/skizzophrenic/SquachWatch-CYD
cd SquachWatch-CYD
```

If you don't have `git`, you can also download a ZIP from GitHub
and unzip it.

## Install the USB driver (Windows only)

Most operating systems already know the CYD's CH340 / CP2102 USB
bridge. On Windows 7 or older, install the CH340 driver manually:
- [CH340 driver download](http://www.wch-ic.com/downloads/CH341SER_EXE.html)

## Which build?

Each board has its own environment in `platformio.ini`. The ones the
web flasher offers:

| Environment | Board |
|---|---|
| `cyd-fast` | 2.8" CYD, ST7789 screen (ESP32-2432S028R, the usual one), display at 80 MHz. The flasher's default for this board since v1.32.0. |
| `cyd` | The same board at the stock display clock. Use it if `cyd-fast` gives you speckles or a white screen. |
| `cyd-ili9341` | 2.8" CYD with the older ILI9341 screen. Same board, different panel batch. (`cyd-ili9341-fast` exists, but this panel usually can't keep up at 80 MHz.) |
| `freenove32` | 3.2" resistive, Freenove FNK0103 |
| `cyd32c` | 3.2" capacitive, ESP32-2432S032C (already runs at 80 MHz) |
| `cyd35-fast` | 3.5" resistive, ESP32-3248S035R (BETA) |
| `cyd35c-fast` | 3.5" capacitive, ESP32-3248S035C (BETA) |
| `awok` | AWOK 2.4", ESP32 Marauder v6.1 |
| `rlphantom-r` | RL Phantom 2.4", ESP32-2432S024R |
| `freenove-s3` | Freenove ESP32-S3 2.8", FNK0104 (BETA) |
| `crowpanel7` | Elecrow CrowPanel Advance 7.0 (BETA) — see below |
| `nm-cyd-c5` | RockBase NM-CYD-C5, ESP32-C5 (BETA) — see [NM-CYD-C5](NM-CYD-C5.md) |
| `sticks3` | M5Stack StickS3 (BETA) |
| `cardputer-adv` | M5Stack Cardputer ADV — not the original Cardputer (BETA) |
| `twatch-s3` | LilyGo T-Watch S3 / S3 Plus (BETA) |

The rest are not on the flasher: `freenove-s3-40` is the Freenove
ESP32-S3 4.0" (FNK0104S), run on one board so far; `tlora-pager` is
the LilyGo T-LoRa Pager, also run on one board so far; `*-crowd`, `*-flood`,
`twatch-s3-loratx` and the `crowpanel7-*` extras are bench builds,
`cyd35` is the frozen 3.5" build, and `rlphantom` (the Phantom's
capacitive build) has never run on a real board.

**Don't know which 2.8" screen you have?** Nobody can tell from the
outside. Start with `cyd-fast`; a solid white screen means try `cyd`,
then `cyd-ili9341`.

## Build and flash

**From VS Code:**
1. Open the `SquachWatch-CYD` folder (File → Open Folder).
2. Click the PlatformIO sidebar icon (the alien-head).
3. Under "Project Tasks" → "cyd-fast" (or your board's environment) →
   "General" → click **Upload**.

**From the command line:**
```sh
pio run -e cyd-fast -t upload
```

Always name the environment with `-e`. A bare `pio run` builds the
default set (`cyd`, `cyd-ili9341`, `awok`), and a bare `pio run -t upload`
would try to flash all three to your one board.

The first build downloads the toolchain + libraries (~200 MB, takes
a few minutes). Subsequent builds are quick.

If asked to select a serial port, pick the one labeled
`USB-SERIAL CH340` (Windows), `/dev/cu.usbserial-*` (macOS), or
`/dev/ttyUSB0` (Linux).

The serial console on the CYD builds runs at **2,000,000 baud**
(`pio device monitor -e cyd-fast` picks that up from `platformio.ini`).
At 115200 it reads as garbage. The AWOK uses 921600, and the ESP32-S3
and C5 boards 115200.

## First boot

The CYD will reboot and:
1. Ask for a **touch calibration**: five targets, tap and hold each
   for a second, then one more dot to check it landed. Boards updating
   from older firmware get a **SKIP** button that keeps the touch they
   had. Walk away and it carries on booting and asks again next time.
2. Show the **SquachWatch splash** for about three seconds.
3. On the very first boot, show the **colour check**: the words RED,
   GREEN and BLUE. If they don't match their colours, tap **INVERT**
   and **ORDER** until they do, then **DONE**.
4. Drop into the **main screen** — Squachy the sasquatch on an animated
   background, live counters along the top, and the button bar, with a
   short walkthrough the first time.

That's it. You're running. Both checks can be redone later from
SETTINGS: **CALIBRATE TOUCH** and **CHECK COLORS**.

## Test it

To verify the detector works:
- Hold the CYD near **any BLE device** with the name `HC-05` or
  `HC-06` (an old BT speaker or a friend's Arduino). The `SKIMMER`
  counter should fire.
- Walk past a **Wyze, Hikvision or Axis** camera (`CAMERA`) or a
  **Ring** doorbell (`RING`). It should show up in the LOG.
- For Flock: walk past a Flock Safety ALPR (if you live in a city
  with them). Or use the [flock-spoof](https://github.com/0xD34D/flock-spoof)
  tool to broadcast a Flock probe request on a second ESP32.

If a microSD card is inserted (and FAT32-formatted), every detection
is also written to `squachwatch-<day>.log` on the card.

## Elecrow CrowPanel Advance 7.0 (ESP32-S3, 800×480 RGB)

Its own target, because nothing about it is a CYD:

```sh
pio run -e crowpanel7
pio run -e crowpanel7 -t upload
pio device monitor -b 115200
```

The console is a real UART through a CH340K (macOS needs WCH's driver);
there is no native USB. No touch calibration on first boot: the GT911
reports panel pixels. Power it from a supply that gives two amps -- the
panel's backlight plus WiFi joining a network browned a unit out on a
laptop port. See [CrowPanel notes](CROWPANEL7.md) for the pin map, why it
draws 400×240 doubled, and what has and has not been tested.

## Troubleshooting

### "A fatal error occurred: Failed to connect to ESP32"

The CYD isn't entering flash mode. Try:
1. **Hold the BOOT button on the back** of the CYD while plugging
   in the USB cable. Some boards need this to enter download mode.
2. **Try a different USB cable** — charge-only cables are the most
   common cause.
3. **Check the USB driver** (Windows): Device Manager → Ports (COM
   & LPT) → should show `USB-SERIAL CH340`. If it's missing, install
   the CH340 driver.
4. **Reduce upload speed** in `platformio.ini`: change
   `upload_speed = 921600` to `upload_speed = 115200`.

### Screen stays white / blank

Usually the wrong screen driver for your batch: flash `cyd` instead of
`cyd-fast`, and if it's still white, `cyd-ili9341` (see
[Which build?](#which-build)). Each environment pulls in its display
setup with `-include` in its `build_flags` in
[`platformio.ini`](../platformio.ini).

### "WiFi: Unknown" / detections not firing

The detection engine needs ~5–10 seconds to warm up after boot
(WiFi promiscuous mode + BLE scan initialization). If you walk past
a target immediately on boot, you may miss it.

### Out of memory / reboot loop

The **DIAGNOSTICS** screen shows free heap, the largest free block and
the last crash. After a crash the splash also holds for a few seconds
with the crash on it. Those are the things to put in a bug report.

## For developers

- **Host tests:** `make -C test` builds and runs the decoder and logic
  tests on your computer (just `g++` and `make`). CI runs them on every
  push to master and on pull requests.
- **Emulator:** `sim/` runs the firmware's real UI code on a PC and
  writes PNGs, so a screen change doesn't need a flash. See
  [sim/README.md](../sim/README.md); on Windows it runs inside WSL.
- **The C5 has its own toolchain.** `nm-cyd-c5` uses a different
  platform (pioarduino, Arduino 3.3) whose installer can break `pio`
  for every other board. Build it with its own `PLATFORMIO_CORE_DIR`.
- **One `pio run` at a time.** Two builds running at once can wipe
  each other's `.pio/build` folders.

## Next steps

- **Customize the matrix rain glyphs**: edit the `GLYPHS[]` string in
  `drawDigitalRain()` in `src/theme.cpp`. Add your own character set.
- **How long a sighting stays live**: `STALE_MS` (60 seconds) in
  `include/detection.h`. Some Flock cameras only probe every 30+
  seconds, so don't drop it much lower.
- **Add a new signature**: append to `kOuiTable` in
  `src/signatures.cpp`, with the matching `DetectionType` from
  `include/state.h` and a confidence grade (see
  [DETECTIONS.md](DETECTIONS.md)). Run `make -C test` — it checks for
  duplicate prefixes and labels too long for the screen — then
  re-flash.
- **Port to another board**: add an `[env:yourboard]` to
  `platformio.ini` with its own display header
  (`include/yourboard_user_setup.h`, pulled in with `-include`) and a
  `-D` flag for anything board-specific; touch, backlight and pin
  differences live in `#if` blocks keyed on that flag. After editing
  any `*_user_setup.h`, delete `.pio/build/<env>/lib*/TFT_eSPI` before
  building, or the board keeps the old display settings.
