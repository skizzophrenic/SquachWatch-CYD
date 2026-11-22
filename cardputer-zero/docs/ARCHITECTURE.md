# Architecture — CM0 build

## 1. Process model

One `squachd` process; one thread per I/O domain:

```
                    +-------------------+
 AF_PACKET raw 80211|  wifi thread      |  monitor vif, nl80211 channel hop
 (mon0, PASSIVE)    |        +----------+----+ promiscuous-rx callback
                    +-------------------+        v
 raw HCI LE reports |  ble thread       |  +---------------------------+
 (hci0, passive)    |        +----------+--->| detection engine        |
                    +-------------------+    | (real detection.cpp,    |
 sim framebuffer   |  ui thread —      |    | unchanged; OUI/SSID/     |
 (plain array,     |  firmware loop,   |<-- | deauth, EVILTWIN,        |
  320x170x16)      |  rendering only  |    | Remote ID, tag checks)   |
        +---------->+                  |    +---------------------------+
        |           +-------------------+
        v                 |
 SDL2 window on      SDL keyboard -> firmware keyboard input path
 labwc (320x170)     (evdev event3 fallback)
                    ALSA (on demand) -> firmware sound samples
```

The firmware's main loop, state machine, detection engine, and UI run
**unchanged**. Linux provides: the two I/O feeds, the framebuffer blit,
keyboard events, audio playback, file storage, and boot.

## 2. Source reuse map

From the sim and test Makefiles (both already build these files on a host):

- **Compiles unchanged (host-proven):** all `ui_*.cpp`, `theme`, `squachy`,
  `mesh`, `meshmsg`, `meshwords`, `meshtalk`, `meshtutor`, `squachmesh`,
  `emote_script`, `crowd_bench`, `qwerty`, `security`, `clock`, `signatures`,
  `remote_id`, `ibeacon`, `device_info`, `ignore_list`, `blackbox`,
  `log_index`, `sound` (behind the shim), `lora_*` (only if Phase 7).
- **Compiles unchanged with the radio shim (new on CM0, previously sim-only):**
  `detection.cpp` — the sim excludes it in favor of the `detection_sim.cpp`
  stand-in only because the sim fakes frames. On the CM0 the shim delivers
  real frames, so the stand-in is retired.
- **New Linux backends (the only new C++ in this project):**
  radio shim (the contract in §4), SDL2 display (Wayland window on labwc),
  keyboard input (SDL scancodes; evdev fallback), ALSA audio, SD/preferences
  files, OTA swap, main bootstrap.
- **Excluded on CM0:** the ESP32 `main.cpp` (setup/loop/hardware init —
  replaced by `main_linux.cpp`); touch code (no touch on the board).

## 3. Shims

Reused from the firmware's `sim/`, extended where noted:

- `Arduino.h` — millis/delays/Serial (Serial → stderr + file log).
- `TFT_eSPI.h` — keep the drawing API; the backend is a blit into an SDL2
  texture instead of the sim window. The sim's framebuffer is a plain array,
  so the backend is a swap of the blit target, not a rewrite of the
  rasterizer.
- `Preferences.h` — sim's RAM flash, now file-backed (`sd_file.cpp`).
- `SPI.h`/`Wire.h` — not needed on the CM0; the panel is driven by the OS's
  own DRM/SPI stack, the app only draws into a window.

## 4. Radio shim contract

The exact API surface used by `detection.cpp` + `main.cpp` (verified by grep):

- **esp_wifi:** `set_promiscuous`, `set_promiscuous_rx_cb`,
  `set_promiscuous_filter`, `get_promiscuous`, `set_channel`, `get_channel`,
  `set_mode`, `get_mode`, `set_band_mode`, `start`, `stop`, `init`, `dein`,
  `scan_start`, `scan_get_ap_records`, `scan_get_ap_num`, `clear_ap_list`,
  `get_max_tx_power`, `get_country`, `esp_wifi_netif`
- **Arduino `WiFi.*`:** `mode`, `scanNetworks`, `scanComplete`, `SSID`,
  `scanDelete`
- **NimBLE:** `BLEDevice`, `BLEScan` (incl. `scanPassiveNow` — the firmware
  already prefers passive scanning), `BLEAdvertisedDevice` (RSSI, MAC,
  service UUIDs, service data), `BLEUUID`, `BLEAdvertising` +
  `BLEAdvertisementData` (squad hellos), `BLEAddress`

