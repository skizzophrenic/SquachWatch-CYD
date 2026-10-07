# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

ESP32 firmware (Arduino framework, PlatformIO) that detects surveillance hardware over WiFi and BLE and shows it on a small touch display. One shared codebase builds for about a dozen boards. The 2.8" "Cheap Yellow Display" (ESP32-2432S028R) is the main target. `docs/DESIGN.md` is the original design contract (data types, signature tables, palette, UI API shape). The README is the user-facing feature reference.

## Commands

Firmware (PlatformIO):

```sh
pio run                          # builds the default envs: cyd, cyd-ili9341, awok (what CI builds)
pio run -e freenove-s3           # one named board
pio run -e cyd -t upload         # build and flash
pio device monitor -e cyd        # CYD envs use 2,000,000 baud (set per env)
```

Host tests (plain g++, no framework, no board):

```sh
make -C test                       # build and run all
make -C test run-remote_id_test    # one test
make -C test clean                 # needed after a header edit: test rules only depend on .cpp files
make -C test crypto                # opt-in, needs libmbedtls-dev; not in CI
```

PC emulator (compiles the real `src/` UI against shims in `sim/`):

```sh
make -C sim -j8                                  # builds sim/squachsim and sim/squachsim-live
sim/squachsim clear out.png --bg 7 --frames 60   # render one screen to PNG
sim/squachsim                                    # no arguments prints the screens and options
make -C sim wasm                                 # browser build, needs em++ (emsdk)
cd sim && python3 gui.py                         # browser GUI on http://localhost:842 (Live and Gallery tabs)
```

CI (`.github/workflows/build.yml`) runs three jobs: `pio run` plus `crowpanel7` and `nm-cyd-c5`, `make -C test`, and the emulator build with one rendered frame.

## Build environments

- `[env]` in `platformio.ini` holds the shared config. Each `[env:xxx]` adds a TFT_eSPI setup header via `-include include/<board>_user_setup.h` and some `-D` board flags. Board differences in code are `#if defined(...)` blocks, mostly in `src/main.cpp`.
- The platform is pinned to `espressif32@6.5.0` (Arduino core 2.0.14). Core 3 removes the LEDC calls the backlight code uses, so do not unpin it as a side effect.
- `nm-cyd-c5` is the one exception: it uses a different platform (Arduino 3.3 / IDF 5.5) and a RISC-V toolchain. `extra_script.py` patches TFT_eSPI for it. `include/mbedtls_compat.h` covers the mbedtls 2 / 3 split.
- `cyd35` is frozen and boot loops on hardware. Do not build it to check a shared-code change, and ask before working on it (see the comment at the top of `platformio.ini`).
- `-fast`, `-crowd`, `-flood` and similar envs differ from their base env by one or two `-D` flags (benchmarks, faster SPI).
- `SQUACH_MESH=1` enables the board-to-board mesh features. Most shipping envs set it. Mesh code is wrapped in `#if SQUACH_MESH`.
- `extra_script.py` stamps `FIRMWARE_VERSION` from `git describe` and `SQW_ENV` from the env name. Bluetooth OTA images are signed per env (`tools/sign_firmware.py`, public key in `include/ota_pubkey.h`), so a board refuses an image built for another env.
- Partition tables (`partitions_*.csv`) give two app slots for OTA. There is no SPIFFS/LittleFS; storage is NVS (`Preferences`) and the SD card.

## Architecture

- `src/main.cpp` (about 8k lines) owns hardware init, `setup()`/`loop()`, touch handling, and the screen state machine. `AppState` in `include/state.h` lists every screen. `include/state.h` also holds `DetectionType`, `Detection` and `Confidence`.
- Screens are `src/ui_*.cpp` with a matching `include/ui_*.h`. Each exposes an init function, a per-frame tick that draws into `TFT_eSPI`, and a touch handler. `main.cpp` calls them based on `AppState`. Adding a screen means a new `AppState` value, the dispatch in `main.cpp`, and an entry in the `sim/Makefile` source lists.
- Detection: `detection.cpp` runs WiFi promiscuous mode and the NimBLE scan and matches against the tables in `signatures.cpp`. Each signature row carries a confidence grade. `remote_id.cpp` decodes ASTM F3411 drone Remote ID. Decoders are pure functions over byte buffers, which is why the host tests can link them alone.
- Rendering: `theme.cpp` (palette, backgrounds, icons, chrome), `squachy.cpp` (the mascot, outfits, lines), `pet.cpp`. Display output goes through `frame_push`/`fast_sprite`/`draw_band`. Large boards (CrowPanel 7") render at 400x240 and upscale.
- Persistence: `settings.cpp` over NVS, `blackbox.cpp` and `log_index.cpp` (detection log rings), `sd_log.cpp` (CSV on SD), `dex.cpp`, `regulars.cpp`, `ignore_list.cpp`.
- SquachMesh: `squachmesh.cpp` is the BLE advert wire format (read `include/squachmesh.h` first). `meshmsg.cpp` builds sealed frames, `meshcrypto.cpp` does AES-CCM, PBKDF2 and X25519, and `meshtalk.cpp`/`mesh.cpp` hold the session logic. `emote_script.cpp` scripts the shared animations, which must produce the same result on both boards.
- LoRa (`lora_*.cpp`, boards with a radio such as the T-Watch S3): packet capture plus decoders for Meshtastic, MeshCore, LoRaWAN, APRS and FANET. `docs/LORA.md` covers it.
- OTA: `ota_core.cpp` is shared. `ota_wifi.cpp` downloads release images. `ota_ble.cpp` takes signed images over Bluetooth.

## Emulator and tests share the shims

`sim/` holds stand-ins for `Arduino.h`, `TFT_eSPI.h`, `Preferences.h`, `SPI.h`, `Wire.h`, `XPT2046_Touchscreen.h` and the ESP headers. Both `sim/Makefile` and `test/Makefile` put `sim/` first on the include path. The emulator leaves out `main.cpp` for `squachsim` (it calls a screen's tick directly) and swaps `detection.cpp`/`sd_log.cpp` for `sim/detection_sim.cpp`. `squachsim-live` runs the firmware's own `setup()`/`loop()` with simulated taps.

Consequences when editing:

- A new `src/` file used by the UI must be added to the source lists in `sim/Makefile`, or the emulator fails to link.
- A new host test must be added to `TESTS` in `test/Makefile` with its `<name>_SRCS` (and `_FLAGS` if it needs `-DSQUACH_MESH=1`). Tests link only the units under test, never the display or radio code.
- A new Arduino or ESP API call in shared code may need a shim in `sim/`.

## Releases

- A `v*.*.*` tag triggers `.github/workflows/release-flasher.yml`. It builds every shipping board, merges flashable images, signs them for Bluetooth OTA, stamps the `web-flasher/manifest-*.json` files, creates the GitHub release and deploys the web flasher and the WASM emulator to Pages.
- The release fails without `.github/release-notes/vX.Y.Z.md`. Its first line can name the release: `<!-- name: Something -->`.
- The README and release GIFs in `docs/` are rendered by the emulator with `sim/make_*.py` scripts.
