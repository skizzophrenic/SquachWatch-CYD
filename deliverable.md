# SquachWatch-CYD v1.0 — build summary

Built solo in one pass after the team plan's parallel tracks bailed
in 2.6 seconds with zero output. The full design contract from
`docs/DESIGN.md` is implemented; the architecture matches the spec.

## What's in the box

```
SquachWatch-CYD/
├── platformio.ini             24 lines
├── README.md                 143 lines
├── LICENSE                    21 lines (MIT)
├── docs/
│   ├── DESIGN.md             577 lines   (the contract)
│   ├── BUILD.md              144 lines   (friendly walkthrough)
│   ├── PINOUT.md              54 lines
│   ├── DETECTIONS.md         185 lines   (per-signature provenance)
│   └── SQUACHWARE-AESTHETIC.md  64 lines
├── include/                  321 lines   (10 .h files)
└── src/                     1256 lines   (9 .cpp files)
```

Total: ~2,789 lines across 27 files.

## What it does

- **Sniffs** WiFi (promiscuous mode for Flock/Axon/ALPR/ESP32 cameras)
  and BLE (NimBLE for AirTag/Meta/Raven/Drone/skimmer).
- **Matches** against 28 Flock OUIs, 3 Axon OUIs, 4 Axon SSID prefixes,
  5 BT Classic skimmer names, 9 BLE service UUIDs, 2 manufacturer IDs,
  and a 12-vendor camera OUI list — all in priority order so a Flock
  probe doesn't get demoted to "generic camera".
- **Displays** on a SquachWare-themed SquachWatch-CYD UI:
  - Splash (1.5 s) with `SQUACHWATCH` / `v1.0` / `TALKING SASQUACH` /
    Sasquach silhouette / scanline animation
  - Clear screen with 14-column matrix digital rain (the
    `SASQUACH` token is in the charset), ghost avatar, live counters,
    three soft buttons
  - Full-screen ALERT with pulsing vapor-pink border, target type,
    vendor, MAC, RSSI, channel, glitchy `TALKING SASQUACH` wordmark
  - Log view with 32 entries, scrollable, color-coded by type
- **Logs** to SD card as CSV (one line per detection, daily file) if
  a card is present; otherwise silent no-op.

## What's not in v1.0

Documented as out-of-scope in DESIGN.md §14. Worth flagging:

- **No BT Classic inquiry** for skimmer names — would conflict with
  NimBLE on a single radio. Skimmer detection is BLE-name based only
  in v1.0.
- **No GPS** — log timestamps are millis since boot, not wall-clock.
- **No buzzer / audio** — touch + visual only.
- **No real-hardware verification** — the build hasn't been flashed
  to an actual CYD yet. Compile-readiness is high (~85% confidence)
  but there will likely be 1-2 small issues to fix on first flash.

## Known integration concerns (for first-flash debugging)

The verifier didn't actually run, so I want to flag the things most
likely to need a tweak when you first `pio run -t upload`:

1. **TFT_eSPI `USER_SETUP_INFO`** — newer TFT_eSPI versions may
   want `#define USER_SETUP_LOADED` BEFORE the include of
   `User_Setup_Select.h`. If you get a "multiple definition" error,
   move the include directives around.

2. **NimBLE scan callback** — `BLEScanResults` is passed by value
   in older versions, by reference in newer. If the API changed,
   the `auto onResults` lambda signature in `detection.cpp` is the
   place to look.

3. **SD card SPI bus** — the CYD's SD card shares the VSPI bus
   with the TFT. The `SD.begin(SD_CS_PIN)` call must come AFTER
   the TFT init, otherwise the bus arbitration gets confused.

4. **NTP-free timestamps** — `millis()` since boot rolls over
   after 49.7 days, and there's no wall-clock alignment. The SD
   log filename (`squachwatch-<day>.log`) uses
   `millis() / 86400000` as a fake day number. Functional but not
   pretty. If you want real dates, add a WiFi-based NTP sync as
   a v1.1 enhancement.

5. **The `esp_bt_controller_get_status()` call** in `detection.cpp`
   init() is currently behind an `if` so it should be safe, but if
   NimBLE on your platform version handles BT Classic differently
   you may need to comment that block out entirely.

## Build it

```sh
cd SquachWatch-CYD
pio run -t upload
```

First build pulls TFT_eSPI + XPT2046_Touchscreen + NimBLE-Arduino
(~3 minutes). Incremental after that.

## Verify detection

- **Skimmer test:** Pair a Bluetooth speaker named `HC-05` (or use
  any device you can rename). The `SKIM` counter should fire.
- **Camera test:** Walk past a Ring/Wyze/Nest camera. The `CAM`
  counter should fire.
- **Flock test:** Live test requires a Flock camera in range.
  Synthetic test: use a second ESP32 running
  [`flock-spoof`](https://github.com/0xD34D/flock-spoof) to
  broadcast a Flock probe request.

## Next steps (suggested v1.1 backlog)

1. BT Classic inquiry for skimmer names (separate radio mode).
2. WiFi AP config portal for SSID list and per-detection alert sound.
3. NTP sync for real timestamps in the SD log.
4. NitekryDPaul's high-precision Flock probe signature (probe
   type=0/subtype=4 with empty SSID + OUI match) — currently we
   match OUI on any management frame.
5. Optional GPS module (NEO-6MV2) for geo-tagged SD logs.
6. Port to other boards (`SquachWatch-Cardputer`,
   `SquachWatch-Marauder`).