Backends (design in `RADIO.md`):

- promiscuous → monitor vif + `AF_PACKET` raw frames
- `set_channel` → nl80211 frequency set
- `scanNetworks` → managed-mode scan on wlan0 (for network lists/NTP)
- `BLEScan` → raw HCI passive LE scan
- `BLEAdvertising` → LE advertising with the squad payload

The detector is blind to which path feeds it (A: onboard, B: USB adapter,
C: ESP32 forwarder) — that is the payoff of the existing frame-ingress
abstraction.

## 5. Display (decided: SDL2 window on labwc)

The stock OS runs **labwc** (Wayland, `-m` mini mode) + swayidle, which
holds the DRM master on the 320×170 SPI panel (fb0 is just the kernel's view
of the same plane). Decision:

- **Window mode, not fb0 takeover.** One SDL2 window, borderless, 320×170,
  on the panel output — effectively fullscreen, but the compositor stays in
  charge. Exit = close the window; the OS UI is back immediately. Taking the
  framebuffer would require killing labwc — that is a kiosk, not an app.
- `sdl_display.cpp`: create the window; blit the sim framebuffer array into
  an SDL texture each frame (~109 KB, nothing for a CM0; throttle to the
  panel refresh).
- Input: SDL keyboard events (scancodes) mapped to the firmware's keyboard
  commands; if SDL misses a key (vendor-keyboard quirks), fall back to raw
  evdev on `/dev/input/event3` (tca8418c — see HARDWARE.md).
- The "if no fb0: write an SPI ST7789v3 driver" contingency is dead — the
  OS already drives the panel.
- UI: `cardputer_zero_user_setup.h` with `TFT_WIDTH 320`, `TFT_HEIGHT 170`;
  layout-fit pass. Precedent: the 135×240 Cardputer ADV variant proves the
  layout code tolerates tight screens; 170 px of height mostly means fewer
  LOG rows and a compacted alert card.

## 6. Build

- `cardputer-zero/Makefile` compiling the firmware sources from the enclosing
  tree (`SQUACHWATCH ?= ..`, the same pattern as `sim/`) plus this port's
  `src/cm0/` files; sim shims on the include path.
- Toolchain: **aarch64** — the stock image is Debian 13 aarch64 (confirmed by
  G0 probe; the official CM0 is 32-bit-only, but this board runs 64-bit).
  Cross-compile with `gcc-aarch64-linux-gnu` on the desktop, or build in an
  aarch64 container. 512 MB + 768 MB zram on the device is fine for running,
  not for full builds — build on the desktop, run on the device.
- The host test suite (`test/`) stays green throughout; it is
  the regression gate for the detection engine, which runs byte-identical on
  the CM0.

## 7. OTA (app-level only)

The firmware's `ota_wifi` flow (join a network, download the release for the
exact board, verify signature, trial with rollback) ports 1:1; the "write to
flash" step becomes an **app self-update**: verify → stage in a versioned
dir → swap the launcher pointer → relaunch. The OS image is never touched.
Signing reuses the project's key via `tools/sign_firmware.py`; rollback =
point the launcher back at the previous version.

## 8. Running as an application

SquachWatch is an app on the stock OS — not a kiosk, not an OS replacement.
No kernel-parameter changes, no forced auto-start, no persistent framebuffer
ownership.

- **Launch:** a `.desktop` entry on the stock image and/or a listing in the
  Cardputer Hub app store; runs as the normal user. **Decided (G0 retest):**
  the stock OS runs labwc (Wayland), so the app renders into a borderless
  SDL2 window on the panel — window mode, not fb0 takeover (the compositor
  holds the DRM master; taking the framebuffer would kill the session, which
  is not "an app on the stock OS").
- **Lifecycle hygiene (the part that makes it a citizen, not a squatter):**
  on exit the app must leave the machine exactly as it found it — delete the
  monitor vif, return `wlan0` to managed, stop the LE scan, close the ALSA
  stream, release the display. The OS's Wi-Fi has to work the moment
  SquachWatch quits; Phase 6's clean-exit test verifies this.
- **Coexistence:** the app is a few tens of MB of RAM; the CM0's 512 MB is
  shared with the OS. Fine in practice; note it, don't assume the whole
  machine.
- **Crash:** log to `/var/log` + SD; no OS-level recovery needed — the OS is
  already the supervisor.
