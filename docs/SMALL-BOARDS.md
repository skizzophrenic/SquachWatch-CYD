# StickS3 and Cardputer ADV

Both boards have no touch screen. A cursor moves between the buttons on each
screen, and the button it is on lights up whole and shimmers. Lists scroll
under the cursor and loop, so you never page.

## M5Stack StickS3

| Do | Press |
|----|-------|
| Next | Front button, click |
| Select | Front button, hold |
| Back | Side button, click |
| Home | Side button, hold |

- **Shake** tosses Squachy. **Knock** (a tap or double tap on the case) pokes him.
- **Wiggle** wakes the screen. POWER SAVER is on by default and the screen goes dark after a minute.
- **FOB MODE** takes DESK's place on the home bar: big Squachy and alerts, no chrome, dark after a minute.
- No SECURITY menu and no typing. A PIN set by an older build never locks it.
- No GPS, no SD card, no wardriving.

## M5Stack Cardputer ADV

Not the original Cardputer.

| Do | Press |
|----|-------|
| Move | Arrows (`;` `,` `.` `/`) or TAB |
| Select | ENTER or SPACE |
| Back | ESC or DEL. On home, ESC opens the menu |
| Home | Hold ESC 3 seconds, from anywhere, even while typing |

Hotkeys (not while typing):

| Key | Where | What |
|-----|-------|------|
| E | anywhere | EXT screen on or off |
| B | anywhere | next background |
| S | home | SCAN picker. In it, B is BLE, W is WIFI, S closes it. On the WiFi scan, B is BLE |
| L | home | LOG |
| D | home | DESK |
| any letter | other screens | presses the button whose label starts with it. Two matches: the key steps between them and ENTER presses |

Every screen's letters, and the quick ways around without the EXT screen:
[CARDPUTER-HOTKEYS.md](CARDPUTER-HOTKEYS.md).

- Text screens (WiFi password, PIN, names, messages) type on the keyboard. DEL deletes, ENTER is OK. FN + arrow still moves.
- Knock, shake and wiggle work as on the StickS3. POWER SAVER is off by default here. Turn it on and a wiggle wakes the dark screen.
- Sound is short tones through its speaker. Settings > SOUND.
- SD card in its own slot: the log and wardrive files go there. Some old cards never answer; a new card works.

### EXT screen (the Cardputer XL mod)

The 2.8" ILI9341 wired to the EXT header as in the XL mod. Turn it on with
**E** or Settings > EXT SCREEN.

- Squachy and the full home screen move to the big screen.
- The built-in screen becomes a still home: the counts, the wardrive line and the hotkeys, including E to turn the big screen off.
- Turning it off leaves a card on the big screen: EXT SCREEN OFF, press E to bring Squachy back.
- When POWER SAVER dims the built-in screen, the big screen goes black and stops drawing until a key or a wiggle wakes it. Its backlight is wired to 5 V in the mod, so it stays lit.
- The panel cannot be detected (the mod does not wire its SDO), so the setting decides.

### GPS (M5Stack Unit GPS v1.1 on the Grove port)

Plug it into the Grove port. It runs at 115200 and the board finds it on its own.

- **WARDRIVE** in Settings logs every WiFi network and Bluetooth device it hears, with position, to a WiGLE file on the SD card. Rows are written only with a real fix.
- The top bar shows a wardrive tag while it is on: green `WD 1.2K` (rows written), amber `WD 3 SAT` (waiting, satellites heard), red `NO GPS` or `NO CARD`.
- The WARDRIVE row says the same: FINDING 3 SATS, FIX 9 SATS, NO GPS.
- **WARDRIVE SOUND** (on by default): two rising beeps when the fix comes, a falling slide when it goes, a soft tick as rows are written.
- **gps-status.log** on the card gets one line a minute while wardrive is on: satellites, fix, rows, losses and memory. Until the GPS sets the clock, lines show time since boot, then get the real time.
- The module says ANTENNA OPEN every second. Its antenna is built in; ignore it.
- The first fix after a power-off can take a few minutes outside. Indoors it may never come.

### Without the extras

| Setup | What you see |
|-------|--------------|
| No GPS | No GPS tag. Turn WARDRIVE on anyway and the tag reads `NO GPS` |
| No EXT screen | The normal home on the built-in screen. Turned on by mistake? Press E |
| No SD card | WARDRIVE reads `NO CARD` |
