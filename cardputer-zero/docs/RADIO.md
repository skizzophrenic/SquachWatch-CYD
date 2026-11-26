# Radio — decision and design

## 0. The decisive unknown

M5Stack does not document the Cardputer Zero's Wi-Fi/BT chipset. 2.4-only
b/g/n plus the Pi Zero 2 W lineage in the coverage suggests a **CYW43439
(BCM43439)** — whose mainline Linux driver (cyw43/brcmfmac) has **no monitor
mode**. If that is the chip, the onboard radio cannot passively sniff, and the
project's premise ("no external hardware") is in question.

Phase 0 (`scripts/hw_probe.sh`) resolves the chip and tests monitor mode,
then the path is chosen:

```
G0: chipset + driver identified, monitor vif test done
├─ driver supports monitor mode (e.g. RTL818x / Atheros)
│    └─ PATH A: onboard radio sniffs. Default. Zero extra hardware.
└─ no monitor support (likely CYW43439)
     ├─ PATH B: USB adapter on the board's own USB-A port
     │    (rtl8187u, rtl8188eus, AR9271 have monitor support)
     │    Small extra hardware — needs the user's sign-off, since the
     │    stated premise was "no external HW".
     └─ PATH C: ESP32 co-processor radio
          A $15 ESP32 board runs the existing firmware's sniffer and
          forwards frames over USB serial — the firmware already has this
          exact shape (the Tab5 forwards its C6 radio through the same
          frame-ingress path the CM0 build will use). Safe, but a
          two-board device.
```

The detector is blind to the path: the shim contract (ARCHITECTURE §4) is
identical in all three. Only `wifi_mon.cpp` / `ble_hci.cpp` internals differ.

### G0 outcome (2026-11-16, probe v1)

Chipset confirmed: **Wi-Fi = BCM43439 over SDIO, brcmfmac** (2.4 GHz only);
**BT = CYW4343A2**. brcmfmac has no monitor mode, so **PATH A is out** (one
confirmation command still pending — `iw dev wlan0` + a monitor vif create).

**Decision (2026-11-16, user): PATH B** — a USB adapter on the board's
USB-A port. The onboard bcm43439 stays managed (NTP/OTA); the adapter
provides the monitor feed. Preferred: AR9271 class (ath9k_htc); acceptable:
RTL8188EUS. Formal close of G0 = `iw dev <adapter> interface add mon type
monitor` succeeding with the physical adapter in the board's USB-A port
(Phase 2 step 1).

**PATH B adapter matrix** (all in-kernel on the device's Debian 13 / kernel
6.18 aarch64 — no out-of-tree drivers):

| Adapter | Driver | Monitor quality | Cost |
|---|---|---|---|
| AR9271 USB (e.g. Alfa AWUS036NHA or generic "USB-N") | ath9k_htc | Excellent — the classic choice | ~$15–25 |
| RTL8188EUS | rtl8xxxu | Good, some quirks | ~$8 |
| RTL8187 | rtl8187 | Solid, old but stable | ~$8 |

Form factor: the board has a USB-A port on the cap bus; a short dongle or a
USB-A extension is the only hardware change. Power: ~100–150 mA, fine on the
GL852G hub.

**PATH C** costs a $10 ESP32 + 2–3 days of forwarder firmware and is a
two-board device; it is the fallback if the user won't touch a USB dongle or
the dongle misbehaves.

**Note:** whichever Wi-Fi path is chosen, BLE stays onboard (the CYW4343A2
works via raw HCI regardless).

## 1. Wi-Fi monitor design (PATH A / B)

- Interface: keep `wlan0` managed (it is used for NTP/OTA joins); add a
  separate monitor vif:
  `iw dev wlan0 interface add mon0 type monitor; ip link set mon0 up`.
- Frames: `AF_PACKET` + `SOCK_RAW` + `ETH_P_802_11` on mon0 → raw 802.11
  (beacons, probe responses, data, deauth/disassoc) → the promiscuous-rx
  callback. `PACKET_FANOUT` if a second reader is ever needed.
- Channel hopping: `iw dev mon0 set freq <hz>` (2412 + 5·(ch−1)); the
  firmware's existing hop clock drives the schedule (it already runs the hop
  for the Tab5's radio).
- Emissions: monitor mode with no transmit is silent — same passive posture
  as the ESP32 promiscuous mode. Managed-mode scans (for network lists) do
  emit scan requests, as they do on every board.
- 5 GHz: not applicable — the board's radio is 2.4-only.
- Deauth-flood and EVILTWIN logic run unchanged on the same feed.

## 2. BLE design

- Raw HCI: `socket(AF_BLUETOOTH, SOCK_RAW, BTPROTO_HCI)`, bound to hci0.
  This is the same layer `btmon` reads; no driver support beyond basic HCI is
  needed.
- Passive listen: HCI **LE Enable Scanner** (enable scan, sniff=1) — the
  controller delivers adv reports with no scan requests. If the controller
  refuses, fall back to a standard **passive** LE scan (LE Set Scan
  Parameters: passive, interval 0x10, window 0x10; LE Set Scan Enable).
  The firmware already prefers passive (`scanPassiveNow` in
  `detection.cpp`).
- Mapping: LE Advertising Report / Scan Notification (address, RSSI, raw
  adv + scan payload) → the NimBLE scan callback shape. The AirTag Find My
  payload check needs the raw payload — present in the report.
- Squad hellos: the advertiser side — LE Set Advertising Parameters/Data +
  Set Enable with the squad payload (≤31 bytes; verify the squad hello fits,
  the firmware's emulator uses it under `-DSQUACH_MESH=1`).
- Posture: passive = no scan requests. The squad hello is a small,
  settings-gated broadcast, as on the firmware boards.

## 3. PATH C detail (if needed)

A cheap ESP32 (a bare module or a $15 display board) runs a thin firmware:
`esp_wifi_set_promiscuous` + a NimBLE passive scan, forwarding raw frames and
adv reports over USB CDC serial using the same framing the Tab5 path uses.
`wifi_mon.cpp`/`ble_hci.cpp` become serial readers; the shim contract and the
entire detection/UI stack are untouched. 2–3 days of work plus the tiny
forwarder firmware.

## 4. What detection.cpp needs from each path

Identical by design — the shim contract in ARCHITECTURE §4. The path choice
is invisible to every line of the detector, the UI, and the test suite.
