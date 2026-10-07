# Quick Task 261006-so6: WiGLE wardriving on the CYD GPS builds - Context

**Gathered:** 2026-10-06
**Status:** Ready for planning. The user approved the spec below and asked for the task to run to completion without a stop for plan approval.

<domain>
## Task Boundary

A person with a CYD GPS build switches WARDRIVE on, drives, pulls the SD card and uploads the file to wigle.net as it is.

Builds on quick task 261006-l11 (same branch, feature/cyd-gps): `cyd-gps` and `cyd-ili9341-gps` envs with `-DCYD_GPS=1`, Serial2 GPS on GPIO35, `src/sd_row.cpp`, GPS FAKE / GPS STATUS console commands, the emulator's Serial2 NMEA replay (`SQUACHSIM_NMEA`, `sim/gps_sample.nmea`). Read its SUMMARY: `.planning/quick/261006-l11-add-optional-gps-support-to-the-cyd-buil/261006-l11-SUMMARY.md`.

The T-Watch already wardrives: `include/wardrive.h`, `src/wardrive.cpp` (capture queues, the Seen dedupe table, raw-flash storage), `src/wardrive_fmt.cpp` (WiGLE 1.6 header and CSV line, host-tested in `test/wigle_test.cpp` and `test/wardrive_test.cpp`). The radio callbacks already call `Wardrive::noteBle` (src/detection.cpp ~355) and `Wardrive::noteWifi` (~794), today under `TWATCH_S3`.

</domain>

<decisions>
## Implementation Decisions (approved spec)

1. Builds: only the CYD GPS builds get it, guarded by `CYD_GPS`. Every other build compiles none of it; cyd, cyd-ili9341 and awok RAM and flash must not change. The T-Watch wardrive behavior must not change.
2. Switch: a WARDRIVE row in Settings on the CYD GPS builds, off by default, persisted (reuse the watch's row and setting if practical). Console WARDRIVE ON / WARDRIVE OFF work on the CYD GPS build.
3. Captured: every WiFi network and every Bluetooth device the radios hear, not only signature matches. Rows only with a real fresh GPS fix. A GPS FAKE bench fix is never written on the CYD.
4. Repeats: write a device again after 5 minutes or 50 m from its last row (the watch's AGAIN_S / AGAIN_M rule). The CYD's Seen table holds 256 entries (the watch keeps 1024).
5. File: WiGLE format written straight to the SD card, one file per session named for its start time in UTC, `/wigle-YYYYMMDD-HHMM.csv`, with WiGLE's two header lines. Reuse `Wardrive::headerLines` and `Wardrive::csvRow`. No raw-flash store on the CYD (4 MB flash, two OTA slots).
6. RAM: the wardrive buffers (Seen table plus queues, about 5 KB) are allocated only while WARDRIVE is on and freed when it goes off. If the largest free heap block falls under 8 KB (the scanner's existing SCAN_FLUSH_BLOCK_B), wardriving pauses and counts what it dropped, so detection keeps running.
7. Visible: DIAGNOSTICS gets a WARDRIVE line: rows written, skipped as repeats, dropped, and the file name. The Settings row shows NO CARD without a card and WAITING FOR FIX until a fresh real fix.
8. Unchanged: the watch's wardriving, the detection SD log, alert cards, and the 261006-l11 GPS work.
9. Tests: one host test of the CYD-sized repeat table: a network heard many times while standing still writes one row; the same network after 50 m or 5 minutes writes again; a full 256-entry table still works (oldest replaced). The WiGLE line format is already tested.
10. Emulator: give the NMEA replay a moving track (a second sample file, or extend the sample) so squachsim-live writes rows as the position changes. The emulator needs an SD stand-in that writes to a host directory if it has none; check what sim/ already provides before adding one.

Out of scope: uploading to wigle.net from the board; wardriving on non-GPS builds.

### Claude's Discretion
- How to split wardrive.cpp so the CYD uses the capture and repeat logic with an SD writer (a storage backend split, or a CYD-only writer beside it).
- Whether the WARDRIVE switch reuses Settings::WATCH_WARDRIVE storage or gets its own key.
- Exact SD write batching (write each row and close, like the detection log, or keep the file open with periodic flush). Mind SD_MAX_FILES = 2: the detection log and the wigle file may both need a handle.

</decisions>

<specifics>
## Measurements to respect

- On the user's CYD (cyd-ili9341-gps, SD card mounted, WiFi and BLE up), heap at the first loop was 26,764 free with a 10,740 largest block. Without a card: 43,520 / 27,636. Allocations must be small and fixed. No std::string or vector growth on the radio tasks.
- Flash: cyd-gps uses 1,745,785 of 1,966,080 bytes.
- The user's board needs the cyd-ili9341 display driver; it is at /dev/cu.wchusbserial1120.

</specifics>

<canonical_refs>
## Canonical References

- ./CLAUDE.md
- include/wardrive.h, src/wardrive.cpp, src/wardrive_fmt.cpp, test/wardrive_test.cpp, test/wigle_test.cpp
- src/detection.cpp (radio callbacks, SCAN_FLUSH_BLOCK_B), src/sd_log.cpp (SD_MAX_FILES, the mount), src/main.cpp (cydGpsTick, the T-Watch wardrive console path), src/ui_settings.cpp (WATCH_WARDRIVE row), src/ui_diagnostics.cpp
- sim/Arduino.h (Serial2 replay), sim/Makefile, sim/detection_sim.cpp

## Rules for this work (from the user's global instructions)

- Testing: before writing a test, state in one plain sentence what a user gets wrong without it. No tests of copy, docs, schema or the Makefile; no assertions over source text; no hand-made "mutation" steps. Call the real function.
- Writing (code comments, commit messages, docs): no em dashes, American spelling, no "it's not X, it's Y" or "X rather than Y" constructions, no bold-first bullets, no unicode arrows, never "trap" or "canary". Match the surrounding comment style.
- Commit messages end with the line: Claude-Session: https://claude.ai/code/session_01EG7yuyjE5K1Z6ZaXWKTxqV

</canonical_refs>
