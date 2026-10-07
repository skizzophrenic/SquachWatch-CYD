# SquachWatch-CYD

## What This Is

ESP32 firmware that detects surveillance hardware (Flock cameras, body cameras, trackers, skimmers, drones and more) over WiFi and BLE, and shows each catch on a small touch display. One codebase builds for about a dozen boards. The main board is the 2.8" Cheap Yellow Display (ESP32-2432S028R). See README.md and CLAUDE.md.

## Core Value

A person carrying the board sees, on the spot, which surveillance devices are near them.

## Constraints

- PlatformIO, Arduino framework, platform pinned to espressif32@6.5.0 (the NM-CYD-C5 env is the one exception).
- The CYD has 4 MB flash split into two OTA app slots and no PSRAM.
- The PC emulator (sim/) and host tests (test/) compile src/ against shims; new sources must be added to their Makefiles.
- CI builds the default envs, crowpanel7 and nm-cyd-c5, runs make -C test and builds the emulator.

## Key Decisions

| Decision | Date | Notes |
|----------|------|-------|
| GSD set up with a minimal stub, not /gsd-new-project | 2026-10-06 | Enough for quick tasks; can be expanded later |
