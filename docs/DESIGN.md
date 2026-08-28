# SquachWatch-CYD — Design Contract

This document is the single source of truth for the SquachWatch-CYD firmware.
All implementation tracks MUST conform to the interfaces, color palette, file
layout, and signature tables defined here.

## 1. Hardware Target

- **Board:** ESP32-2432S028R ("Cheap Yellow Display" / CYD)
- **MCU:** ESP32-WROOM-32 (single Xtensa LX6 core, 240 MHz, 320 KB RAM, **no PSRAM**)
- **TFT:** ILI9341 / ILI9342C, 320×240, SPI
- **Touch:** XPT2046 resistive, SPI
- **SD card slot:** the CYD has one onboard (SPI, shared bus). On boot we
  detect the SD card; if present, we write a timestamped event log to
  `/squachwatch-YYYYMMDD.log` (one CSV line per detection:
  `ts,type,rssi,mac,channel,vendor,ssid`). No GPS, so logs are local-timeline
  only — but they are useful for documenting incident history. If the card
  is absent, logging is silently disabled and the rest of the firmware is
  unaffected.
- **No extras otherwise:** no buzzer, no GPS, no battery monitoring.
- **Input:** touch only (no keyboard, no buttons wired to GPIOs in user code)

## 2. Build System

- **Toolchain:** PlatformIO
- **Platform:** `espressif32` (version pinned: `~6.5.0` or latest stable)
- **Framework:** `arduino`
- **Board:** `esp32dev`
- **Upload speed:** `921600`
- **Dependencies:**
  - `TFT_eSPI` (Bodmer) — main display driver
  - `XPT2046_Touchscreen` (Paul Stoffregen) — touch driver
  - `NimBLE-Arduino` — BLE scanning (lighter than the BLE library)
- **Memory model:** Arduino-ESP32, no PSRAM. Be conservative with large buffers.

### `platformio.ini` (target)

```ini
[env:cyd]
platform = espressif32
board = esp32dev
framework = arduino
upload_speed = 921600
monitor_speed = 115200
lib_deps =
    bodmer/TFT_eSPI@^2.5.43
    paulstoffregen/XPT2046_Touchscreen@^1.4
    h2zero/NimBLE-Arduino@^1.4.0
build_flags =
    -DCYD
    -DUSER_SETUP_LOADED=1
    -include $PROJECT_DIR/include/cyd_user_setup.h
```

### `include/cyd_user_setup.h`

A TFT_eSPI user setup header for the CYD's ILI9341 + XPT2046 pinout.
Pin map (verify against current Sunton schematic; the values below match
the most common "ESP32-2432S028R" variant — the "USB-C 2" rev has slightly
different pins, which is documented in `docs/PINOUT.md`):

| Signal    | GPIO |
|---|---|
| TFT_MOSI  | 27 |
| TFT_SCLK  | 14 |
| TFT_CS    | 15 |
| TFT_DC    |  2 |
| TFT_RST   |  4 |
| TFT_BL    | 21 |
| TOUCH_CS  | 33 |
| TOUCH_IRQ | 36 |
| TOUCH_MOSI| 32 |
| TOUCH_MISO| 39 |
| TOUCH_SCLK| 25 |
| SD_CS     |  5 |
| SD_MOSI   | 23 |
| SD_MISO   | 19 |
| SD_SCLK   | 18 |

The SD card shares the VSPI bus with the TFT (different CS lines). On boot
the firmware calls `SD.begin(SD_CS)`; if it returns false, SD logging is
disabled and everything else works normally.

## 3. File Layout

```
SquachWatch-CYD/
├── platformio.ini
├── README.md
├── LICENSE                       (MIT)
├── docs/
│   ├── DESIGN.md                 (this file)
│   ├── BUILD.md                  (build + flash guide)
│   ├── PINOUT.md                 (CYD pin map)
│   └── DETECTIONS.md             (signature sources + license notes)
├── include/
│   ├── cyd_user_setup.h          (TFT_eSPI config for CYD)
│   ├── detection.h               (DetectionEngine class)
│   ├── signatures.h              (OUI/UUID/name tables — generated)
│   ├── theme.h                   (color tokens, draw helpers)
│   ├── state.h                   (AppState enum, shared state)
│   ├── ui_boot.h
│   ├── ui_clear.h
│   ├── ui_alert.h
│   └── ui_log.h
├── src/
│   ├── main.cpp                  (state machine, ties everything together)
│   ├── detection.cpp
│   ├── signatures.cpp            (the OUI/UUID/name data)
│   ├── theme.cpp
│   ├── ui_boot.cpp
│   ├── ui_clear.cpp
│   ├── ui_alert.cpp
│   └── ui_log.cpp
```

