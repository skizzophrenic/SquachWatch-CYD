# Fix reference — `src/cm0/lora_lx.cpp` (Linux SX1262 backend)

Consolidated, verified issue list for the uncommitted Phase A work.
Three independent sources were cross-checked on 2026-11-23:

1. **The local RadioLib** — `~/claude/SquachWatch-CYD/.pio/libdeps/tdeck-plus/RadioLib`
   (the library the firmware uses). It is **stock upstream master**, not a
   fork: its `Module.cpp`/`Module.h`/`SX126x_*` are byte-identical in
   architecture to `jgromes/RadioLib` master fetched from GitHub the same day.
2. **Upstream RadioLib master** (GitHub, fetched 2026-11-23) — same code.
3. **Semtech DS.SX1261-2 Rev 2.2** (Dec 2024) command tables (via the
   Leviculum driver-developer extract).

Line numbers refer to `src/cm0/lora_lx.cpp` as of HEAD `d7f0b9d` + the
uncommitted file (475 lines).

**Verdict: the backend cannot receive a packet as written. Eight independent fatal
defects sit on the chip→sniffer path.** The scaffolding (SPI transport, GPIO
v2 uapi layout, cap power, command framing, most opcodes, all symbol
encodings, `readReg`, the device-error read in `begin()`, sync-word register
format, SetBufferBaseAddress, boosted gain, TCXO command) is verified
correct and should be kept.

## Status (2026-11-24)

### Session 6 — engine + LORA page integrated (G5)

The backend fixes above are all applied; this session linked the
firmware's LoRa engine and its UI into the app, replacing the
ESP32 radio backend with this one:

- **Makefile**: `SQUACH_LORA=1` now global (main.cpp's LORA states and
  `Lora::begin()/tick()` only exist under it); all `lora_*.cpp` +
  `ui_lora*.cpp` link in, `lora_radio.cpp` (the ESP32 backend for the
  same `LoraRadio::` symbols) excluded; `lora_myname.cpp` stays in its
  original list (filter dedups).
- **Shims**: `freertos_shim.h` gained `vTaskDelay`/`pdMS_TO_TICKS`/
  `vTaskDelete`/`pdPASS` and the plain (timed) mutex API; three
  `freertos/{FreeRTOS,task,semphr}.h` forwarders for the includes the
  engine spells by name; the sim's `EspClass`/`ESP` are renamed past and
  a CM0 one takes the names (`getEfuseMac()` reads the wifi MAC for the
  Meshtastic node number; `restart()` logs — its caller, the LORA
  console's REGION save, is unreachable on this board); `WiFi.status()`
  now answers the truth (wlan0's address, for the enrichment's
  `wifiUp()` gate); new `WiFiClient.h` (a real TCP socket) and
  `HTTPClient.h` (the plain-HTTP GET the enrichment transport makes).
- **Entry + keys** (`src/cm0/main_linux.cpp`): the firmware's LORA page
  is wired for the watch/T-Deck only, so the harness owns the entry:
  **R** on the home screen sets `LORA_CHAT` as `enterLoraChat()` does;
  while the page is up the arrows scroll the ring, the level key swaps
  the Meshtastic/MeshCore tab, and BACK is the firmware's (the page has
  no back point, so its default goes home).
- **Verified so far**: `make SQUACH_LORA=1` builds clean; `--selftest`
  45/45; `make test` green; headless `--shot` boots with the engine
  (keys made and saved, `[meshtalk] crypto self-test PASS`, radio
  absent reported gracefully); deb built. No-LORA baseline still green
  after the shim changes (`make -j4` clean, `--selftest` 45/45).
- **On device (2026-11-24, user-confirmed install)**: the deb is in
  (1.50.0+cm0.41.gd7f0b9d.dirty); a headless boot on the CM0 with the
  real radio logged `[lora] up: SX1261 V2D 2D02, TCXO on, ring of 256
  in PSRAM`. Its "band plan EU 868" line is a `--shot` artifact (shot
  mode runs Preferences in memory; the saved `tz 0` = US EASTERN gives
  US 915 on a real launch).
