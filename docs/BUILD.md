# Building SquachWatch-CYD

The friendliest possible walkthrough. If you have a CYD board and
a computer, you can flash this in about ten minutes.

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

## Build and flash

**From VS Code:**
1. Open the `SquachWatch-CYD` folder (File → Open Folder).
2. Click the PlatformIO sidebar icon (the alien-head).
3. Under "Project Tasks" → "cyd" → "General" → click **Upload**.

**From the command line:**
```sh
pio run -t upload
```

The first build downloads the toolchain + libraries (~200 MB, takes
a few minutes). Subsequent builds are quick.

If asked to select a serial port, pick the one labeled
`USB-SERIAL CH340` (Windows), `/dev/cu.usbserial-*` (macOS), or
`/dev/ttyUSB0` (Linux).

## First boot

The CYD will reboot and:
1. Show the **SquachWatch splash** for 1.5 seconds.
2. Drop into the **clear screen** — matrix digital rain, ghost
   avatar, live counters, three soft buttons.

That's it. You're running.

## Test it

To verify the detector works:
- Hold the CYD near **any BLE device** with the name `HC-05` or
  `HC-06` (an old BT speaker or a friend's Arduino). The `SKIMMER`
  counter should fire.
- Walk past a **Ring / Wyze / Nest / Eufy** camera. The `CAMERA`
  counter should fire.
- For Flock: walk past a Flock Safety ALPR (if you live in a city
  with them). Or use the [flock-spoof](https://github.com/0xD34D/flock-spoof)
  tool to broadcast a Flock probe request on a second ESP32.

If a microSD card is inserted (and FAT32-formatted), every detection
is also written to `squachwatch-<day>.log` on the card.

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

The TFT_eSPI user setup is wrong. Confirm `cyd_user_setup.h` is
being `-include`'d by `platformio.ini`. See the
[`build_flags`](../platformio.ini) section.

### "WiFi: Unknown" / detections not firing

The detection engine needs ~5–10 seconds to warm up after boot
(WiFi promiscuous mode + BLE scan initialization). If you walk past
a target immediately on boot, you may miss it.

### Out of memory / reboot loop

The detection engine and the matrix rain are tight on memory, but
should fit. If you see reboots, try:
- Reduce the matrix rain column count in `src/theme.cpp` (look for
  `RAIN_X[14]`) from 14 to 10.
- Disable NimBLE logging in `src/detection.cpp`.

## Next steps

- **Customize the matrix rain glyphs**: edit the `GL[]` string in
  `src/theme.cpp`. Add your own character set.
- **Tune the detection threshold**: some Flock cameras only probe
  every 30+ seconds; if you're missing them, increase
  `STALE_MS` in `include/detection.h`.
- **Add a new signature**: append to `kOuiTable` in
  `src/signatures.cpp`, with the matching `DetectionType` from
  `include/state.h`. Re-flash.
- **Port to another board**: the firmware is portable to any
  ESP32 + ILI9341 board. Replace `cyd_user_setup.h` and the touch
  pin config in `src/main.cpp`.