## 4. Public Data Types (`include/state.h`)

```cpp
#pragma once
#include <stdint.h>

enum class DetectionType : uint8_t {
    UNKNOWN   = 0,
    FLOCK     = 1,   // Flock Safety camera / sensor
    AXON      = 2,   // Axon body camera / LE equipment
    META      = 3,   // Ray-Ban Meta smart glasses
    SKIMMER   = 4,   // HC-05/06/03 Bluetooth skimmer
    RAVEN     = 5,   // Raven gunshot detector
    AIRTAG    = 6,   // Apple AirTag
    DRONE     = 7,   // OpenDroneID drone
    ALPR      = 8,   // Motorola / Vigilant ALPR
    CAMERA    = 9,   // Generic camera (existing OUI list from Cardputer code)
    COUNT     = 10
};

inline const char* detectionTypeName(DetectionType t) {
    switch (t) {
        case DetectionType::FLOCK:   return "FLOCK";
        case DetectionType::AXON:    return "AXON";
        case DetectionType::META:    return "META";
        case DetectionType::SKIMMER: return "SKIMMER";
        case DetectionType::RAVEN:   return "RAVEN";
        case DetectionType::AIRTAG:  return "AIRTAG";
        case DetectionType::DRONE:   return "DRONE";
        case DetectionType::ALPR:    return "ALPR";
        case DetectionType::CAMERA:  return "CAMERA";
        default:                     return "UNKNOWN";
    }
}

struct Detection {
    uint8_t        mac[6];
    int8_t         rssi;
    uint8_t        channel;        // 0 if N/A
    DetectionType  type;
    char           vendor[12];     // e.g. "Flock-Ext", "Wyze", "ESP32"
    char           name[20];       // BLE name or SSID, truncated
    uint32_t       firstSeen;
    uint32_t       lastSeen;
    uint16_t       hits;           // observation count (for dedupe)
    bool           active;
};

enum class AppState : uint8_t {
    BOOT   = 0,    // ~1.5s splash
    CLEAR  = 1,    // matrix-rain idle / "no detections"
    ALERT  = 2,    // full-screen detection overlay
    LOG    = 3     // list of recent detections
};

// Touch button IDs
enum class ButtonId : uint8_t {
    SCAN = 0,
    LOG  = 1,
    CLR  = 2
};
```

## 5. Detection Engine API (`include/detection.h`)

```cpp
#pragma once
#include "state.h"

class DetectionEngine {
public:
    bool     init();                              // set up WiFi promiscuous + BLE + BT Classic
    void     loop();                              // call every tick; non-blocking
    void     clearLog();                          // wipe the rolling log
    uint8_t  logCount() const;                    // # entries in log
    const Detection* logAt(uint8_t idx) const;    // 0 = newest
    const Detection* latest() const;              // most recent active detection (for ALERT)
    // Live counters (atomic reads)
    uint16_t countByType(DetectionType t) const;
private:
    // internal buffers
};
```

`init()` does:
- `WiFi.mode(WIFI_STA)`; set promiscuous mode with mgmt + data filter
- Register promiscuous Rx callback (WiFi sniff)
- Init NimBLE, start BLE scan
- (Optional) issue BT Classic inquiry every N seconds for skimmer names

`loop()` does:
- Drain pending promiscuous frames
- Drain pending BLE scan results
- Periodically expire stale detections (`active=false` if `now - lastSeen > 60s`)

## 6. Signature Tables (`include/signatures.h`)