- **Listen default fix (same day, on-device evidence):** the first
  on-device session of the page showed nothing on air. The radio had
  booted to the firmware's factory default LISTEN = BOTH (3) — a CAD
  survey over {MT, MC} — while the user's real pager transmission
  (want-ack retransmits, MT-LongFast 906.875, the same profile the
  session-5 probe decoded parked there) went by undetected. CAD itself
  is resolved on this chip (the "CAD" note above, session 4 round 1),
  but the survey's CAD window is tens of ms at ~30% duty per profile:
  a one-shot message (a ~65 ms preamble, a few retransmits over a
  short window) is easy to miss by duty-cycle alone. FOCUS (continuous
  RX) is the path with 100% catch on a known network and the one G3
  was proven on, so the harness now (a) defaults a fresh device to
  LISTEN 1 (Meshtastic FOCUS) when the user has saved no `loraLstn`
  choice (main_linux.cpp, first frame), and (b) gives R on the page a
  second job: swap the radio's listen target Meshtastic <-> MeshCore
  (persisted, applied at once; the page's listen line reflects it).
  The survey (LISTEN 3) stays available via console `LORA LISTEN 3`
  for when ambient traffic, not one-shot messages, is the goal.
- **Regression (same day, user report: "neither MeshCore nor Meshtastic
  shows") -- TWO causes, found in sequence.**
  (a) **The page's R toggle persisted.** The G4 instructions told the
  user to press R to point the radio at MeshCore; that press stored
  `loraLstn=2` in nvs. The user's pager sends Meshtastic -> nothing on
  that tab; the pager doesn't send MeshCore at all -> nothing there
  either.
  NOT the CRC change (it only runs inside readPacket, i.e. after a
  decode, and only sets log flags). Fix: R is now a session-only
  toggle (no `setLoraListen`; the persistent choice stays the console's
  `LORA LISTEN`), main_linux.cpp; device nvs reset to `u loraLstn 1`
  (backup in /tmp/settings.nvs.bak).
  (b) **The saved choice is never applied at boot on the CM0.** After
  the reset, the next run was STILL silent -- and its log had the radio
  up (`[lora] up: SX1261 ...`) but NO `[lora] listening: ...` line at
  all: the radio ran the factory default (the CAD survey) the whole
  time, missing the one-shot message. Why: the firmware's boot-time
  `applyListen(Settings::loraListen())` is gated
  `#if defined(TWATCH_S3) || defined(TDECK_PLUS)` (lora_sniffer.cpp
  setup) -- the CM0 is neither. The old CM0 boot block only acted when
  NO value was saved, so it masked the gap on the very first launch
  (that one worked) and skipped afterwards. Fix: the CM0 boot block
  (main_linux.cpp step()) now applies the saved choice EVERY boot;
  factory default 3 (survey) is treated as unset -> park Meshtastic
  and save it. `LORA LISTEN 3` gives a session-only survey (next boot
  re-parks Meshtastic). Rebuilt + installed. **DEVICE-VERIFIED
  (same day, user: "meshtastic messages now coming through as
  expected"):** boot log `[lora] listening: Meshtastic` (the fix
  firing), decode line
  `[lora] 906.875 SF11/250k sync 2b  -27 dBm  snr 5.75  ferr +0  crc ok
  436 ms  !5a5a7850>all hop0/3 ch08 TEXT foo` -- which also closes the
  CRC-flag parity item: "crc ok" instead of "no crc".
  G4 also needs the pager side to actually transmit MeshCore (MeshCore
  TX is a separate setting on the pager -- the user's pager was
  Meshtastic-only).
- **G5 PASS on device (same day):** after the listen-default fix, the
  user relaunched, opened the page (R) and sent from the T-LoRa Pager
  — the message shows on the page. Device log decode line:
  `[lora] 906.875 SF11/250k sync 2b  -40 dBm  snr 6.25  ferr +0  no crc
  436 ms  !5a5a7850>all hop0/3 ch08 TEXT testing`. Boot log shows the
  default fired: `[cm0] lora: no saved listen choice; listening
  Meshtastic ...` + `[lora] listening: Meshtastic`.
- **CRC flag parity (same day):** the decode line's "no crc" meant the
  flags were never derived, not a CRC failure. The ESP32 backend reads
  the received header's CRC flag from the chip (RadioLib fork
  `getLoRaRxHeaderInfo`, jgromes/RadioLib 7.7.1 SX126x.cpp:732: bit 7
  of the low byte of the 16-bit register at 0x076B) and marks a clean
  frame PK_CRC_PRESENT -> PK_CRC_OK; we set neither, so good frames
  logged "no crc" and lora_opalert's `crcOk` gate (PK_CRC_PRESENT &&
  PK_CRC_OK) could never pass. readPacket now reads 0x076B/0x076C
  (explicit mode only) and applies the ESP32 rule (lora_radio.cpp:412).
  **Device-verified:** the 2026-11-24 decode after the fix reads "crc
  ok" (see (b) above).
- **On the device next**: G4 (MeshCore on air in the MeshCore tab —
  press R on the page to point the radio at MeshCore, then send on that
  network), G6 (frame parity with pocket-lorascan on the same traffic).

### Session 5 — first on-air decode + log fix (G3)

**G3 (Meshtastic LongFast on-air decode) has real evidence now.** On-device
probe run (`/tmp/lx_bringup` on the CM0 console): 3/3 frames decoded on
906.875 MHz LongFast, sync 0x2B, CRC valid, no drops:

```
FRAME #1 len=83 rssi=-41 snr=5.0 flags=0x00 toa=886784us
  FF FF FF FF 50 78 5A 5A A5 46 9A C3 63 08 00 50 F4 C4 D4 66 9E A0 31 EA 7D E1 E6 A2 95 A3 4B CF …
FRAME #2 len=24 … rssi=-40 snr=7.0 toa=436224us
FRAME #3 len=27 … rssi=-39 snr=5.0 toa=436224us
```

Sender identified from the payload (firmware's own `looksMeshtastic`
shape: 16-byte header, id = bytes 8–11): `to` = 0xFFFFFFFF (broadcast),
`from` = 0x5A5A7850 — the 0x5A5A prefix is SquachWatch's node-ID scheme
(`0x5A5A0000 | MAC low 16`), i.e. **the user's T-LoRa Pager running the
SquashWatch firmware**. Frame #1 (83 B) is its NodeInfo announcement
(sent before the first text after boot); #2/#3 are the user's test
messages, retransmitted (want-ack, no ACK possible — CM0 is RX-only).
So the decode is not just any Meshtastic traffic: it is the other
SquachWatch's Meshtastic TX path, end to end. (The "SquashMesh
end-to-end" open item — SquashWatch's *own* protocol — is still open;
this is the Meshtastic layer.)

**New bug found + fixed (log-only): `cmd()` status check off by one bit.**
The status byte's command-status field is bits [3:1]; the check read
`rx[1] & 0x07` (bits [2:0]). Value 0xD4 = mode RX + status 0x02
("data available" — the normal between-packets state in continuous RX)
was misread as 0x04 "processing error" and logged to stderr after every
`ClearIrqStatus`. Effect: stderr flooded ~1 line per packet (the bg_10
run's wall of `status 0xD4` lines; the flood broke its SSH connection —
exit 255). Fix: `((rx[1] >> 1) & 0x07) >= 0x03` (:253) — real errors
(0x03 timeout / 0x04 processing / 0x05 execution) still log; normal
RX states stay quiet. Radio path untouched.

Also noted: the host's docker daemon is flaky today (intermittent
"No such container" on `docker exec` while `docker ps` shows the
container up; detached execs silently swallowed). The probe now builds
**natively on the device** — it has g++ 14.2.0 (Debian 13, same as the
container): `scp` the sources + `firmware/include` +
`firmware/src/lora_pkt.cpp` to `/tmp/lx_build/`, then
`g++ -std=c++17 -O2 -static -o /tmp/lx_bringup lx_bringup.cpp
lora_lx.cpp lora_pkt.cpp -I. -Iinclude` (~18 s). The container path is
not needed for the probe anymore.

