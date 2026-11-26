# LoRa on the Cardputer Zero — design spec (Phase 7)

> Status (2026-11-25): **receive and transmit working on the device**,
> with upstream's T-Deck behaviour (see "Current behaviour" below). G1–G3
> and G5 pass; G4 (MeshCore on air) and G6 (pocket-lorascan side by side)
> are still open. The current build is installed and running on the
> test device (2026-11-25).
> Phase A history is kept below as written. Session-state and gates are tracked in
> `SESSION_STATE.md`; the per-fix status of the backend is in
> `docs/LORA_FIXLIST.md` (its "Status" section is the up-to-date
> record).
>
> Implementation deltas vs this v1 spec: the backend file is
> `src/cm0/lora_lx.cpp` (not `lora_lx_sx1262.cpp`), and it implements the
> firmware's `LoraRadio::` shim contract (lora_radio.h) rather than the
> `lora_start/lora_scan_once/lora_poll` interface sketched below — the
> sniffer and LORA page then come in unchanged from the firmware, as
> intended.

## Current behaviour (2026-11-25)

Parity target: upstream's **T-Deck**, the only SquachWatch board with a
keyboard and LoRa. Where upstream gates a feature on `TDECK_PLUS`, the CM0
gets it through a patched copy of the firmware file (`tools/fw_patch.py`)
or a harness copy of the function — never an edit to the firmware's own files.

- **Listening** is chosen on SETTINGS > SYSTEM > **LORA** (saved) or the
  console's `LORA LISTEN`. The harness applies the saved choice every boot
  (upstream does so only on the T-Watch/T-Deck) and parks an unset device on
  Meshtastic (continuous RX).
- **The LORA page** (`src/cm0/lora_home.cpp`, upstream's `ui_lorahome.cpp`
  drawn at 320×170) is opened with **R** on the home screen. Its switch,
  as upstream's `uiLoraHomeToggleNet`, only picks the network the page shows
  and sends to — an earlier harness version retuned the radio from it, one
  way only, and left the Zero deaf to Meshtastic. No `r` key on the page:
  every printable key goes to the compose line.
- **Transmit** (`lora_lx.cpp` `transmit()`): real SX1262 TX — PA config, TX
  power, payload length in SetPacketParams, SetTx, wait for TxDone, then the
  RX packet params and IRQ mask back. Power is SETTINGS > SYSTEM > **LORA TX**
  (OFF / 10 / 20 / 22 dBm), as on the T-Deck; the compose line says *LORA TX
  IS OFF: SETTINGS* while it is off.
- **SYSTEM page rows** LORA CHATS / LORA / LORA TX: upstream's T-Deck rows,
  via the patched `ui_settings.cpp`.
- **Home-screen pill**: upstream's T-Deck `twatchLoraBadge` (harness copy in
  `lora_home.cpp`), up while the radio is on, "LORA n NEW" with chats unread.
  Placed top left beside the counter tiles (the user's choice; the T-Deck
  puts it top centre and drops the tiles under it); the tiles leave it room
  through the patched `ui_clear.cpp`'s `iconMargin`, as they do for the GPS
  label.
- **New-message chime**: upstream's `tdeckLoraSoundTick` rules (harness copy
  in `main_linux.cpp`): MESSAGE sound, SOUND = ALL only, not on the LORA page
  or chats, at most every 30 s.
- **The other node matters**: a node listening to both networks in turn (a
  CAD survey) misses one-shot messages; the user's T-LoRa Pager only received
  the Zero's messages once set to Meshtastic.

## Goal

Receive-then-send LoRa on the Cardputer Zero, tracked to the
project's `lora_*` shim path, so the app on the board can do everything
the T-Watch/T-Deck can do on LoRa (receive) plus TX (phase 2).

- **Phase A (this spec):** receive. Meshtastic + MeshCore listening, US
  915 MHz, US Eastern default. Same decode/display behavior as the
  T-Deck.
- **Phase B (separate approval):** TX, same defaults as the T-Deck
  (OFF until changed; 10/20/22 dBm; EU ceiling 14 dBm with duty-cycle
  note). Not part of Phase A.

"Parity" with the base SquachWatch repo means matching its LoRa
listening behavior on this board — receive-only correctness and a
faithful chat/log view. Upstream ESP32 firmware is **untouched**; the
CM0 gets its own Linux SX1262 backend through the existing shim.