```cpp
#pragma once
#include "state.h"

struct OuiEntry   { uint8_t  b[3];     const char* name; DetectionType type; };
struct UuidEntry  { uint16_t uuid;     const char* name; DetectionType type; };
struct NameEntry  { const char* name;  DetectionType type; };
struct SsidEntry  { const char* prefix; const char* name; DetectionType type; };
struct MfgIdEntry { uint16_t mfgId;    const char* name; DetectionType type; };

// Implemented in src/signatures.cpp
extern const OuiEntry    kOuiTable[];
extern const uint16_t    kOuiCount;
extern const UuidEntry   kUuidTable[];
extern const uint16_t    kUuidCount;
extern const NameEntry   kBtClassicNames[];
extern const uint16_t    kBtClassicCount;
extern const SsidEntry   kSsidPrefixes[];
extern const uint16_t    kSsidCount;
extern const MfgIdEntry  kMfgIdTable[];
extern const uint16_t    kMfgIdCount;

// Helpers. Each returns DetectionType::UNKNOWN if no match.
DetectionType lookupOui(const uint8_t* mac);
DetectionType lookupUuid(uint16_t uuid16);
DetectionType lookupBtName(const char* name);
DetectionType lookupSsid(const char* ssid);   // case-insensitive prefix match
DetectionType lookupMfgId(uint16_t mfgId);
```

### 6.1 OUI / UUID / SSID / mfg-ID sources

Signatures are aggregated from three independent research streams. The
implementation MUST include every entry below; provenance lives in
`docs/DETECTIONS.md`.

#### 6.1.1 Flock Safety (~30 OUIs) — from `colonelpanichacks/flock-you`
research by `@NitekryDPaul` and `DeFlockJoplin`. ESP32-module OUIs in
Flock hardware plus their LTE backhaul module OUIs.

```
Flock OUIs (alphabetical, all lowercase in source):
  00:a0:d8   00:f4:8d   14:5a:fc   24:0a:c4   24:6f:28
  24:b2:b9   30:ae:a4   34:85:18   3c:91:80   70:c9:4e
  80:30:49   82:6b:f2   84:f3:eb   8c:aa:b5   94:b9:7e
  a4:cf:12   ac:67:b2   b4:1e:52   b4:e6:2d   b8:35:32
  c0:49:ef   cc:50:e3   cc:db:a7   d0:39:57   d4:ad:fc
  d8:f3:bc   dc:54:75   e0:0a:f6   e8:9f:6d
```

`24:6F:28` and `CC:50:E3` are also flagged in Gemini's research as Espressif
modules commonly found in Flock/IoT deployments; the table must contain both.

#### 6.1.2 Axon / Taser (3 OUIs + 4 SSID prefixes)

```
OUIs:  00:25:DF   (legacy Taser International)
       E4:05:40   (modern Axon body cams & docks)
       28:24:FF   (Axon Signal / Body 3 network)

SSID prefixes (case-insensitive, prefix match):
  AB2-    (Axon Body 2 active pairing)
  AB3-    (Axon Body 3 active pairing)
  AB4-    (Axon Body 4 active pairing)
  AXON-   (generic Axon field network)
```

#### 6.1.3 Card skimmers (3 OUIs + BT Classic names + SPP UUID)

```
OUIs:  20:13:00   (Linvor serial module prefix)
       98:D3:00   (generic Bluetooth SPP module)
       00:1A:7D   (CSR generic chip)

BT Classic names (exact match): HC-03, HC-05, HC-06, RN42, BT04-A

BLE 16-bit service UUID: 0x1101   (Standard Serial Port Profile — classic
                                    skimmers expose SPP for the thief's
                                    phone to pull dumps)
```

#### 6.1.4 Meta Ray-Ban Smart Glasses — 16-bit UUID `0xFD5F`

#### 6.1.5 Raven gunshot detector — 5 custom service UUIDs
`0x3100`, `0x3200`, `0x3300`, `0x3400`, `0x3500`
(also a manufacturer ID `0x09C8` for XUNTONG, the BLE-radio supplier)

#### 6.1.6 AirTag / Apple FindMy — manufacturer ID `0x004C`
Match when first 4 bytes of manufacturer data are `1E FF 4C 00` or
`4C 00 12` (Near-owner vs separated state subtype).