**Quiet-verify run (same day) + one triaged transient.** The redeployed
probe re-run on the console (`./lx_bringup 120`): **zero `status 0xD4`
lines**, one frame decoded at t=9 s (len 27, rssi −36, snr +7 — the
pager's retransmit of the test message; it keeps retrying until it
receives an ACK). The run printed exactly one error line:
`[lora] cmd 0x80 status 0xAA` — the first `SetStandby(RC)` after reset.
**Triaged: expected transient, not a defect.** 0xAA → command status
bits [3:1] = 0x05 ("execution failure"): the chip is not ready for the
first SetStandby issued ~15 ms after the reset pulse; the second
`SetStandby(XOSC)` (after the TCXO/calibrate block) returns a clean
status. The reference's `init()` sends the identical sequence (0x80
{0x00} right after `hard_reset()`, same comment) and never reads the
status byte — it works on this exact Cap, and so do we (G2/G3 pass).
The line is kept: honest, once per bring-up, and real errors (0x03–0x05
on any other command) must stay visible. Do not "fix" it.

## Status (2026-11-24, end of session 4)

Resumed after the user-requested pause; the whole REMAINING list from
session 3 is now **APPLIED** (code compiles under the app's flags; the
on-device decode test is the next gate). Line numbers below are against the
current `lora_lx.cpp` (uncommitted).

### New findings, session 4 (both in the Makefile, not the backend)

1. **`CXXFLAGS :=` never saw the LoRa define.** The `SQUACH_LORA=1` block
   does `EXTRA_CXXFLAGS += -DSQUACH_LORA=1`, but `CXXFLAGS` was
   `:=`-expanded at line 109 — *before* that `+=` — so the define never
   reached any compiler. "Compiles + links" from session 3 was true of a
   build without the define. `CXXFLAGS` is now recursive (`=`); every use
   is recipe-scoped, so this is safe.
2. **The define must stay off the shared sources.** Upstream guards its
   *entire* LoRa engine/UI with `#if SQUACH_LORA` (main.cpp, ui_settings.cpp,
   …). Once the define actually reaches the compiler, the link pulls in
   `Lora::begin/tick/takeOperatorHit/applyListen` (lora_sniffer.cpp),
   `uiLoraChat*` (ui_lorachat.cpp) and friends — none of which this build
   compiles (by design: backend only, integration is G5–G6). The Makefile
   now applies `-DSQUACH_LORA=1` to `build/cm0/lora_lx.o` only
   (target-specific `CXXFLAGS +=`). **G5/G6 integration file list (for
   then):** lora_sniffer.cpp (2604 ln, the Lora:: engine), lora_enrich.cpp,
   lora_feed.cpp, ui_lorachat.cpp, ui_lorahome.cpp — plus the global define;
   lora_radio.cpp stays out (ESP32 backend, replaced by lora_lx.cpp).

### Probe upgrade (session 4)

`~/claude/lx_bringup.cpp`: two listen profiles per run — MT-LongFast
(906.875 MHz, 250 kHz) and MeshCore-US (915.0 MHz, 62.5 kHz — a real
preamble was already seen near 915.0 in session 3) — 60 s windows (argv[1]
extends), heartbeat every 5 s of IRQ activity, and every decoded frame in
full: len/RSSI/SNR/flags/ToA + hex bytes. Built aarch64 → `/tmp/lx_bringup`.

## Status (2026-11-24, end of session 3)

Work paused on the user's request; docs updated for handoff. Line numbers
below are against the current `lora_lx.cpp` (**524 lines**, uncommitted).

**APPLIED** (device-verified where noted):
- T1.1 reset release — `begin()` :309-312, `resetBus()` :513. **Device-verified** (G1/G2).
- T1.2 setSyncWord before readback — `begin()` :345-348. **Device-verified** (sync readback 0x24B4, G2).
- T1.3 version 20-byte read — `begin()` :316-328. **Device-verified** (`"SX1261 V2D 2D02"`).
- T1.4 SetRx 0x82 — :397, :406. **Device-verified** (RX accepted; a real preamble at 915.0 MHz latched IRQ 0x0004 within ~1 s of a 915.0 RX session, 2026-11-24).
- T1.5 SetCadParams 0x88 + SetCad 0xC5 + CAD DIO mask — :410-423. Commands accepted by the chip; **CadDone never latched** (open — see "CAD" below).
- T1.6 GetIrqStatus 0x12, `(rx[2]<<8)|rx[3]` — :429-435. **Device-verified** (preamble flag read via this path).
- T1.7 DIO1Mask = IrqMask — `setDioMasks()` :283-289. **Device-verified** (DIO1 fired on the real preamble).
- H.4 I2C one-transaction 2-byte writes — `capPowerOn()` :49-98. **Device-verified** (G1).
- H.1 `Spi::xfer` buffers sized from `n` (vectors, no 256 cap) — :175-193.
- T1.9 `readPacket` rewrite — :451-485: len/start from the 0x13 reply (rx[2]/rx[3]); ReadBuffer `0x1E` transfers `2+len` bytes, data at rx[3]; GetPacketStatus 0x14 → `rssi = -x/2` dBm, `snr4` signed; consumed IRQ word re-read and cleared at the end (the sniffer leaves RX_DONE clearing to the backend). **Matches pocket-lorascan's `read_packet()` field-for-field** (see reference notes below). Not yet seen decode a real frame on device (needs T2.1 + on-air traffic; next test).
- T2.2 auto LDRO — `apply()` :374-375 (`Lora::autoLdro || PF_LDRO`), state `s.ldro` feeds `timeOnAirUs`.
- T2.3 IQ errata 0x0736 bit 2 **plus** 0x0889 bit 2 (500 kHz modulation-quality) — `apply()` :380-385, read-modify-write on every apply, exactly as pocket-lorascan's `set_lora()`.
- New (reference parity): `clearIrq(0xFFFF)` before every `SetRx` — :395, :403 (pocket-lorascan `rx_continuous()`).