## Hardware facts (verified on device 2026-11-24)

- Cap LoRa-1262 (U214) seated on the board's cap-bus. SX1262.
- Cap 5 V rail on (`/sys/class/leds/ext_5v_out/brightness` = 255).
- I2C expander (PI4IOE5V6408, addr 0x43) alive: `out=0x01 hiz=0xFE dr=0x01`.
  The pocket-lorascan app reads and writes this the same way.
- SPI EXT bus = `/dev/spidev0.1` (spi:spidev). GPIOs 22/23/26
  (busy/irq/reset) on gpiochip0. GPS on GPIO14/15 (confirmed working by
  the user; UART default 115200 8N1).
- GPS confirmed by user (2026-11-24). LoRa activity confirmed working by
  the user's `pocket-lorascan` screenshot: waterfall renders, scans
  show, no packets in an empty room.
- `pocket-lorascan` v1.1.1 (already installed) is the reference driver
  for this exact Cap: drives spi0.1 + gpiochip0 + ext_5v_out + PI4IOE
  0x43. The CM0 backend will replicate (not copy) its init.

## Antenna

- The included RP-SMA rubber duck (868 → 923 MHz design, 3 dBi) is
  within spec for the US 915 MHz band (FCC Part 15.247 compliant,
  868-923 tuned). 868 only applies in the EU band.
- Not a board change; no antenna work required for Phase A.
- Note: the "868 MHz antenna works in the US" claim is not in any doc
  we read — FCC/cert and chip range confirm 868-923 coverage, which
  includes 915.

## Implementation plan

### Phase A — receive only

1.  **Linux SX1262 backend** (`lora_lx_sx1262.cpp`, new, under `src/`):
    - Cap bring-up: enable `ext_5v_out` (if 0), drive the I2C expander
      (out|0x01, hiz&0xFE, dr|0x01).
    - gpiochip0: reset (26) high → low → high; busy (22) polled and
      gated; irq (23) as the interrupt flag.
    - SPI (`/dev/spidev0.1`, mode 0, 1 MHz, 8 bits):
      hard reset → SetStandby (RC) → TCXO 3.0 V (`SetDIO3AsTcxoCtrl`) →
      Calibrate(all) → ClearDeviceErrors → SetRegulatorMode(DC-DC) →
      SetDio2AsRfSwitchCtrl → SetStandby(XOSC) → SetPacketType(LoRa) →
      set US915 frequency (906.875 MHz, LongFast) → CalibrateImage →
      SetBufferBaseAddress → RX boosted gain → set sync word
      0x2B → SetDioIrqParams → verify via sync-word read == `0x24 0xB4`.
    - Decode path: IRQ preamble/header-valid/RX-done; CRC errors
      dropped; per-packet decode for Meshtastic (text + name + node id,
      0x2B sync) and MeshCore (text); region = US915 (US Eastern).
    - Region from timezone, same rule as the firmware (915 for US and
      rest-of-Americas; AU 915 for AU/NZ; 868 elsewhere).
2.  **Shim contract** (`lora_*`):
    - `lora_start()` / `lora_stop()` — cap power up/down, mute GPU if
      in use.
    - `lora_scan_once(channel, channel_ms)` — sample and decode packets
      for a short window.
    - `lora_poll()` — non-blocking up to N new messages per call.
    - Events into the detection engine in the same shape the ESP32
      feeds it, so detection.cpp and the UI stay unchanged.
3.  **Wiring into the app:** detection engine + UI unchanged; a new
    `lora_*` shim backend in the CM0 harness. The boot path hands
    control of the cap-bus to the LoRa backend instead of pocket-lorascan.
