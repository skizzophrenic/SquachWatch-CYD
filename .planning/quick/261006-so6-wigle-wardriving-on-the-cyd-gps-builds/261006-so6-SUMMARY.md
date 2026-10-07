---
phase: quick-261006-so6
plan: 01
subsystem: wardrive
tags: [cyd-gps, wardrive, wigle, sd-card, emulator]
requires: [quick-261006-l11]
provides: [WARDRIVE-CYD]
affects: [src/detection.cpp, src/main.cpp, src/ui_settings.cpp, src/ui_diagnostics.cpp, sim/]
tech-stack:
  added: []
  patterns: [heap buffers made only while a feature is on, a live flag with an in-flight counter for freeing under radio tasks]
key-files:
  created:
    - include/wardrive_capture.h
    - src/wardrive_seen.cpp
    - src/wardrive_sd.cpp
    - test/wardrive_seen_test.cpp
    - sim/gps_drive.nmea
  modified:
    - include/wardrive.h
    - src/wardrive.cpp
    - src/wardrive_fmt.cpp
    - src/detection.cpp
    - src/main.cpp
    - src/ui_settings.cpp
    - include/ui_diagnostics.h
    - src/ui_diagnostics.cpp
    - test/Makefile
    - test/wigle_test.cpp
    - sim/Makefile
    - sim/Arduino.h
    - sim/detection_sim.cpp
    - sim/main_sim.cpp
    - docs/PINOUT.md
decisions:
  - The CYD reuses the watch's WARDRIVE row and its "wardrive"/"on" setting
  - The WiGLE header names the hardware brand per board; the CYD writes brand=Sunton, model=ESP32-2432S028R
  - One file handle, opened and closed per flush, so the detection log and the WiGLE file stay inside SD_MAX_FILES = 2
metrics:
  completed: 2026-10-06
  tasks: 3
  commits: 5
---

# Quick Task 261006-so6: WiGLE wardriving on the CYD GPS builds

A cyd-gps or cyd-ili9341-gps board with WARDRIVE on, a card and a real GPS fix now writes every WiFi network and Bluetooth device it hears to `/wigle-YYYYMMDD-HHMM.csv`, in the WigleWifi-1.6 format the watch already writes. The other boards' RAM and flash did not change.

## What was built

- The capture queue, the repeat rule (5 minutes or 50 m) and the distance function moved to `include/wardrive_capture.h`. The watch's flash store uses them from there and is no longer compiled into the CYD GPS builds.
- `src/wardrive_seen.cpp`: the CYD's 256-entry repeat table in four blocks of 64. When it is full, the entry written longest ago makes room.
- `src/wardrive_sd.cpp`: the CYD writer. The queues, the table and a 1 KB row buffer are made only while WARDRIVE is on with a card in (six allocations, none over 1,442 bytes) and freed when it goes off. Below an 8 KB largest heap block it pauses and counts what it drops. Rows need a fresh fix that is not a bench fix. A row waits at most 2 s for the card. The duress wipe deletes every `/wigle-*.csv`.
- The radio callbacks feed it on CYD_GPS. The Bluetooth name and company ID are read from the advert bytes, with no allocation on the Bluetooth task.
- The console gives WARDRIVE ON, WARDRIVE OFF, WIGLE (where the file is) and a wardrive line in GPS STATUS. Settings has a WARDRIVE row after POWER SAVER. DIAGNOSTICS has WARDRIVE and WIGLE lines.
- The emulator: `SQUACHSIM_CONSOLE` types console lines into squachsim-live, `SQUACHSIM_SD` names a host directory for the card, the stand-in engine reports four networks and two tags a second, and `sim/gps_drive.nmea` drives north at 10 m/s for two minutes.

## Commits

| Commit | What |
|---|---|
| f28800e | test: the repeat table's host test (fails: no implementation) |
| ba556f7 | feat: the repeat table; the watch store takes the shared pieces |
| 8cf60ac | feat: the SD writer, the radio hooks, console, boot, wipe, the Settings tap |
| c3b6b77 | feat: Settings row, DIAGNOSTICS lines, emulator drive, docs |
| 3f1cc5b | style: plain first lines in the new files |

## Sizes

Baseline from a clean tree at a67ad3b, final from a clean tree at 3f1cc5b (cyd-ili9341-gps and twatch-s3 final at c3b6b77; the later commit changed comments only).

| Env | RAM before | RAM after | Flash before | Flash after |
|---|---|---|---|---|
| cyd | 94,216 | 94,216 | 1,741,253 | 1,741,253 |
| cyd-ili9341 | 94,216 | 94,216 | 1,741,381 | 1,741,381 |
| awok | 93,680 | 93,680 | 1,729,257 | 1,729,257 |
| cyd-gps | 94,400 | 94,496 (+96) | 1,745,873 | 1,753,957 (+8,084) |
| cyd-ili9341-gps | 94,400 | 94,496 (+96) | 1,746,013 | 1,754,093 (+8,080) |
| twatch-s3 | 113,464 | 113,464 | 1,853,657 | 1,853,685 (+28) |

cyd, cyd-ili9341 and awok match to the byte. The watch's 28 bytes come from the new brand argument to headerLines(); its header text is unchanged and wigle_test passes. The CYD GPS builds' static RAM grew by 96 bytes (pointers, counters and the 32-byte file name), more than the plan's "a few dozen".

