# SquachWatch for the Cardputer Zero

[SquachWatch](https://github.com/skizzophrenic/SquachWatch-CYD) — the passive
Wi-Fi/Bluetooth watcher that turns surveillance cameras, trackers, deauth
floods and wardriving into a game — running as an app on the **M5Stack
Cardputer Zero** (Raspberry Pi CM0, Debian 13 "trixie", 320×170 screen,
46-key keyboard, ES8389 speaker).

It is an app on the stock OS, not a replacement: it starts from the
Cardputer Zero's launcher, takes the screen and keyboard while it runs, and
leaves the machine as it found it on exit (Wi-Fi adapter restored, backlight
restored, Bluetooth scan off).

This folder builds the SquachWatch firmware around it (`..`, the rest of
this repo) **unmodified**: no firmware file is copied or patched. What lives
here is the Cardputer Zero side: the display/keyboard harness, the Linux radios (Wi-Fi monitor mode, raw
Bluetooth HCI), sound over ALSA, the motion sensor, GPS, storage and the shims
that let the ESP32 firmware build on Linux.

- [What you need](#what-you-need)
- [Build](#build)
- [Test](#test)
- [Install on the Cardputer Zero](#install-on-the-cardputer-zero)
- [The speaker fix (one-time OS change)](#the-speaker-fix-one-time-os-change)
- [Using it](#using-it)
- [After a firmware change](#after-a-firmware-change)
- [Layout](#layout)

---

## What you need

**On the Cardputer Zero**

- The stock M5Stack OS (Debian 13 trixie, arm64) with its app launcher
  (`APPLaunch`). Tested on kernel 6.18.39+rpt-rpi-v8.
- **A USB Wi-Fi adapter that supports monitor mode**, in the USB-A port. The
  built-in Wi-Fi (BCM43439, brcmfmac) cannot sniff. Tested: **EDUP
  EP-AX1672** (MediaTek MT7921AU, `mt7921u` driver; 2.4 and 5 GHz). Without
  an adapter the app still runs, with Bluetooth detection only.
  If the adapter drops off the USB bus mid-run (seen on the Cardputer Zero:
  `USB disconnect` / `error -71` in `dmesg`, also with the app closed), the
  app logs `[wifi_mon] adapter lost`, pauses Wi-Fi detection (a SCAN then
  says `sweep skipped: no Wi-Fi adapter`) and rebuilds its monitor
  interface by itself once the adapter is back (`[wifi_mon] adapter back`).
  Frequent drops point at power or the connector: try a short cable or a
  powered hub.
- Optional: a GPS module on the Grove port (UART, `/dev/serial0`; M5's Unit
  GPS at 115200, 9600 or 38400 baud is found automatically) for wardriving.
- SSH access (for copying the package over), and `sudo`.

**On the build machine**

- Linux with **Docker**. The app is built in an arm64 Debian trixie
  container — the device's own OS — so it links against exactly the
  libraries the Cardputer Zero has.
- On an **x86-64** machine the arm64 container runs under qemu user-mode
  emulation, which needs qemu's *static* binfmt handlers registered (one
  time):
  - Debian/Ubuntu: `sudo apt install qemu-user-static binfmt-support`
  - Arch/CachyOS: `sudo pacman -S qemu-user-static qemu-user-static-binfmt`
    then `sudo systemctl restart systemd-binfmt`
  - Anything with Docker: `docker run --privileged --rm tonistiigi/binfmt --install arm64`

  Check: `ls /proc/sys/fs/binfmt_misc/ | grep aarch64` lists `qemu-aarch64`.
  The handler must be the **static** `qemu-aarch64-static`: a dynamically
  linked `qemu-aarch64` fails inside containers (they exit with code 255).
- On an arm64 machine (another Pi, an ARM Mac with Docker), no emulation is
  needed.

## Build

```sh
# 1. The source (this port is the cardputer-zero/ folder of the firmware repo)
git clone https://github.com/skizzophrenic/SquachWatch-CYD.git
cd SquachWatch-CYD/cardputer-zero

# 2. The build environment (once; rebuild it after editing docker/Dockerfile)
docker build --platform linux/arm64 -t squachwatch-cz-build docker/

# 3. Build the app: ./squachd, an aarch64 binary
docker run --rm --platform linux/arm64 -u "$(id -u):$(id -g)" \
    -v "$PWD/..":/src -w /src/cardputer-zero squachwatch-cz-build make -j4

# 4. Build the Debian package: dist/squachwatch_<version>_arm64.deb
docker run --rm --platform linux/arm64 -u "$(id -u):$(id -g)" \
    -v "$PWD/..":/src -w /src/cardputer-zero squachwatch-cz-build make deb
```

Under emulation the first build takes a while (tens of minutes on a desktop;
`main.cpp` alone is 12,000 lines); later builds recompile only what changed.
A change of firmware version (a new commit or tag) rebuilds everything, on
purpose — the version is compiled in and shared with your squad.

LoRa (the Cap LoRa-1262) is in the build only with `SQUACH_LORA=1` —
`make -j4 SQUACH_LORA=1`, then `make SQUACH_LORA=1 deb`. Switching the flag
rebuilds everything.

The package version is the latest release tag plus the commits since it, e.g.
`1.49.0+cm0.3.g7645468` (`.dirty` if built from uncommitted changes).

**A native desktop build** (an SDL window on your desktop, for working on the
UI without the device) needs `g++ make` and the SDL2, ALSA and mbedtls 3
headers — Debian/Ubuntu: `libsdl2-dev libasound2-dev libmbedtls-dev`;
Arch: `sdl2 alsa-lib mbedtls3`. Build it into its **own** output directory —
`build/` holds the arm64 objects, and mixing the two gives link errors:

```sh
make -j"$(nproc)" OBJ_DIR=build-host BIN=squachd-host
./squachd-host          # the SDL window; see "Using it" for the keys
```

## Test

In the build container (`docker run … squachwatch-cz-build <command>`, as
above):

| Command | What it checks |
|---|---|
| `make test` | 9 unit-test suites (Wi-Fi radiotap parsing, BLE HCI parsing, battery, settings store, SD card, overlay, dimmer, shake/tap, audio) plus the checks that this port's copies of firmware code still match the firmware (`sound-enum-check`, `shake-sync-check`) and that build-flag tricks stay safe (`sdlog-slot-check`, `sound-odr-check`). All must report `0 failures` / `ok`. |
| `./squachd --selftest` | The real firmware's detection path end to end in virtual time: synthetic Wi-Fi/BLE input → detections, trackers, deauth, squad advertising, the "coming soon" features. Ends with `N checks, 0 failures`. |
| `./squachd --shot out.png` | Boots the real UI headless and saves the 320×170 home screen as a PNG. |

## Install on the Cardputer Zero

Pick **one** of the two ways; the package is the normal one.

### A. The Debian package (recommended)

```sh
scp dist/squachwatch_*_arm64.deb user@cardputer:
ssh user@cardputer
sudo apt install ./squachwatch_*_arm64.deb
```

It installs:

| Path | What |
|---|---|
| `/usr/bin/squachd` | the app — `root:netdev`, mode `0750`, with capabilities `cap_net_admin,cap_net_raw+ep` (set by the package's postinst) |
| `/usr/share/APPLaunch/applications/squachwatch.desktop` | the launcher entry (`Exec=/usr/bin/squachd --fb`) |
| `/usr/share/APPLaunch/share/images/squachwatch.png` | its icon (256×256) |
| `/usr/share/doc/squachwatch/README.md` | this file |

It depends on `libsdl2-2.0-0 libasound2t64 libstdc++6 libgcc-s1 libc6`
(already on the stock OS) and `iw iproute2 libcap2-bin` — `apt` fetches any
that are missing.

**Why the capabilities, and why `netdev`:** sniffing needs a monitor-mode
interface and raw sockets, i.e. `CAP_NET_ADMIN` and `CAP_NET_RAW` — and
nothing more, so the app runs as **your user, not root**. Anyone who can run
a `CAP_NET_RAW` binary can sniff, so only members of the `netdev` group may
run it.

**Your user's groups** (the postinst prints a note for anything missing):

| Group | For |
|---|---|
| `netdev` | running the app at all |
| `video` | the screen (`/dev/fb0`) and the backlight |
| `input` | the keyboard (`/dev/input/event*`) |
| `dialout` | the GPS (`/dev/serial0`) |
| `audio` | the speaker |

The stock image's user is in all of them. Otherwise:
`sudo usermod -aG netdev,video,input,dialout,audio <user>`, then log out and
back in.

**The launcher:** SquachWatch appears in the app menu. If it does not show
up straight away, restart the launcher: `systemctl --user restart APPLaunch.service`.

**Update:** build a new package and `sudo apt install ./<new>.deb` over the old
one. **Remove:** `sudo apt remove squachwatch` (your settings and logs in your
home directory are kept).

### B. Manual install (development)

From the build machine, after `make`:

```sh
make -o squachd deploy DEVICE=user@cardputer
```

This copies the binary, launcher entry and icon over SSH and runs
`scripts/install.sh` with `sudo` on the device: the same result as the
package, but at `/usr/local/bin/squachd` (`-o squachd` stops `make` from
trying to rebuild the binary on the host). Re-run it after every build —
replacing the file drops its capabilities, and the script sets them again.

Don't mix A and B: the package's launcher entry runs `/usr/bin/squachd`, the
manual one `/usr/local/bin/squachd`, and both use the same entry file. The
package warns if it finds a manual copy; remove it with
`sudo rm /usr/local/bin/squachd`.

## The speaker fix (one-time OS change)

On the Cardputer Zero the headphone-jack detector (GPIO17, no pull resistor)
picks up the speaker's own signal during loud playback and reports
headphones plugged in and out several times a second — and M5's device tree
tells the audio driver to **mute the speaker** on every "plugged in". Short
sounds like SquachWatch's alert beeps are lost entirely. This affects every
app on the device, not only SquachWatch.

The fix is a small device-tree overlay that keeps the detector from muting
the speaker. With it, plugged-in wired headphones no longer mute the speaker
either (both play), so turn it off again when you want headphones. A
`headphone-detect on|off|status` script that does all of this, with notes,
is in the companion repo
[Bratag-labs/cardputer-zero](https://github.com/Bratag-labs/cardputer-zero)
(`HEADPHONE-DETECT.md`). By hand, on the device:

```sh
cat > /tmp/headphone-detect-off.dts <<'EOF'
/dts-v1/;
/plugin/;
/ {
    compatible = "brcm,bcm2835";
    fragment@0 {
        target-path = "/sound";
        __overlay__ { m5stack,hp-mute-pins = "Headphones"; };
    };
};
EOF
dtc -q -@ -I dts -O dtb -o /tmp/headphone-detect-off.dtbo /tmp/headphone-detect-off.dts
sudo install -m 0755 /tmp/headphone-detect-off.dtbo /boot/firmware/overlays/
sudo cp /boot/firmware/config.txt /boot/firmware/config.txt.bak
printf '[all]\ndtoverlay=headphone-detect-off\n' | sudo tee -a /boot/firmware/config.txt
sudo reboot
```

The overlay line must come after M5's `dtoverlay=cardputerzero-v5-overlay`
(appending at the end does that). To undo it: delete those two lines from
`/boot/firmware/config.txt` and reboot.

SquachWatch also keeps the audio codec powered while its **SOUND** setting is
on (ALERTS or ALL): from cold, the codec swallows the first part of a sound,
and the alerts are only ¼ second long. It does this by holding a capture
stream open — the microphone is read and the data thrown away at once, never
stored. With SOUND off it is released. Measured cost: about 5 mA.

## Using it

Start **SquachWatch** from the launcher. On the device:

| Key | Does |
|---|---|
| Arrows, `TAB` | move the cursor |
| `ENTER`, `SPACE` | press what the cursor is on |
| `ESC`, `DEL` | back one screen; on the home screen, open the menu |
| **hold `ESC` 1 s** | quit SquachWatch (a bar fills to show it) |
| letters (home screen) | hotkeys: **S** scan, **L** log, **D** desk, **B** background (other screens have their own) |
| knock on the case | pokes Squachy (home screen) |
| shake it | tosses Squachy; wakes a dimmed screen |

On the **text screens** — the squad name and messages (SquachMesh), a Wi-Fi
password, the PIN and the lock screen — the keyboard types: **ENTER** saves,
**Backspace/DEL** deletes, **ESC** leaves without saving, **TAB** shows or hides
a password, and the arrows still move. (`~` and `^` cannot be typed.)

**LoRa** (with the M5Stack Cap LoRa-1262 seated, in a build made with
`SQUACH_LORA=1`) works as on upstream's T-Deck, the SquachWatch board with a
keyboard and LoRa:

- A pink **LORA** pill at the top left of the home screen while the radio is
  on — **LORA n NEW** with chats unread — beside the counter tiles.
- **R** on the home screen opens the LORA page: the radar dish, signal tiles
  and strip, the chats, and a compose line. **TAB**/arrows move the cursor
  (BACK, the tabs, the chats, the network switch), **ENTER** presses; typing
  goes into the compose line and **ENTER** sends. The tabs and the switch
  pick the network the page shows and sends to, and retune the radio to
  listen to it for this run; what the radio hears at boot is the saved
  SYSTEM > LORA row.
- **SETTINGS > SYSTEM** has the LoRa rows: **LORA CHATS** (the chat list),
  **LORA** (what the radio listens to: Meshtastic, MeshCore, both in turn, or
  off — saved) and **LORA TX** (OFF / 10 / 20 / 22 dBm; off by default). The
  compose line says *LORA TX IS OFF: SETTINGS* until it is on.
- A new chat message plays the two-note MESSAGE chime, as on the T-Deck: only
  with **SOUND = ALL** (it is not an alert sound), not on the LORA page or
  in the chats, not at night or with the screen dimmed, at most every 30 s.
- Listen on **MESHTASTIC** (or MESHCORE) rather than both: "both in turn"
  hops between the networks and misses single messages easily. The same goes
  for the node you talk to — a T-LoRa Pager set to both missed the Zero's
  messages until it was set to Meshtastic.

After 60 seconds without a key the screen dims (any key, or a shake, brings
it back); alerts still sound while it is dimmed. SETTINGS > BRIGHTNESS sets
the screen's brightness while the app runs; on exit the launcher's own level
comes back.

**Where things are** (in the home directory of the user running it):

| Path | What |
|---|---|
| `~/SquachWatch/` | the "SD card": detection logs (`squachwatch-YYYYMMDD.log`), WiGLE wardrive CSVs, `gps-status.log` |
| `~/.local/state/squachwatch/nvs/` | saved settings |
| `~/.local/state/squachwatch/squachd.log` | the app's log when started from the launcher (`squachd.prev.log`: the run before) |

**From a terminal** (SSH or a desktop session):

```sh
squachd             # an SDL window (desktop session); stdin takes console commands
squachd --fb        # full screen on /dev/fb0 + the keyboard, as the launcher runs it
squachd --selftest  # the detection self-test (no radios, virtual time)
squachd --shot out.png
```

Console commands (one per line on stdin): `Q` quit, `N`/`P` next/previous,
`H` back, `L` press, `S [n]` step the loop n times, `T <type> [rssi]` inject a
synthetic detection, `Y <text>` type text as on the keyboard (`\n` = ENTER,
`\b` = DEL; off a text screen, letters are hotkeys).

Environment overrides (ignored when the binary runs with its capabilities,
so they only apply to development copies): `SQUACHWATCH_STATE` (settings
directory), `SQUACHWATCH_SD` (card directory), `SQUACH_WIFI_IF`,
`SQUACH_BLE_DEV`, `SQUACH_GPS_DEV`, `SQUACH_KBD_DEV`, `SQUACH_AUDIO_DEV`,
`SQUACH_AUDIO_CTL`, and `SQUACH_I2S_TRACE=1` (log every sound sent to the
speaker).

**Not available on the Cardputer Zero (yet)** — the firmware's features
that need hardware or ESP32 internals this board does not have: LoRa
without the Cap LoRa-1262 (or in a build without `SQUACH_LORA=1`), HUMAN SENSING (ESP32 Wi-Fi CSI), RING (Bluetooth
connections), the EXT SCREEN and firmware updates. The last two show
**FEATURE COMING SOON** when selected.

## After a firmware change

This folder always builds the firmware it sits in. After firmware changes
(a new release, or work on `master`), rebuild and run the checks:

```sh
docker run --rm --platform linux/arm64 -u "$(id -u):$(id -g)" \
    -v "$PWD/..":/src -w /src/cardputer-zero squachwatch-cz-build \
    sh -c 'make -j4 && make test && ./squachd --selftest'
```

Then review what changed under `CARDPUTER_ADV` and
`SQW_SMALL`: this build borrows those two board flags (the ESP32 Cardputer
ADV's keyboard and small-screen code), so new code for that board also runs
here. `make test` flags the cases that would break silently: a new sound
(`sound-enum-check`) or a changed shake detector (`shake-sync-check`).
A few firmware files are compiled from patched copies (`tools/fw_patch.py`,
rules in the `Makefile`: the T-Deck's LoRa settings rows and home-screen
pill, enabled for this board) so the firmware's own sources stay as they are;
when a change moves one of the patched lines the build stops with
`fw_patch: ... expected 1 line ...` — check the new code and update the line
in the `Makefile`.
Finally, test on the device.

## Layout

Paths are relative to this folder (`cardputer-zero/`).

| Path | What |
|---|---|
| `..` | the SquachWatch firmware (`src/`, `include/`, and `sim/`, whose shims this build reuses) |
| `src/cm0/` | the Cardputer Zero side: `main_linux.cpp` (harness: SDL / framebuffer, keys, loop), `wifi_mon.cpp` (monitor mode + channel hopping), `ble_scan.cpp` (raw HCI LE scan), `i2s_alsa.cpp` (sound), `shake_iio.cpp` (motion sensor), `gps_serial.cpp`, `sd_card.cpp`, `prefs_store.cpp`, `dimmer.cpp`, `battery.cpp`, the unit tests and `selftest.cpp` |
| `src/cm0/lora_lx.cpp`, `src/cm0/lora_home.cpp` | LoRa: the SX1262 driver on the Cap's SPI bus, and the LORA page (the T-Deck's page at 320×170), the home-screen pill |
| `tools/fw_patch.py` | copies a firmware source into `build/gen/` with whole-line changes (see "Updating"); the firmware's own files are never edited |
| `src/cm0/sys_shim/`, `src/cm0/radio_stub/` | headers that stand in for ESP32/Arduino APIs the firmware calls |
| `docker/Dockerfile` | the arm64 build environment |
| `scripts/build-deb.sh` | builds the Debian package (`make deb`) |
| `scripts/install.sh` | the manual installer (`make deploy`) |
| `assets/` | the launcher entry and icon |
| `docs/` | hardware facts, architecture and radio design |