4.  **Verification:** boot on the device, confirm a waterfall-equivalent
    in the app and a decoded packet on a loopback/demo channel (and
    confirm no collision with pocket-lorascan's gpio/SPI usage). Gate:
    0 errors, expected waterfall on device.

### Phase B — TX (separate approval)

- `SetTx` path via the same SPI. TX OFF by default (same rule as the
  T-Deck), 10/20/22 dBm settings, EU ceiling 14 dBm with duty-cycle note.
- Gated behind a UI toggle; no TX without explicit gate.

### UI

- New LORA page (T-Deck style, adapted): listening state, BOTH /
  MESH / MESHCORE / OFF chip set, decoded chat view (same rows and
  coloring the ported app uses), a small radar/signal-strength
  column, and a "LORA" status pill on the home screen.
- Uses the same rendering pipeline (320×170 plain array + SDL2 blit)
  and keyboard navigation the ported app uses — no new framework.
- LORA chats are one row per message, with privacy-mode handling,
  same dedup rule (10 min), and the same one-buzz-per-N-seconds rule.

### Out of scope (Phase A)
- TX. DMs. Radar with GPS. The continuous "BOTH" alternation scheduler
  — Phase A keeps a simpler, single-preset RX loop (same as the
  watches). The BOTH alternation is added in a later phase.
- The 868-915 band mismatch for the included duck antenna. Not a
  problem for the chip range; no matching-network work for Phase A.

## Risks / gotchas

- **gpiochip and SPI**: both initialized by pocket-lorascan. SquachWatch
  must give the cap-bus to one app at a time; the shim must own the
  SPI/gpio cleanly (reset/close everything on stop).
- **Standby current**: ~5 mA (Cap), not significant for the plug-in
  device; duty-cycle only if the device is battery-only.
- **Busy line**: the kernel's GPIO API can leave `busy` pin at high on
  boot; handle a stuck-high busy by hard reset, not a hang.
- **GPS collision**: Cap GPS is on GPIO14/15; do not enable SPI on those.
- **Sender-side antenna choice** (Phase B): ducks may be tuned
  868-915; verify receive sensitivity at 915 before enabling TX.
- **Wrong band**: region from timezone; US Eastern → US915.
- **USB-UART GPS on 14/15**: don't send GPS debug on those pins.

## Verification gates (Phase A)

| # | Gate | Evidence |
|---|------|----------|
| G1 | Cap bring-up clean: 5 V + expander | `ext_5v_out` = 255; I2C read `out=0x01 hiz=0xFE dr=0x01` |
| G2 | SPI + reset + Calibrate all | `sync word read == 0x24 0xB4` |
| G3 | Decode Meshtastic LongFast on US915 | decoded packet on the air, sample `0x2B` sync |
| G4 | Decode MeshCore Public channel | decoded packet on the air, sample packet |
| G5 | UI fits the Cardputer Zero screen | `LORA` page renders at 320×170 (screenshot) |
| G6 | Behavior matches pocket-lorascan | packet count and format match the app on the same frame |

Phase B gates (TX) are listed in the Phase B section when it is
scheduled.

**Gate status (2026-11-24):** **G1 PASS**, **G2 PASS** (device evidence
in `SESSION_STATE.md`). **G3 PASS** — decoded on the air, session 5
(`LORA_FIXLIST.md` "Status": 3/3 LongFast frames on 906.875 MHz, the
user's T-LoRa Pager). **G4 pending** (MeshCore Public channel on the
air). **G5 PASS (device)** — the engine + LORA page are linked
(`SQUACH_LORA=1`, deb installed by user confirmation; device boot
shows the radio up, band plan US 915, crypto self-test PASS); the
page is reached with **R** on the home screen (harness hotkey), arrows
scroll, level swaps the Meshtastic/MeshCore tab, BACK goes home. (The
session-6 "R again swaps the radio" key is gone since 2026-11-25: the
listen target is the SYSTEM page's LORA row, as upstream — see "Current
behaviour".) A fresh device defaults to listening Meshtastic (continuous-RX
FOCUS): the survey's short CAD window at low duty misses one-shot
messages easily — see `LORA_FIXLIST.md` "Status", session 6. On-page
decode verified: the user's T-LoRa Pager message showed on the page
(decode line in the fixlist).
**G6 pending** (compare the app's decoded frames with pocket-lorascan's
on the same traffic).
CAD: resolved on this chip (session 4 round 1, after the T2.6 BUSY
gate — `LORA_FIXLIST.md` "CAD"); it gates only SURVEY/peek, not
G3/G4/G5.

## Approval

Phase A started on the user's go-ahead (chat, 2026-11-23/24); the backend
is built and being verified per the gates above. Phase B (TX) was
approved and built in session 8 (2026-11-25): send works from the LORA
page's compose line, gated by SETTINGS > SYSTEM > LORA TX (off by
default), and was seen on the user's T-LoRa Pager.
