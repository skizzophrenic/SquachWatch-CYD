# SquachWatch-CYD

> Surveillance-device detector for the ESP32-2432S028R ("Cheap Yellow Display").
> Built for the **TALKING SASQUACH** brand family.

SquachWatch-CYD sniffs the 2.4 GHz airwaves for known wireless signatures
of Flock Safety cameras, Axon body cameras, recording glasses, card
skimmers, AirTags, drones, and more. It runs standalone on a bare CYD board
— no PC, no extras, just plug it into USB.

The UI is a vaporwave-themed take on the **SquachWare** aesthetic: matrix
digital rain, Squachy the mascot, full-screen dramatic ALERT overlays, and
the glitchy SquachWatch wordmark.

## What it detects

| Type | What | How |
|---|---|---|
| `FLOCK` | Flock Safety ALPR / Raven sensors | WiFi OUI (28 prefixes) + BLE name + manufacturer ID |
| `AXON` | Axon body cameras, tasers, LE equipment | WiFi OUI (3 prefixes) + SSID prefix `AB2-`/`AB3-`/`AB4-`/`AXON-` |
| `META` | Ray-Ban Meta smart glasses | BLE service UUID `0xFD5F` |
| `SKIMMER` | Bluetooth card skimmers (HC-05/06/03, RN42, BT04-A) | BT Classic name match + SPP UUID `0x1101` |
| `RAVEN` | Raven gunshot detector | Custom service UUIDs `0x3100`–`0x3500` |
| `AIRTAG` | Apple AirTag / FindMy trackers | Manufacturer ID `0x004C` + subtype `0x12`/`0x1E` |
| `DRONE` | OpenDroneID-compliant drones | BLE service UUID `0xFFFA` |
| `ALPR` | Motorola / Vigilant ALPR | WiFi OUI `00:0E:58` |
| `CAMERA` | Generic / covert IP cameras | WiFi OUI list (Wyze, Hikvision, Reolink, Arlo, Blink, Tuya, Verkada, Avigilon, Axis, etc.) |
| `SAMSUNG_TAG` | Samsung Galaxy SmartTag / SmartTag+ | BLE service UUID `0xFD5A` |
| `GOOGLE_TAG` | Google Find My Device Network trackers (Chipolo, Pebblebee, Moto Tag) | BLE service UUID `0xFEAA` |
| `TILE` | Tile BLE trackers | BLE service UUID `0xFEED` / `0xFEEC` |
| `RING` | Ring doorbells / cameras | WiFi OUI (15 prefixes, Ring LLC's full registered block) |

## Hardware

- **ESP32-2432S028R** ("Cheap Yellow Display" / CYD) — about $15.
  Built-in 320×240 ILI9341 TFT, XPT2046 resistive touch, and an
  onboard microSD card slot.

That's it. No buzzer, no GPS, no extra modules. The CYD is the
whole device.

## Web Flash

No build tools, no IDE, no cloning anything — flash a board straight
from your browser:

**[https://skizzophrenic.github.io/SquachWatch-CYD/](https://skizzophrenic.github.io/SquachWatch-CYD/)**

Works in Chrome, Edge, or Brave on desktop. Plug in, click Connect &
Install, done.

## Build

Three steps:

1. Install [PlatformIO](https://platformio.org/) (CLI or VS Code extension).
2. Clone the repo:
   ```sh
   git clone https://github.com/skizzophrenic/SquachWatch-CYD
   cd SquachWatch-CYD
   ```
3. Build and flash:
   ```sh
   pio run -t upload
   ```

The first build pulls the TFT_eSPI, XPT2046, and NimBLE-Arduino
libraries; after that it's incremental.

A full beginner-friendly walkthrough is in [docs/BUILD.md](docs/BUILD.md).

## Usage

1. Plug the CYD into USB-C.
2. The `SQUACHWATCH v1.0` splash runs for 1.5 seconds.
3. The clear screen appears: matrix rain, ghost avatar, live counters.
4. The three soft buttons at the bottom:
   - **`[ SCAN ]`** — return to the clear (idle) screen.
   - **`[ LOG ]`** — open the rolling 32-entry detection log.
   - **`[ CLR ]`** — wipe the log and return to clear.
5. When something is detected, the device **flashes a full-screen
   ALERT** — pulsing vapor-pink border, target type, confidence,
   MAC, RSSI, channel, a signal radar, and the glitchy `SQUACHWATCH`
   wordmark. Tap anywhere to dismiss early, or it clears itself after
   60 seconds.

If a microSD card is present, every detection is also appended to
`squachwatch-<day>.log` (CSV: `ts,type,rssi,mac,channel,vendor,ssid`).
No GPS, so logs are local-timeline only — but they're useful for
documenting incident history.

## Screenshots

Screenshots will land here after the first hardware test. No
fabricated marketing shots — what you see on a real CYD is what
you'll get.

## Project layout

```
SquachWatch-CYD/
├── platformio.ini
├── README.md
├── LICENSE
├── docs/
│   ├── DESIGN.md                 (the contract — single source of truth)
│   ├── BUILD.md                  (friendly walkthrough)
│   ├── PINOUT.md                 (CYD pin map)
│   ├── DETECTIONS.md             (per-signature provenance)
│   └── SQUACHWARE-AESTHETIC.md   (CSS → RGB565 mapping)
├── include/
│   ├── state.h
│   ├── theme.h
│   ├── signatures.h
│   ├── detection.h
│   ├── sd_log.h
│   ├── cyd_user_setup.h          (TFT_eSPI config for the CYD)
│   └── ui_*.h
└── src/
    ├── main.cpp
    ├── theme.cpp
    ├── signatures.cpp
    ├── detection.cpp
    ├── sd_log.cpp
    └── ui_*.cpp
```

## License

**Common Public License 1.0 (CPL-1.0).** See [LICENSE](LICENSE).

## Credits

- Flock Safety OUI research: [@NitekryDPaul](https://x.com/NitekryDPaul),
  DeFlockJoplin, [`colonelpanichacks/flock-you`](https://github.com/colonelpanichacks/flock-you)
  (MIT).
- Generic-camera OUI table: [`skizzophrenic/Cardputer-CSI-Human-Detector`](https://github.com/skizzophrenic/Cardputer-CSI-Human-Detector)
  (MIT, this author's earlier work).
- Axon / skimmer / SSID prefix data: compiled with assistance from
  Gemini (Google), expanded against public sources.
- AirTag manufacturer-data format: public Apple FindMy spec.
- The SquachWare vaporwave aesthetic and `TALKING SASQUACH` brand
  belong to **skizzophrenic / Talking Sasquach** — see
  [talkingsasquach.com](https://talkingsasquach.com) and the
  [SquachWare-CFW](https://github.com/skizzophrenic/SquachWare-CFW)
  project.
- Vibes: also skizzophrenic, who vibecoded most of this at unreasonable
  hours with an AI doing the typing. Yes, the same guy credited above
  for "research." Make of that what you will.

## Status

**v1.0 — first release.** Detection is reliable for the high-priority
targets (Flock, Axon, skimmer, Meta). AirTag and Raven are best-effort.
Drone and generic ALPR are flags for further investigation — see
[docs/DETECTIONS.md](docs/DETECTIONS.md) for per-target confidence.

A verified-flashing build on real hardware is the v1.0 exit criterion.
No real-hardware verification has been done yet — open an issue if
you test it and find bugs.
