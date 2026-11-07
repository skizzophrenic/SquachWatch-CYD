# SHARE LOGS: the logs to a computer over WiFi

On the builds made with `-DSQW_LOG_SHARE=1` -- the 2.8" CYD (`cyd`,
`cyd-ili9341`), the Freenove 3.2" (`freenove32`) and the 3.5" resistive
ESP32-3248S035R (`cyd35-fast`) --
**Settings → SYSTEM → SHARE LOGS** joins your saved WiFi network and serves
the board's logs as a small web page. No cable, no card reader.

## What you get

| File | What it is |
|---|---|
| `blackbox.csv` | The black box in flash: the boot and crash history, then every kept detection (time, type, MAC, RSSI, channel, hits, vendor, name). The same text `BLACKBOX` prints on the serial console. Two tables in one file, each with its own header line. |
| `squachwatch-YYYYMMDD.log` | The SD card's daily detection log (CSV), one per day. Only when a card is in. |
| `squachwatch-nodate.log` | Rows logged before the clock was set. |
| `wigle-*.csv` | Wardrive files, on the GPS builds. |

## Using it from Windows 10

1. Save your network first: **Settings → SYSTEM → WIFI NETWORKS → ADD**.
2. Tap **SHARE LOGS**. The board pauses detection, joins the network and shows
   an address, a user (`squach`) and a six-digit code.
3. On the PC, on the same network, open the address in a browser, for example
   `http://192.168.1.50/`. Enter the user and the code when it asks, then
   click a file to download it.

Or, from PowerShell or Command Prompt, with the `curl.exe` that ships with
Windows 10:

```
curl.exe -u squach:123456 -o blackbox.csv http://192.168.1.50/blackbox.csv
curl.exe -u squach:123456 -o today.log "http://192.168.1.50/f?n=squachwatch-20261026.log"
```

Tap **STOP** when you're done. Detection starts again straight away.

While it's on the network it also asks a time server for the time, as the
boot update check does, so the clock and the SD log's dates are right even
with **UPDATE CHECK** off.

## What guards it

- The board has to be unlocked to start sharing, so the PIN is needed when
  one is set.
- Every request needs the code on the screen. The code is new each time
  sharing starts. Five wrong codes stop the share.
- Sharing stops on its own after **5 minutes with no request**, after
  **20 minutes** whatever happens, and if it fails (it leaves the failure
  screen after a minute).
- It is plain HTTP. Anyone on the same network can see what is sent, so
  share on a network you trust, not public WiFi.

## While it runs

- Detection is paused. The WiFi radio cannot sniff and serve at the same
  time; this is the same pause an update over WiFi takes.
- Files are sent from the main loop, so the screen freezes while a big one
  goes out, typically a few seconds.
- The screen stays awake. The timers above bound how long.

## On the 3.5" (cyd35-fast)

The SD slot is on its own SPI bus (pins 18/19/23, CS 5), so the card mounts
and its logs are offered alongside `blackbox.csv`.

The WiFi stack needs more memory than the 3.5" has spare with its screen
buffer allocated, so the buffer is handed over while sharing runs: the
share screen redraws without it and may flicker. When sharing stops the
buffer can't be taken back in one piece, so the board **restarts** (a few
seconds; nothing is lost).

## One catch

An official update (over WiFi or from the web flasher) replaces this build
with the release, which does not have SHARE LOGS. Reflash this build
afterwards, or turn **UPDATE CHECK** off on the SYSTEM page so the board
doesn't offer the release.