#### 6.1.7 Tile tracker — 16-bit UUID `0xFEED`
(Categorized as TRACKER, not AIRTAG, but worth flagging for personal
security context.)

#### 6.1.8 OpenDroneID drone — 16-bit UUID `0xFFFA` (ASTM F3411 Remote ID)

#### 6.1.9 Motorola / Vigilant ALPR — OUI `00:0E:58`

#### 6.1.10 Generic / covert IP cameras (from `skizzophrenic/Cardputer-CSI-Human-Detector`
plus Gemini additions)

```
ESP32 family:   24:0A:C4  30:AE:A4  24:6F:28  DC:54:75  E8:9F:6D  8C:AA:B5  34:85:18
Wyze:           2C:AA:8E  D0:3F:27  7C:78:B2  B8:D7:AF  (last is the Wyze wireless module)
Ring:           FC:65:DE  68:37:E9
Amazon:         34:D2:70
Hikvision:      F0:27:2D  C0:56:E3  44:19:B6
Reolink:        28:57:BE
Realtek:        00:E0:4C
Arlo:           BC:DD:C2
Blink:          4C:69:05
Tuya (covert):  A4:C1:38
```

#### 6.1.11 Flock BLE setup beacon (added from Gemini)
- BLE name (substring match): `Flock_Setup`
- BLE name (substring match): `FS Ext Battery`
- Manufacturer ID: `0x09C8` (XUNTONG)

### 6.2 Lookup precedence

`lookupOui` MUST scan the table in this priority order so that a MAC that
matches both a generic ESP32 OUI and a Flock-specific deployment pattern
resolves to `FLOCK`:

1. Axon (any match) → `AXON`
2. Flock OUIs (any match) → `FLOCK`
3. Motorola/Vigilant → `ALPR`
4. Specific camera vendors (Wyze, Ring, Arlo, Blink, Reolink, Hikvision, Amazon, Tuya) → `CAMERA`
5. ESP32 family OUIs (any match) → `CAMERA` (the device has the WiFi capability
   of a camera-class IoT device, even if we can't pin the brand)
6. Skimmer OUIs (Linvor, SPP, CSR) → `SKIMMER`
7. No match → `UNKNOWN`

The signature data MUST be ordered to satisfy this precedence; the table is
walked in order and the first match wins.

## 7. Theme Tokens (`include/theme.h`)

The user's SquachWare CSS palette, mapped to RGB565 for the CYD's TFT.

```cpp
#pragma once
#include <TFT_eSPI.h>

namespace Theme {
    // SquachWare CSS tokens → RGB565
    constexpr uint16_t BG           = 0x0801;  // #0a000f
    constexpr uint16_t TASKBAR      = 0x0803;  // #0d001a
    constexpr uint16_t PURPLE       = 0xAC1F;  // #b400ff  (SquachWare --purple / --win-border)
    constexpr uint16_t CYAN         = 0x07FF;  // #00fff5
    constexpr uint16_t PINK         = 0xF96F;  // #ff2d78
    constexpr uint16_t VAPOR_PINK   = 0xFB99;  // #ff71ce
    constexpr uint16_t VAPOR_PURPLE = 0xBB5F;  // #b967ff
    constexpr uint16_t VAPOR_BLUE   = 0x067F;  // #01cdfe
    constexpr uint16_t VAPOR_YELLOW = 0xFFD2;  // #fffb96
    constexpr uint16_t GREEN        = 0x07E0;  // #00ff88  (TFT_GREEN)
    constexpr uint16_t AMBER        = 0xFD20;  // alert-yellow
    constexpr uint16_t RED          = 0xF800;  // TFT_RED
    constexpr uint16_t WHITE        = 0xFFFF;
    constexpr uint16_t BLACK        = 0x0000;

    // Color by threat level (used in log rows)
    uint16_t colorFor(DetectionType t);

    // SquachWare-style titlebar gradient simulation (purple→cyan→pink)
    uint16_t titlebarColor(int x, int w);

    // Standard draw helpers
    void drawTitleBar(TFT_eSPI& t, const char* title);
    void drawButton(TFT_eSPI& t, int x, int y, int w, int h, const char* label, bool pressed);
    void drawScanline(TFT_eSPI& t, int y, uint16_t color = VAPOR_PURPLE);
    void drawPulsingBorder(TFT_eSPI& t, uint32_t now, uint16_t a, uint16_t b, uint8_t thick = 4);
    void drawGhostAvatar(TFT_eSPI& t, int cx, int cy, uint32_t now);  // the cute vapor ghost

    // SquachWare typography (best-effort on CYD):
    //   "header" → use Font4 (large blocky) or scaleFont for big titles
    //   "body"   → use Font2 (small monospace, VT323-like)
    //   "mono"   → use Font1 (1-pixel monospace, like Share Tech Mono)
    //   (See src/theme.cpp for the font picker)
}
```

### SquachWare typography on the CYD

The CYD's TFT_eSPI font set is limited; we approximate the SquachWare font
stack as follows (declared in `src/theme.cpp`):

| SquachWare role | CYD font | Notes |
|---|---|---|
| Big header (Orbitron 900) | `Font4` or `Font6` | Bold, blocky. Used for `SQUACHWATCH v1.0`, `DETECTION`, target names. |
| Body / labels (VT323) | `Font2` | CRT terminal look. Status pills, button labels, log rows. |
| Mono / data (Share Tech Mono) | `Font1` | MAC, RSSI, channel numbers. |

If rendering the splash title as `SQUACHWATCH` in `Font4` doesn't fit the
320 px width, the UI is allowed to fall back to `Font2` for that one line.

## 8. UI Module APIs

Each UI screen exposes:

```cpp
// src/ui_boot.cpp
void uiBootInit(TFT_eSPI& t);
void uiBootTick(TFT_eSPI& t, uint32_t now);   // draw next frame; returns when BOOT should end
bool uiBootDone(uint32_t startMs);            // true if boot should yield

// src/ui_clear.cpp
void uiClearInit(TFT_eSPI& t);
void uiClearTick(TFT_eSPI& t, uint32_t now, const DetectionEngine& eng);

// src/ui_alert.cpp
void uiAlertInit(TFT_eSPI& t, const Detection& d);
void uiAlertTick(TFT_eSPI& t, uint32_t now);
bool uiAlertTouched(const TouchPoint& tp);    // user dismissed; go back to CLEAR

// src/ui_log.cpp
void uiLogInit(TFT_eSPI& t);
void uiLogTick(TFT_eSPI& t, uint32_t now, const DetectionEngine& eng, int scrollOffset);
void uiLogTouch(const TouchPoint& tp);        // scroll / select / back
```

## 9. State Machine (in `main.cpp`)

```
        ┌────────┐ 1.5s timer
   ────▶│  BOOT  ├──────┐
        └────────┘      ▼
                  ┌────────┐  detection    ┌────────┐
            ┌────▶│ CLEAR  │──────────────▶│ ALERT  │
            │     └────────┘               └────────┘
            │       ▲   ▲ tap [LOG]            │ tap to
            │       │   └──────────────┐      │ dismiss
            │ tap   │                  ▼      │ / 5s timer
            │ [CLR] │             ┌────────┐  │
            └───────┴─────────────│  LOG   │◀─┘
                                  └────────┘
```

- `BOOT` runs once at startup, ~1500 ms.
- `CLEAR` is the idle state. It shows the matrix rain + ghost avatar + live
  counters. A new detection transitions to `ALERT`.
- `ALERT` shows the full-screen dramatic overlay. Auto-dismisses after 5 s,
  or earlier if the user taps anywhere.
- `LOG` shows the rolling log (last 32). Tap `[SCAN]` to return to `CLEAR`.
- Tap `[CLR]` from anywhere returns to `CLEAR` and wipes the log.

## 10. Touch Buttons

Bottom-of-screen soft buttons, drawn in `CLEAR` and `LOG` states:

| Button | Position (320×240) | Hit box | Action |
|---|---|---|---|
| `[SCAN]` | x=10..105, y=210..232 | 95×22 | Force-return to `CLEAR` |
| `[LOG]`  | x=115..210, y=210..232 | 95×22 | Open `LOG` |
| `[CLR]`  | x=220..315, y=210..232 | 95×22 | Clear log + return to `CLEAR` |

Buttons are styled in SquachWare chrome: cyan label, purple 1 px border,
filled with `BG`; on press, swap to `PURPLE` background with white text.

## 11. Aesthetic Rules (SquachWare)

These are non-negotiable visual rules — any UI code that violates them gets
sent back.

- **Background** is `Theme::BG` (`#0a000f`), NOT pure black. Pure black
  appears only inside the deepest ALERT overlay regions.
- **Titlebars** use the purple→cyan→pink gradient (simulated with
  `titlebarColor(x, w)`).
- **Status pills** are cyan text on `Theme::TASKBAR` background with a 1 px
  purple top border.
- **Threat colors**: `GREEN` for clear, `VAPOR_YELLOW` for info / skimmer,
  `AMBER` for warn, `PINK` for high (FLOCK, AXON, META), `RED` for critical.
- **Log rows** are colored by `Theme::colorFor(type)`.
- **Logos / wordmarks**:
  - Splash title text: `SQUACHWATCH v1.0` (caps, all one word, no hyphen
    on splash). Subtitle: `SQUACH WATCH` in vapor-purple. The ALERT
    screen's glitchy bottom wordmark reads `SQUACHWATCH`.
  - Always write "Sasquach" — never "Sasquatch".
  - Sasquach silhouette in the splash, standing near the horizon, drawn
    in purple outline, ~40 px tall. Pure decorative, no text on it.

## 12. Boot Order

```cpp
void setup() {
    Serial.begin(115200);
    tft.init();
    tft.setRotation(0);              // 320×240 landscape
    tft.fillScreen(Theme::BG);
    touch.begin();
    touch.setRotation(0);
    bootStart = millis();
    state = AppState::BOOT;
    // engine init is deferred until CLEAR to keep splash snappy
}

void loop() {
    uint32_t now = millis();
    touch.poll();
    switch (state) {
        case AppState::BOOT:
            if (!engine) engine = new DetectionEngine();
            uiBootTick(tft, now);
            if (uiBootDone(bootStart)) {
                engine->init();
                state = AppState::CLEAR;
                uiClearInit(tft);
            }
            break;
        case AppState::CLEAR:
            uiClearTick(tft, now, *engine);
            if (const Detection* d = engine->latest()) {
                uiAlertInit(tft, *d);
                state = AppState::ALERT;
            }
            handleBottomButtons(tp);
            break;
        case AppState::ALERT:
            uiAlertTick(tft, now);
            if (uiAlertTouched(tp) || (now - alertStart > 5000)) {
                state = AppState::CLEAR;
                uiClearInit(tft);
            }
            break;
        case AppState::LOG:
            uiLogTick(tft, now, *engine, scroll);
            uiLogTouch(tp);
            handleBottomButtons(tp);
            break;
    }
}
```

## 13. License & attribution

**MIT.** Match the user's existing projects (`Cardputer-CSI-Human-Detector`,
`M5PORKCHOP_DualScreen`). Full text in `LICENSE`.

Per-section attribution lives in `docs/DETECTIONS.md`:

- Flock OUI research: `@NitekryDPaul`, `DeFlockJoplin`, `colonelpanichacks/flock-you`
  (MIT). SquachWatch-CYD redistributes the OUI list as data (not code); credit
  is preserved in the docs.
- Axon / skimmer / SSID prefix data: generated by Gemini (Google) at the
  user's request, used as a starting point and expanded against public
  sources.
- Cardputer-derived generic-camera OUIs: `skizzophrenic/Cardputer-CSI-Human-Detector`
  (MIT).
- AirTag manufacturer-data format: public Apple FindMy spec, also
  documented in `ESP32Marauder` (GPL-2; we use the *format description*
  not the code).

## 14. What's OUT of scope (v1.0)

- No buzzer / audio alerts
- No SD card logging
- No GPS
- No web UI / WiFi AP config portal
- No OTA updates
- No multi-screen navigation gestures beyond the three buttons
- No localization (English only)

These can land in v1.1+ if the user wants them.