**REMAINING** (ordered; §6 order with today's reorder) — **items 1–7
APPLIED in session 4** (2026-11-24 resume): T2.1 passes `p.bwKhz10` into the
table (`setModulation()` param renamed to match); T2.5 sets `PK_CRC_ERR`
(RadioLib readData rule from the IRQ word) + `PK_IMPLICIT`, ferrHz/cr
deferred as noted; T2.6 `waitBusy(100)` at the top of `cmd()` (gates all
commands, reference-wise) plus a log-only status-byte check (0x03–0x05) in
`cmd()`; T1.10 `payloadLen = implicit ? p.implicitLen : 0`; H.2 `resetBus()`
at the top of `begin()` and in the alloc catch, header comment corrected;
T2.4 the dead band-tracking block was **deleted** (single 902–928
calibration in `begin()` = reference parity; band-following codes deferred
with EU868); Tier 4: includes, `std::memset` ×2, `TCXO_DECIVOLTS` table,
startup frequency 906.875 MHz, header GPS note, Makefile `lora-flag` stamp
(same trick as `VERSION_STAMP`). Only item 8 (H.3 event poll) remains
deferred. Verification: syntax check OK in-container; full
`make SQUACH_LORA=1 squachd` link after the Makefile findings above is the
next build gate; then the on-device decode test.
1. **T2.1 BW units — the one that blocks G4.** `apply()` :375 passes `p.bwKhz10 / 10` (kHz) into a table keyed in `bwKhz10` units ({625,1250,2500,5000}, `setModulation()` :264-271): 62.5 kHz (MeshCore US) and 125 kHz and 500 kHz all miss → fall back to 0x05 (250 kHz). 250 kHz is right only by the fallback coincidence. Fix: pass `p.bwKhz10` directly.
2. **T2.5 rest: `pk.flags`.** `readPacket` sets `flags = 0` always (:475). The sniffer's CRC accounting (`push()`, lora_sniffer.cpp:200: `if (pk.flags & PK_CRC_ERR) s_stats.crcErrors++`) and its "never decode a bad frame" behavior depend on it. Set from the IRQ word read at the end of `readPacket` (before clearing it): `PK_CRC_ERR` when `CrcErr` or (`HeaderErr` && !`HeaderValid`); `PK_IMPLICIT` when `s.implicit`. `ferrHz` (regs 0x0715/0x0716) and `cr` (explicit-header parse) are display-only here — pocket-lorascan doesn't fill them either; defer.
3. **T2.6 BUSY gate** — `cmd()` :235 transmits without waiting for BUSY low; the reference gates every command (`wait_busy()` at the top of `cmd()`). Short-timeout `waitBusy` at the top of `cmd()` (skip it for 0x12/0x13/0x14 reads? No — the reference gates all; keep it simple, gate all).
4. **T1.10 payloadLen** — :274 hardcodes 0xFF; should be `p.implicitLen` under `PF_IMPLICIT` (explicit mode ignores it; all Phase A profiles are explicit; pocket-lorascan also sends 0xFF — low risk).
5. **H.2 begin() cleanup** — `begin()` has no `resetBus()` at the top and the allocation `catch` (:303-306) returns -2 without releasing claimed lines → a retry after a partial failure hits EBUSY. Fix: `resetBus()` at top of `begin()` and in the catch; fix the stale lora_lx.h:21 comment ("called from begin()/standby()" — standby() does not call it).
6. **T2.4 CalibrateImage in apply() — downgraded.** The reference calibrates **once in init for 902-928** (`0x98 {0xE1,0xE9}`, 2 bytes) and never recalibrates while hopping the whole US band; it decodes fine. `begin()` already does that single calibration, so Phase A (US band only) needs nothing in `apply()`. The band-tracking block in `apply()` (:368-371) updates `s.calHz` without sending 0x98 — dead weight for the US band; either implement band-following codes (table in §facts) or delete the block. Decide when EU868 profiles enter scope.
7. **Tier 4 hygiene** — missing includes `<algorithm>` (std::min :405,:416) and `<cstdlib>` (std::atoi :55); `memset` unqualified (:295,:453); `tcxoDeci = code×5` (:355) wrong for every code except 0x06 (use the §facts voltage table); Makefile `SQUACH_LORA` flag not stamped (mixed-object risk on toggle); lora_lx.h:10 GPS parenthetical (line 14/15, not 0/1); startup frequency 915.0 vs spec 906.875 (cosmetic).
8. **H.3 waitIrq event poll** — battery concern only (cardputer-zero/BATTERY-POWER.md); the 1 ms DIO1 level poll (:441-449) is functionally correct. Defer.

**CAD — RESOLVED (2026-11-24, session 4 round 1).** After the T2.6 BUSY
gate went in, the probe's `startCad(4, 0, 800)` latched **CadDone
(0x0080)** on DIO1 and via `GetIrqStatus` — session 3 saw 0x0000 after
1.5 s with the same sequence. The raw-SPI probe (backend released)
reproduces it independently: `SetCadParams`/`SetCad` accepted (status 0xA2, no command
errors) → IRQ word 0x000080 after 1.5 s. Root cause almost certainly the
missing BUSY gate: `SetCadParams`/`SetCad` issued while BUSY was still
high from the preceding command were dropped by the chip, so the CAD
never actually ran (the status-byte mode-nibble theory from session 3 is
dead — the commands were fine, they were never received). CAD now works
on this chip; SURVEY/peek modes are unblocked.

## The reference source is now ground truth

**pocket-lorascan is open source** (v1.1.1, cloned from
`https://github.com/duanesweazy1/pocket-lorascan`, commit `c8e6168`; also
installed on the device: `dpkg -s pocket-lorascan`, binary
`/usr/share/APPLaunch/bin/pocket-lorascan`). The whole driver is one file:
**`lorascan.py`** (2400 lines; `class SX1262` :745-860, `cap_power_on()`
:700, GpioLines :632-676, RX loops :1290-1378). It is the app the user
confirmed decodes on this exact Cap. Cross-checked line-by-line against
`lora_lx.cpp` on 2026-11-24:

- **GPIO v2 layout: byte-identical.** 592-byte request: offsets@0,
  consumer@256, `config.flags`@288 (INPUT=0x4/OUTPUT=0x8), `num_attrs`@296,
  `attrs[0]`@320 (id=2 output-values, values@328, mask@336), `num_lines`@560,
  fd@588. Same ioctl constants (`0xc250b407`/`0xc010b40f`/`0xc010b40e`).
  This **resolves §5 open question 2**: whatever the device kernel's exact
  uapi is, the reference proves this layout + these constants work there.
- **Init sequence: identical** (Standby RC → TCXO 0x06 → Calibrate 0x7F →
  ClearDeviceErrors → RegulatorMode DC-DC → Dio2AsRfSwitch → Standby XOSC →
  PacketType LoRa → freq → `CalibrateImage {0xE1,0xE9}` (2 bytes) →
  BufferBaseAddress → boosted gain 0x96 → **set sync word 0x2B before the
  readback** (0x24B4 check) → DIO params → device-error check).
- **Reference's DIO1Mask is 0** (`SetDioIrqParams(IRQ_MASK, 0,0,0,0)`): it
  never uses the DIO1 pin — it polls the IRQ **word over SPI** every 5-10 ms
  (`_listen` :1298-1327, `_listen_lwan` :1329-1366: `flags = radio.irq()`;
  `if flags: radio.clear_irq(flags)`; on RX_DONE → `read_packet()`;
  `crc_bad = flags & (CRC_ERR | HEADER_ERR)`). Our backend instead drives
  DIO1 (DIO1Mask = IrqMask, `waitIrq` = level poll) because the firmware
  shim's contract is DIO1-based. Both are valid; the sniffer's `waitIrq()`
  semantics require ours.
- **`set_lora` per profile**: `ldro = 1 if (2^sf / bw_khz) >= 16.0 else 0`;
  BW_CODE {62.5:0x03, 125:0x04, 250:0x05, 500:0x06}; then the two errata
  register writes (0x0889 bit 2, 0x0736 bit 2) — now in our `apply()`.
- **No CAD, no CalibrateImage per frequency, no ferr/cr reads** (see above).
- **BUSY gate**: the reference's `cmd()` calls `wait_busy()` first
  (:759-762) — keep T2.6.

§5 open questions: **1** (0x13 framing) — still needs one on-device decode
to close end-to-end (the rewrite matches the reference exactly, which is the
strong prior); **2** — resolved (reference byte-identical, works on device);
**3** (version string) — closed: `"SX1261 V2D 2D02"` read via the 20-byte
transfer (G2, 2026-11-24).

## Verified SPI command facts (ground truth — do not re-derive)

Wire framing (datasheet Table 10-1, and `Module.cpp SPItransferStream`):
MISO byte **0 = RFU**, byte **1 = status byte**, then the command response.
So for a 1-byte command, data starts at `rx[2]`; `readReg` (cmdLen 3) at
`rx[4]`; ReadBuffer (cmdLen 2) at `rx[3]`. The status byte (`rx[1]`) carries
chip-mode bits [6:4] and command status bits [3:1]: `0x02` data available,
`0x03` command timeout, `0x04` processing error, `0x05` execution failure.

| Command | Opcode | Reply (data bytes, `rx` index) |
|---|---|---|
| GetIrqStatus | 0x12 | `(rx[2] << 8) \| rx[3]` |
| GetRxBufferStatus | 0x13 | `len = rx[2]` (8-bit), `start = rx[3]` |
| GetRssiInst | 0x15 | `rssi = rx[2]`, dBm = `-rssi / 2` |
| GetPacketStatus | 0x14 | `rssiPkt = rx[2]` (dBm `-x/2`), `snrPkt = rx[3]` (dB `x/4`, signed), `signalRssiPkt = rx[4]` |
| GetDeviceErrors | 0x17 | `(rx[2] << 8) \| rx[3]` |
| ReadRegister | 0x1D | data from `rx[4]` (the code's `readReg` is correct) |
| ReadBuffer | 0x1E | data from `rx[3]` (after opcode + offset) |
| ClearIrqStatus | 0x02 | write, 2 param bytes |

Other verified facts:

- **SetDioIrqParams 0x08 = 8 param bytes: `IrqMask[15:0], DIO1Mask[15:0],
  DIO2Mask[15:0], DIO3Mask[15:0]`. A DIO fires only when a bit is set in
  BOTH `IrqMask` and the corresponding `DioxMask`** (datasheet §13.3.1).
  There is no "params" field. The firmware's pattern: reprogram
  `setDioIrqParams(mask, mask)` per mode — `startReceive(timeout, RX_IRQS,
  RX_IRQS, 0)`, CAD gets the CAD mask (RadioLib `SX126x.cpp:604`).
- **DIO1 is HIGH while an IRQ is pending** (datasheet RX sequence: "wait for
  DIO1 HIGH"; RadioLib attaches `GpioInterruptRising`, upstream master).
  DIO2 = 1 during TX (RF switch control).
- LoRa header type: **`0x00` = explicit, `0x01` = implicit**. LoRa CRC:
  **`0x00` = off, `0x01` = on** (2-byte; RadioLib `SX126x_commands.h:242-245`).
  PayloadLength param is ignored in explicit mode.
- SetRx 0x82: timeout × 15.625 µs; `0x000000` = single-shot, `0xFFFFFF` =
  continuous. SetCad = `SetCadParams` 0x88 (7 bytes: symNum, detPeak,
  detMin, exitMode, timeout[23:0]) then `SetCad` 0xC5 (no params). Chip
  default detMin = 10 (0x0A).
- IRQ bits: TxDone 0x0001, RxDone 0x0002, Preamble 0x0004, HeaderValid
  0x0010, HeaderErr 0x0020, CrcErr 0x0040, CadDone 0x0080, CadDetected
  0x0100, Timeout 0x0200.
- CRC-failed frame = `CrcErr`, or `HeaderErr` without `HeaderValid`
  (RadioLib `readData` logic) — kept, flagged, never decoded.
- BW codes: 62.5→0x03, 125→0x04, 250→0x05, 500→0x06.
- Register 0x0736 bit 2 (IQ errata, datasheet §15.4): **1 = standard IQ,
  0 = inverted IQ**.
- Register 0x08AC: 0x96 = RX boosted gain (correct in the code).
- Image calibration 0x98 `{freq1, freq2}` band codes: 430-440 0x6B/0x6F,
  470-510 0x75/0x81, 779-787 0xC1/0xC5, 863-870 0xD7/0xDB, 902-928
  0xE1/0xE9 (POR default band).
- SetDIO3AsTcxoCtrl 0x97 = `tcxoVoltage, delay[23:0]` (delay × 15.625 µs);
  voltages: 0x00=1.6, 0x01=1.7, 0x02=1.8, 0x03=2.2, 0x04=2.4, 0x05=2.7,
  0x06=3.0, 0x07=3.3 V.
- `bwKhz10` (lora_pkt.h) = bandwidth in **100 Hz units** (tenths of a kHz):
  625 = 62.5 kHz, 2500 = 250 kHz. `bwKhz10 / 10` = kHz.
- Shim `LoraRadio::IRQ_*` equal the chip's native bit positions
  (lora_radio.h:48-55), so masks pass through the backend unchanged.

## Tier 1 — fatal (each one alone stops the radio)

### T1.1 Reset never released — chip held in reset forever
- Where: `begin()` :296-299 (`s.reset->set(1, 0)`), `resetBus()` :464.
- Why: `GpioLines` requests exactly one line (index **0**); `set(int i, int v)`
  takes the index first. `set(1, 0)` targets nonexistent line 1 → EINVAL,
  ignored → NRST stays low from :297; all SPI hits a chip in reset. Call
  sites use `(value, index)` order against a `(index, value)` method.
- Fix: `s.reset->set(0, 1)` at :298 and :464.

### T1.2 `begin()` never programs the sync word before the readback
- Where: readback :330-332; `setSyncWord()` :259 only called from `apply()`
  :358.
- Why: right after reset, 0x0740/0x0741 hold POR values, not 0x24B4 →
  `r.ok` always false (`err = -3`).
- Fix: call `setSyncWord(0x2B)` in `begin()` before :330.

### T1.3 Version read transfers 4 bytes → zero data bytes
- Where: :305-307.
- Why: 4-byte transfer; data starts at `rx[4]` → 0 bytes copied → `version`
  empty → `chipFound` always false.
- Fix: transfer 20 bytes `{0x1D, 0x03, 0x20, 0x00, 16×0}`, copy `rx[4..19]`
  (mirrors firmware `rawVersion`, lora_radio.cpp:194).

### T1.4 `startReceive()` uses opcode 0x87 — not a command
- Where: :368, :375.
- Fix: **0x82** (SetRx). The timeout encoding (ms × 64 = 15.625 µs ticks) is
  already correct.

### T1.5 `startCad()` sends 0x82 (SetRx) instead of CAD
- Where: :380-386.
- Why: the 5-byte CAD payload is parsed as a SetRx timeout → "CAD" is timed
  continuous RX. No `SetCadParams` (0x88) sent, no `SetCad` (0xC5) issued,
  CAD bits (0x0080/0x0100) absent from the `begin()` mask, and the timeout
  is passed as raw ms (needs ×64).
- Fix: `startCad()` → `cmd(0x88, {sym, detPeak, detMin, exitMode, t>>16,
  t>>8, t})` with `t = ms × 64`, then `cmd(0xC5, {})`. `detMin`: 0x0A (chip
  default) when not supplied. `detPeak`: the sniffer passes `cadPeak(p) =
  sf + 16` (lora_sniffer.cpp:144) for all SF ≥ 10 profiles; raise the
  `detPeak ? : 0x01` fallback at :383 to `sf + 16` (the 0x01 fallback only
  bites SF < 10 profiles, which the sniffer doesn't use in Phase A — latent,
  not active). Before CAD, reprogram SetDioIrqParams with the CAD mask
  (`CadDone | CadDetected` in both IrqMask and DIO1Mask), as the firmware
  does (RadioLib `SX126x.cpp:604`).

### T1.6 `irq()` reads the wrong command, byte-swapped
- Where: :395-398.
- Why: opcode 0x17 is GetDeviceErrors, not GetIrqStatus (0x12); and it
  assembles `rx[3] << 8 | rx[2]` (byte-swapped). The sniffer never sees
  RX_DONE. Note `begin()`'s own device-error read (:333-334) uses the
  **correct** order — the file is inconsistent with itself.
- Fix: `auto rx = s.spi->xfer({0x12, 0, 0, 0});` return
  `(uint16_t)((rx[2] << 8) | rx[3])`.

### T1.7 DIO1 mask left at 0 — the pin is never driven
- Where: `begin()` :327-329 — `cmd(0x08, {irqMask>>8, irqMask, 0,0, 0,0,
  0,0})`.
- Why: the first u16 is the **overall IrqMask** (set correctly to the
  firmware's RX_IRQS set — do not "fix" it); the second u16 is the
  **DIO1Mask**, and the datasheet requires the bit to be set in BOTH masks
  for DIO1 to fire. DIO1Mask = 0 → DIO1 stays low forever → `waitIrq` (which
  correctly waits for high, see T1.8) always times out. Deterministic, not
  a datasheet question.
- Fix: second u16 = the same IRQ set: `cmd(0x08, {mask>>8, mask, mask>>8,
  mask, 0,0, 0,0})`. And per-mode reprogramming as in T1.5 (CAD mask during
  CAD).

### T1.8 (corrected) `waitIrq` polarity is CORRECT
- Where: :410 — `if (s.irq->get(0) > 0) return true;`.
- DIO1 is **HIGH while an IRQ is pending** (datasheet RX sequence "wait for
  DIO1 HIGH"; upstream RadioLib `setDio1Action` uses `GpioInterruptRising`).
  The level poll is right; once T1.7 arms DIO1 and T1.9(d) clears the IRQ
  after each frame, the poll loop works.
- Remaining (not fatal): the 1 ms busy-poll is a battery concern on the
  cardputer — see H.3.

### T1.9 `readPacket()` is broken in four ways
- Where: :416-438.
- a) Length: `len[2] | len[3] << 8` parses the wrong bytes. Correct:
  `len = rx[2]` (8-bit) from the 0x13 reply.
- b) Buffer start: taken from 0x12 (not a buffer-index command). Correct:
  `start = rx[3]` from the same 0x13 reply.
- c) Data: `xfer({0x1E, startByte, 0})` clocks 3 bytes → at most one data
  byte. Correct: transfer `2 + len` bytes `{0x1E, start, len×0}`; data
  starts at `rx[3]`.
- d) **Never clears the IRQ status** (both reviews flag this; the sniffer
  deliberately leaves RX_DONE clearing to the backend, lora_sniffer.cpp:214;
  the ESP32 backend clears inside `readData`, RadioLib `SX126x.cpp:567`).
  With the flag latched and DIO1 low, the same frame re-reads in a loop.
  Fix: end `readPacket` by clearing the consumed flags (all, as RadioLib
  does).
- Net today: with T1.1–T1.7 fixed, every frame would still come out 1 byte
  and then re-read forever.

### T1.10 (corrected, downgraded) Header/CRC bytes are CORRECT for Phase A
- Where: `setPreamble()` :270-275 — byte 3 = `0x00` = **explicit** (verified:
  RadioLib `SX126x_commands.h:242`, upstream master), byte 5 = `0x01` =
  CRC on. Both are the correct settings for Meshtastic/MeshCore (explicit
  header, 2-byte CRC).
- Only fix: byte 4 (PayloadLength) is `0xFF`. Ignored in explicit mode, but
  must be `p.implicitLen` for `PF_IMPLICIT` profiles:
  `payloadLen = implicit ? p.implicitLen : 0`.
- (Earlier draft of this doc claimed "implicit-255" here — that was wrong in
  both the constant mapping and the severity.)

## Tier 2 — wrong settings (decoding fails even after Tier 1)

### T2.1 Bandwidth lookup never matches
- Where: `apply()` :356 passes `p.bwKhz10 / 10` (e.g. 62, 125, 250, 500);
  the table at :265 is keyed in `bwKhz10` units ({625, 1250, 2500, 5000});
  fallback code is 0x05.
- Effect: every profile misses the table; 250 kHz is correct only by the
  fallback coincidence. 62.5 kHz (MeshCore US) gets the 250 kHz code —
  will not decode.
- Fix: pass `p.bwKhz10` directly (table values 0x03/0x04/0x05/0x06 are
  correct), or re-key the table to kHz.

### T2.2 LDRO not auto-enabled
- Where: :356 — `ldro = (p.flags & PF_LDRO) ? 1 : 0`.
- Reference: the chip rule (symbol time ≥ 16.38 ms) via
  `Lora::autoLdro(sf, bwKhz10)` (lora_pkt.h). Without it, SF11/SF12 at
  narrow bandwidths won't decode.
- Fix: `ldro = (p.flags & PF_LDRO) || Lora::autoLdro(p.sf, p.bwKhz10)`.

### T2.3 IQ inversion missing the SX1262 errata register
- Where: `setPreamble()` sets only the InvertIQ parameter byte; register
  0x0736 is never touched.
- Fix (datasheet §15.4, direction verified): write 0x0736 bit 2 =
  **1 when standard, 0 when inverted** (`PF_INVERT` → clear the bit).

### T2.4 Image calibration
- `apply()` :340-342 computes the band and updates `s.calHz` but never
  sends CalibrateImage 0x98 (unlike `tuneHz()` :449-453). Fix: send it in
  `apply()` too, in standby.
- Both sites hardcode the 902-928 codes (0xE1/0xE9) = the POR default band.
  Codes must follow the frequency (table above); an 869 MHz EU profile
  would be calibrated for the wrong band.
- The 0x98 format `{freq1, freq2}` used in the code is correct.

### T2.5 `readPacket` leaves signal/header fields empty
- Where: :428-437 — only len/data/freq/bw/sf/sync/pflags/profile/toaUs set.
- Missing (with the verified 0x14 mapping): `rssi` = `-rx[2]/2` dBm,
  `snr4` = signed `rx[3]`, `ferrHz` from registers 0x0715/0x0716
  (REG_FREQ_ERROR_RX_CRC, ×BW scaling per RadioLib `getFrequencyError`);
  `cr` from the LoRa RX header-info register (hardcoded `cr = 5` at :436 is
  wrong otherwise); `flags` — `PK_IMPLICIT` when implicit,
  `PK_CRC_ERR` when `CrcErr` or (`HeaderErr` and not `HeaderValid`)
  (RadioLib `readData` rule) — the sniffer's CRC-error accounting
  (lora_sniffer.cpp:199) depends on it.
- Effect today: every frame displays 0 dBm / 0 SNR and gate G6 (behavior
  matches the reference) cannot pass.

### T2.6 No BUSY gate before commands
- `cmd()` :240-243 transmits immediately. The chip requires BUSY low before
  every command (datasheet §8.3.1; RadioLib gates every command).
  `begin()` gates manually after mode changes/calibration; `cmd()` itself
  does not → dropped commands after mode switches → intermittent failures.
- Fix: `waitBusy()` (short timeout) at the top of `cmd()`.

## Tier 3 — memory safety and resource handling

### H.1 `Spi::xfer` stack overflow
- Where: :174-184 — `txBuf[256]`/`rxBuf[256]`, `std::memset(rxBuf, 0, n)`
  (:177) and `put_u32(16, n)` (:184) unclamped (only the tx copy at :176 is
  clamped). A correct 255-byte ReadBuffer transfer is 258 bytes → fixing
  T1.9c triggers a stack overflow.
- Fix: size the buffers from `n` (or `std::vector`).

### H.2 Handle leak / EBUSY on re-`begin()`
- A throw mid-`begin()` (e.g. busy/irq `GpioLines` ctor after `reset`
  succeeded, :285-288) returns `err = -2` leaving lines claimed; a second
  `begin()` hits EBUSY. `resetBus()` is the only cleanup and is never
  called, although lora_lx.h:22 claims `begin()`/`standby()` call it.
- Fix: tear down everything allocated on every `begin()` failure path (and
  call `resetBus()` at the top of `begin()` so a retry starts clean);
  correct the header comment or wire the calls.

### H.3 `waitIrq` busy-polls every 1 ms
- Battery concern (cardputer-zero/BATTERY-POWER.md); `<poll.h>` is included
  (:8) but unused. The polarity is correct (T1.8) — this is a
  battery/latency improvement, not a correctness fix: request the irq line
  as an edge event (DIO1 rising, debounce attr) and `poll()` the line fd.

### H.4 Cap-power I2C expander writes are no-ops
- Where: `capPowerOn()` :67-80. On i2c-dev with `I2C_SLAVE`, each `write()`
  is a separate I2C transaction, and the PI4IOE treats the first byte of a
  transaction as the register pointer. A 1-byte `write(val)` therefore only
  **moves the pointer** — the value is never written to any register. The
  `rd` lambda (write pointer, then read) does work. The expander is only in
  the right state because pocket-lorascan/boot already left it at
  (`out=0x01 hiz=0xFE dr=0x01`, per LORA_CM0.md); on a cold, unconfigured
  cap bus the LoRa rail may never be enabled.
- Fix: one 2-byte write `write(fd, {reg, val}, 2)` for `wr`; check return
  values. Severity is boot-state-dependent (harmless while something else
  already configured the expander; dead on a cold bus).

## Tier 4 — hygiene / build

- **Includes**: `<algorithm>` (std::min, :374,:384) and `<cstdlib>`
  (std::atoi in `capPowerOn`) missing — compiles only via transitive
  libstdc++ includes. Add them; qualify `memset` with `std::` (:282, :417).
- **Makefile**: `SQUACH_LORA` flips `-DSQUACH_LORA` without invalidating
  existing objects → mixed builds. Stamp the flag (e.g. `.deps/SQUACH_LORA`
  file, or suffix objects when enabled).
- **`tcxoDeci = code × 5`** (:340) is only correct for code 0x06 (3.0 V);
  the mapping is not linear (table above: 0x02 = 1.8 V → `tcxoDeci` should
  be 18, code × 5 says 10). Fix the table or the report.
- **Startup frequency** :323 = 915.0 MHz vs the spec's 906.875 MHz
  LongFast — cosmetic (every profile is retuned by `apply()`), align with
  the spec.
- **Status byte unchecked**: `rx[1]` of every read carries command status
  (0x03 timeout, 0x04/0x05 command errors). RadioLib checks it under
  `RADIOLIB_SPI_PARANOID`; a cheap `cmd()`-level check (fail on 0x03-0x05)
  would catch dropped commands early (pairs with T2.6).
- **GPS header comment** (lora_lx.h:10): on gpiochip0 (SoC GPIO chip),
  line N = GPIO N, so "GPS path on GPIO14/15 (GPIO 0/1 on gpiochip0)" is
  wrong — GPIO14/15 are lines 14/15 (the 0/1 refers to something else,
  probably expander lines). Fix the parenthetical; the code never touches
  those pins, so this is doc-only.
- **Spec/process mismatches** (fix in `docs/LORA_CM0.md`, not code): file
  named `lora_lx_sx1262.cpp` in spec vs `lora_lx.cpp` in code; spec
  interface `lora_start/lora_scan_once/lora_poll` vs implemented
  `LoraRadio::` contract; spec date "2026-11-24" postdates the review;
  "not yet approved" status vs code already written; the antenna
  "FCC-compliant" claim is asserted and then admitted to be unsourced.

## 5. Open questions — settle on device before ticking gates

1. **0x13 reply on this chip**: the datasheet layout (len = rx[2], start =
   rx[3]) is now certain, but the bring-up test should still print a real
   reply — one on-device run confirms framing end-to-end.
2. **Kernel uapi recency**: the 592-byte `gpio_v2_line_request` ABI
   requires a recent kernel; pocket-lorascan (same approach) works on
   device, so likely fine — but it is an unstated dependency.
3. **Version string**: the ESP32 backend field-verifies this read; print it
   in the test (a 20-byte transfer behaves identically, T1.3).

Bring-up test binary (do this **before** full integration): prints version
string, sync-word readback, device errors, IRQ word, and a 0x13 reply after
a real or injected preamble. Its output is the evidence for gates G2/G3.
(The DIO1-mask and I2C questions from the earlier draft are resolved by the
datasheet — no longer open.)

## 6. Fix order

1. T1.1 (reset), H.4 (I2C) — power path.
2. T1.3 (version), T1.2 (sync set + readback) — bring-up gates.
3. T1.4, T1.5, T1.6, T1.7 — mode/IRQ plumbing (T1.7 is the one that makes
   DIO1 fire at all; T1.8 needs no change).
4. T1.9 (readPacket rewrite, incl. IRQ clear) — after H.1 (buffer sizing).
5. T1.10 (implicit payloadLen), T2.1 (BW), T2.2 (LDRO), T2.3 (IQ errata) —
   per-profile config.
6. T2.4 (image cal), T2.5 (packet status fields), T2.6 (BUSY gate).
7. H.2 (cleanup paths), Tier 4 hygiene, H.3 (event poll).
8. Build the §5 test binary, run on device, paste output under G2/G3, then
   only then integrate `lora_sniffer` + the LORA page (gates G4–G6).