## Checks

- `make -C test`: 44 suites, all passed, including wardrive_seen_test, wardrive_test and wigle_test.
- `pio run` for all six envs: success. These are compile checks.
- `make -C sim -j8`: both binaries build.

Emulator drive run (`SQUACHSIM_CONSOLE='WARDRIVE ON'`, `sim/gps_drive.nmea`, 4,800 frames):

```
[wardrive] off, rows go to the SD card
[wardrive] ON: rows go to the SD card once the GPS has a real fix
[gps] FIRST FIX after 15 s, 8 satellites
file: wigle-20261006-1200.csv
WigleWifi-1.6,appRelease=sim,model=ESP32-2432S028R,release=sim,device=SquachWatch,display=SquachWatch,board=cyd-gps,brand=Sunton,star=Sol,body=3,subBody=0
MAC,SSID,AuthMode,FirstSeen,Channel,Frequency,RSSI,CurrentLatitude,CurrentLongitude,AltitudeMeters,AccuracyMeters,RCOIs,MfgrId,Type
rows: 144
  24 02:53:51:00:00:01
  24 02:53:51:00:00:02
  24 02:53:51:00:00:03
  24 02:53:51:00:00:04
  24 02:53:51:00:01:01
  24 02:53:51:00:01:02
distinct latitudes: 24
02:53:51:00:00:01,SimNet-1,[WPA2-PSK-CCMP][ESS],2026-10-06 12:00:01,1,2412,-58,-33.8565766,151.2150000,25,5,,,WIFI
```

144 rows from 720 sightings: each device once per 50 m over 1,200 m.

Emulator bench-fix run (`WARDRIVE ON;GPS FAKE -33.857 151.215;GPS STATUS`, no NMEA):

```
[wardrive] ON: rows go to the SD card once the GPS has a real fix
[gps] BENCH FIX at -33.8570000,151.2150000: SD rows written now carry FAKE, and the clock is left alone
[gps] status: sentences 0 good, 0 bad; in view 0, heard 0, used 0; no real fix yet; bench fix at -33.8570000,151.2150000
[wardrive] WAITING FOR FIX; file none yet; written 0, skipped 0 as repeats, dropped 0
no wigle file with a bench fix
```

Renders: DIAGNOSTICS with `--gps` shows `WARDRIVE: ON, 42 written, 310 repeats, 0 dropped` and `WIGLE: /wigle-20261006-1200.csv`. Settings shows WARDRIVE OFF under the SYSTEM heading, below POWER SAVER.

## Deviations from Plan

1. Orchestrator amendment: `headerLines()` takes a `brand` argument. The watch passes "LilyGo" (its header is unchanged; wigle_test passes with only the extra argument), and the CYD passes "Sunton" with model "ESP32-2432S028R". The plan's note that the CYD header still says brand=LilyGo no longer applies.
2. [Rule 3] Typed console input made the one-shot renderer `squachsim` fail to link: clock.cpp's console commands name 26 globals that only main.cpp defines. clock.cpp is now compiled a second time for squachsim-live with `SQUACHSIM_SERIAL_IN=1`, the one build where Serial has input; squachsim keeps the shared copy with no input. `sim/detection_sim.cpp` gained an empty `logDump()`, which the live clock.cpp needs. Files: sim/Makefile, sim/Arduino.h, sim/detection_sim.cpp.
3. [Rule 1] All console lines read in one pass overwrote each other, since GPS and WARDRIVE commands share one slot that loop() empties. The emulator serves one line each 500 ms, as typed.
4. [Rule 2] The SD wipe deletes in passes of 16 until a pass comes back short, so a card with more than 16 WiGLE files is fully cleared.
5. [Rule 1] If a radio task is still pushing after the bounded wait, the free path keeps the buffers (unreachable, since the live flag is already false) and tries again on the next tick. Deleting them then would free memory still in use.
6. The emulator's networks carry real security bits (three WPA2, one open), so the AuthMode column is filled in the drive run.
7. The first baseline build overlapped the test/Makefile edit. The baseline was rebuilt from a clean tree, with the same numbers.

No plan acceptance criteria were dropped under the testing rules. The one host test is the one the spec names.

## Known limits

- In the emulator the Settings tap toast says "Needs an SD card", because the emulator's detection log never reports a card. The writer itself uses `SQUACHSIM_SD`.

## Hardware checks for the user

1. Flash cyd-ili9341-gps on the board at /dev/cu.wchusbserial1120 with a card in. Settings shows WARDRIVE OFF. Tap it: WAITING FOR FIX indoors, and NO CARD with the card out.
2. Outdoors with the module, after a fix the row reads ON with a rising count, and DIAGNOSTICS shows written, repeats and the file name. Note HEAP free and largest with WARDRIVE on and off.
3. Drive a few kilometers. On a computer, the file starts with the WigleWifi-1.6 line and the column line, and wigle.net accepts the upload.
4. GPS FAKE on the bench with WARDRIVE on: the row stays WAITING FOR FIX and no file appears.
5. With WARDRIVE on, the detection log and alert cards behave as before, and DIAGNOSTICS shows whether wardriving paused for memory in a dense place.
6. The duress wipe deletes the wigle files from the card.

## Self-Check: PASSED

Files and commits checked below.
