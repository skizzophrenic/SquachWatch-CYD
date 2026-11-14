# Detection signatures — provenance and confidence

This file documents every signature SquachWatch-CYD matches, with
the source it came from, the confidence level, and any caveats.

Confidence:
- **High** — multiple independent sources, verified against the
  canonical research (Flock You, Marauder, Eye Spy).
- **Medium** — one or two sources, plausible but not cross-verified.
- **Low** — single source, anecdotal, may have false positives.

---

## Flock Safety — `FLOCK` — **one High prefix, the rest Low**

**Why it works:** Flock Safety cameras have on-board WiFi modules
(typically ESP32) that periodically emit probe requests searching for
available networks. The `addr1` (receiver) trick catches "sleeper"
cameras that aren't actively transmitting — they still leak their MAC
when the promiscuous listener sends them a frame.

**What the audit found.** Every prefix in this table was checked against
the IEEE registry. Of the 29 entries it had then, exactly **one** is
registered to Flock Safety: `B4:1E:52`. Sixteen are Espressif blocks; the
rest are Liteon, one Shenzhen Intellirocks, one SPECTRA - TEK (labelled
"Flock-Sierra" for a long time, which is neither Sierra nor Flock), one
not in the registry at all, and one locally-administered address, which
by definition identifies no vendor.

v1.24.0 added twenty more from Flock-You's field list and a Flock
camera's firmware dump: thirteen Liteon, two USI, three Silicon Labs, one
Espressif, and Atheros's factory-default block. All module vendors, so
all Low. That makes **49** Flock prefixes, still with exactly one High.
One prefix from that list was left out on purpose: `48:27:EA` is
registered to Samsung Electronics and would call every Samsung TV a
Flock camera.

That is not an argument for deleting them. Flock really does build on
ESP32, so an Espressif prefix really is evidence — it is just evidence
shared with every dev board, smart plug and hobby project on earth,
including **another SquachWatch**. Two of these devices in a room would
otherwise flag each other as ALPR cameras.

So the rows stay and the *grading* changed. Confidence is now a property
of the matched signature rather than of the type:

| Grade | Meaning |
|---|---|
| **High** | The block is registered to the company that makes the product. |
| **Medium** | Registered to a parent whose range is far wider than the product — Amazon owns Ring, and also Echo, Fire TV and Kindle. |
| **Low** | A module or ODM vendor whose parts are in everything, or a block not in the IEEE registry. |

Across all 97 OUI rows in the firmware that comes out at 33 High, 4
Medium, 60 Low.

**This is what makes ALERT FILTER work.** It has always been a minimum-
confidence filter, and until now confidence was constant per type, so it
had nothing to filter on. Set it to High and a passing ESP32 stays in the
log without taking over the screen.

**Not only prefixes.** Flock is also matched by a WiFi network called
exactly `Flock` (exact, not a prefix, so "Flockhart Family WiFi" is
safe), the `flock-` / `FLOCK-` setup SSIDs, Flock's own Bluetooth
accessory service (a 128-bit UUID from a camera's firmware), the BLE
names `Flock`, `Penguin`, `Pigvision`, `Flock_Setup` and `FS Ext
Battery`, the BLE company ID `0x09C8` (XUNTONG, Flock's radio supplier),
and a battery pack that names itself with a bare ten-digit serial. That
last one is a string any gadget could carry, so it is logged Low.

**Source:** [`colonelpanichacks/flock-you`](https://github.com/colonelpanichacks/flock-you)
(MIT) — the canonical Flock detector project. OUI research by
`@NitekryDPaul` and the DeFlockJoplin community. Registrant for every
prefix verified against the IEEE MA-L registry.

**Note:** Flock has reportedly turned off Bluetooth on newer hardware, so
BLE-name based detection is opportunistic.

---

## Axon / Taser — `AXON` — **High confidence**

**Why it works:** Axon body cameras emit `AB2-`, `AB3-`, `AB4-`
SSIDs while in pairing mode. They also use the `00:25:DF` (legacy
Taser International) and `E4:05:40` (modern Axon) OUIs.

**Source:** [Axon Evidence admin docs](https://www.axon.com/help/admin-and-it/software/admin-and-it/device-management/device-settings/bwc-wifi-networks.htm)
(public) + ESPHome community OUI registry
+ Gemini-compiled signature set.

**Confidence in v1.0:** **High** for the SSID-prefix match (we catch
the camera in pairing mode). **Medium** for the OUI match (depends
on the camera being powered on and transmitting).

**Since the OUI audit** the prefixes carry their own grades: `00:25:DF`,
the legacy Taser International block, is High; `E4:05:40` and
`28:24:FF` are Low.

---

## Camera glasses — `META` (shown as `GLASS`) — **Medium confidence**

**Why it works:** Two independent signatures.

The specific one is BLE service UUID `0xFD5F`, which Ray-Ban Meta
glasses advertise. That one is High on its own.

The broad one is the Bluetooth SIG company IDs that the dedicated
glasses-spotting apps key on: `0x01AB` Meta Platforms, `0x058E` Meta
Platforms Technologies, `0x0D53` Luxottica (who make Ray-Ban), and
`0x03C2` Snap, for Spectacles. This is what turns a one-product
detection into a category one.

**Source:** [Eye Spy project](https://simeononsecurity.com/articles/eye-spy-passive-surveillance-detector-esp32-2026/),
the [HN "glasses to detect smart-glasses" project](https://news.ycombinator.com/item?id=46075882),
[banrays](https://github.com/NullPxl/banrays), and the company-ID list
reported for the Nearby Glasses / AntiZuck apps
([VR.org](https://vr.org/articles/smart-glasses-bluetooth-detection-apps-antizuck-2026)).

**Confidence:** **Medium**, and it was High before the company IDs
were added. Meta puts those same IDs on their other Bluetooth
products, Quest headsets included, so a match means a Meta radio is
nearby rather than a camera necessarily pointed at you. Graded down
deliberately: this file's rule is that a type carrying signatures of
mixed quality reports the lower one.

---

## Card skimmers — `SKIMMER` — **High confidence**

**Why it works:** Cheap Bluetooth-enabled card skimmers are built
from HC-05 / HC-06 / HC-03 modules (or similar — RN42, BT04-A)
and broadcast the module's default name. The classic
serial-port-profile (SPP) UUID `0x1101` is also exposed.

**Source:**
[Sparkfun Skimmer Scanner](https://github.com/sparkfunX/Skimmer_Scanner),
[ESP32Marauder "Detect Card Skimmers"](https://github.com/justcallmekoko/ESP32Marauder/wiki/detect-card-skimmers),
[Eye Spy](https://simeononsecurity.com/articles/eye-spy-passive-surveillance-detector-esp32-2026/).

**Confidence in v1.0:** **High** for the BLE name match.
**Medium** for the SPP UUID (some skimmers use different SPP
implementations). BT Classic inquiry is **not** enabled (it
conflicts with NimBLE on a single radio) and still has not been
switched on, so skimmers are caught by their BLE name. Three module
OUIs (`20:13:00`, `98:D3:00`, `00:1A:7D`) also match, graded Low.

---

## Raven gunshot detector — `RAVEN` — **Medium confidence**

**Why it works:** Raven devices advertise five custom service UUIDs,
`0x3100`, `0x3200`, `0x3300`, `0x3400` and `0x3500` (proprietary, not in
the Bluetooth SIG assigned range), and only those five match. From
v1.24.0 to v1.35.0 every value from `0x3100` to `0x3500` matched, on the
strength of `0x3101` and `0x3102`, the two that hand out a Raven's GPS
position. Those sit inside its GPS service rather than in its advert,
and the wide range named an NVIDIA Shield a Raven (issue #41).

**Source:** [Flock You documentation](https://github.com/colonelpanichacks/flock-you/wiki/Detection-Datasets#raven).

**Confidence in v1.0:** **Medium**. We match the UUIDs but haven't
verified them against a physical Raven device. Likely works.

---

## Apple AirTag / FindMy — `AIRTAG` — **High confidence**

**Why it works:** AirTags broadcast a manufacturer data payload
from Apple (`0x004C`) with a known subtype byte. Originally
documented here as `0x12` ("near owner") / `0x1E` ("separated") per
the Apple FindMy spec — but a real AirTag test (registered to an
Apple ID whose iPhone no longer exists, battery freshly reinserted)
showed it broadcasting subtype `0x07` ("Proximity Pairing", the same
message used for "Connect to AirTag?" setup prompts) rather than
`0x12`/`0x1E`, which only start once a tag has been in "separated"
state for a while. `0x07` is now matched too, so a tag is caught
before it reaches full lost-mode — at the cost of some false-positive
risk, since other Apple accessories (AirPods, etc.) also use `0x07`.

**Source:** Apple FindMy spec (public) + the
[ESP32Marauder AirTag sniffer](https://github.com/justcallmekoko/ESP32Marauder)
+ the [Eye Spy](https://simeononsecurity.com/articles/eye-spy-passive-surveillance-detector-esp32-2026/) scoring table.

**Confidence in v1.0:** **Medium** — the `0x12`/`0x1E` match alone
would be High, but including `0x07` for faster detection trades some
of that away (see above). Note: AirTags rotate their address
frequently, so detection may flicker in and out.

---

## Tile trackers — formerly under `AIRTAG`

In v1.0 Tile's `0xFEED` service UUID (from
[Eye Spy](https://simeononsecurity.com/articles/eye-spy-passive-surveillance-detector-esp32-2026/))
was counted as `AIRTAG`. Tile has its own `TILE` type now — see
[Tile trackers — `TILE`](#tile-trackers--tile--high-confidence) below.

---

## OpenDroneID drones — `DRONE` — **Medium confidence**

**Why it works:** ASTM F3411 Remote ID broadcasts over BLE in the
advert's **service data** under UUID `0xFFFA`. The message fills the
whole advert, so a real drone has no room left to list `0xFFFA` among
its services — which is where the check looked until v1.24.0, and why
no real drone was ever reported before then. It reads the service data
now. Drones also put Remote ID in their **WiFi beacons**, as an
ASD-STAN vendor element (`FA:0B:BC`, type `0x0D`) carrying a pack of
messages; since v1.24.0 those are logged as `DRONE` too, named by the
serial in the pack.

**Source:** [ASTM F3411 spec](https://www.astm.org/f3411-22.html) +
[Eye Spy](https://simeononsecurity.com/articles/eye-spy-passive-surveillance-detector-esp32-2026/).

**What it reports:** the payload is decoded now, not just flagged.
An ASTM F3411 advert carries a 25-byte message, and three of the types
are worth reading: Basic ID gives the aircraft serial and airframe
type, Location gives its position and altitude, and System gives the
**operator's** position — where the pilot is standing. A drone sends
those as separate adverts, so the decoder accumulates them per MAC.
Offsets and scaling taken from
[opendroneid-core-c](https://github.com/opendroneid/opendroneid-core-c);
coordinates are degrees x 10^7, altitude is half-metres offset by 1000.

The full decode — position, altitude, the operator — comes from the
Bluetooth form. A WiFi beacon names the drone by its serial in the log.

**What it cannot reach:** Bluetooth 5 Long Range. That is out of scope
permanently on the original board — the ESP32-WROOM is BLE 4.2 and
Espressif document no hardware support for Coded PHY or extended
advertising — and no build listens for it on the newer chips either.
Bluetooth *Legacy* and WiFi beacons are both heard.

**Confidence:** **Medium**. Compliance is rolling out so detection is
opportunistic, and the Long Range transport is unreachable per above.

---

## Motorola / Genetec ALPR — `ALPR` — **Medium confidence**

**Correction, and the reason this section was rewritten.** Until
v1.5.20 the only prefix here was `00:0E:58`, documented above as
Vigilant hardware. It is not Vigilant hardware. The IEEE registry
assigns that block to **Sonos, Inc.** of Goleta, California, in
February 2004, and still does — so every Sonos speaker within range
was being logged as a licence-plate reader. It has been removed
rather than corrected, because there was nothing to correct it to.

**Why it works:** IEEE MA-L blocks registered to the vendors who
actually sell these systems. Motorola Solutions, who absorbed
Vigilant: `00:04:7D`, `00:18:85`, `00:1F:92`, `4C:CC:34`, plus
`B8:E2:8C`, which is registered to Motorola Solutions Malaysia rather
than the US parent. Genetec, whose AutoVu line is an LPR platform:
`00:BF:15`, `0C:BF:15`.

**Source:** the IEEE registry itself, read per vendor rather than
copied from another detector — which is how the Sonos entry got in.
Vendor list cross-checked against
[FlipDeFlock](https://github.com/ReconGrunt/FlipDeFlock).

**Confidence:** **Medium** for the type. The blocks are certain; what
is not certain is that a given device on one is a plate reader rather
than some other product from a large vendor. That is the same caveat
`CAMERA` carries. Under the per-signature grading at the top of this
file the seven rows themselves are graded High, because each block
really is registered to the vendor named.

---

## Generic / covert IP cameras — `CAMERA` — **graded per row**

**Why it works:** Most consumer IP cameras use WiFi modules from a
small set of manufacturers. We match OUIs from Wyze, Arlo, Blink,
Hikvision, Amazon, Realtek, and Tuya on the consumer side (Ring has
its own type, below), plus Verkada, Avigilon (Alta), and Axis
Communications on the
commercial/institutional side — the brands actually installed in
offices, stores, and public spaces, not just homes.

**Source:** [`skizzophrenic/Cardputer-CSI-Human-Detector`](https://github.com/skizzophrenic/Cardputer-CSI-Human-Detector)
(this author's earlier work, MIT) + Gemini additions for the Tuya
and Wyze-module prefixes. Commercial-vendor OUIs (Verkada `E0:A7:00`,
Avigilon Alta `70:1A:D5`, Axis `00:40:8C`/`B8:A4:4F`) are from the
public IEEE MA-L registry via [maclookup.app](https://maclookup.app),
cross-checked per-vendor.

**Confidence in v1.0:** **High** for the listed vendors — that's the
only camera-matching path actually implemented right now. A
generic "flag any Espressif OUI as a possible camera" fallback was
discussed (see the note atop `kOuiTable` in `signatures.cpp`) but
would need a real audit of Espressif's OUI ranges against known false
positives before shipping — it is **not** built, and the UI does not
show a Low-confidence camera reading in v1.0.

**Since the OUI audit** the rows carry their own grades: the blocks
registered to Wyze, Hikvision, Verkada, Avigilon, Axis and Arlo are
High; Amazon's two are Medium (Amazon makes far more than cameras);
Realtek, Tuya, Blink and Wyze's module block are Low. Two rows were
relabelled by the audit: `F0:27:2D` was "Hikvision" and is Amazon's,
and `28:57:BE` was "Reolink" and is Hikvision's.

**And one the audit graded instead of checking.** `BC:DD:C2` was
labelled "Arlo" and graded Low. It is registered to **Espressif**, so
it matched ESP32s -- dev boards, ESPHome sensors, smart plugs, and
SquachWatch itself -- as covert cameras, while no real Arlo was ever
matched, because none of Arlo Technology's own three blocks
(`FC:9C:98`, `48:62:64`, `A4:11:62`) were in the table at all. They are
now, at High, and the Espressif row is gone. Note that this is the
fallback the paragraph above says is *not* built: one Espressif OUI was
flagging cameras regardless, because it did not look like Espressif.
`tools/check_ouis.py` now reads this table against the IEEE registry so
the next one is caught by a job rather than by a reader.

---

## Samsung Galaxy SmartTag / SmartTag+ — `SAMSUNG_TAG` — **High confidence**

**Why it works:** SmartTags advertise Samsung's own Bluetooth SIG-
assigned 16-bit service UUID, `0xFD5A`, used specifically for SmartTag
discovery (Samsung also has separate assigned UUIDs for onboarding,
`0xFD59`, and firmware update, `0xFE59`, which we don't need for
detection). Unlike AirTag's manufacturer-ID scheme, this UUID isn't
shared with any of Samsung's other product lines.

**Source:** Bluetooth SIG's public 16-bit UUID assignment registry
(`0xFD5A` → Samsung Electronics) + [arXiv:2210.14702](https://arxiv.org/pdf/2210.14702),
an academic security/privacy analysis of Samsung's crowd-sourced
Bluetooth location system that reverse-engineered the SmartTag
protocol.

**Confidence in v1.0:** **High** — a dedicated, SIG-assigned UUID
specific to this product line, same tier as the META match.

---

## Google Find My Device Network trackers — `GOOGLE_TAG` — **Medium confidence**

**Why it works:** Trackers on Google's Find My Device Network
(Chipolo ONE/CARD Point, Pebblebee Card/Clip/Tag, Moto Tag) advertise
under Google's `0xFEAA` service UUID — the same UUID Google has used
for years for general-purpose "Eddystone" beacons. That reuse is the
catch: retail/asset/museum Eddystone beacons unrelated to tracking
also use `0xFEAA`, so a match here means "some Google-beacon-class
device," not specifically a tracker.

**Source:** [Google's official Find My Device Network (FMDN)
specification](https://developers.google.com/nearby/fast-pair/specifications/extensions/fmdn)
(Fast Pair extension docs).

**Confidence in v1.0:** **Medium** — real, current Google documentation,
but the UUID itself is shared with non-tracker Eddystone beacons, so
higher false-positive risk than the Samsung match above.

---

## Tile trackers — `TILE` — **High confidence**

**Why it works:** Tile devices advertise under two 16-bit Bluetooth
service UUIDs, `0xFEED` and `0xFEEC`, both officially assigned to
Tile, Inc. by the Bluetooth SIG. This match previously existed in the
codebase but was bucketed under `AIRTAG` (both being "tracker class"
devices) rather than getting its own type — it's split out here.

**Source:** Bluetooth SIG's public 16-bit UUID assignment registry
(`0xFEED` and `0xFEEC` → Tile, Inc.).

**Confidence in v1.0:** **High** — SIG-assigned UUIDs specific to this
product line, same tier as the Samsung SmartTag match above.

---

## Ring doorbells / cameras — `RING` — **High confidence**

**Why it works:** Ring (Amazon) devices are matched by their
registered WiFi MAC OUI block — 15 prefixes total, covering Ring
LLC's full public MA-L registration. Two of these prefixes previously
existed in the codebase but were bucketed under the generic `CAMERA`
type; the remaining 13 are Ring LLC's complete registered block,
added here so Ring gets its own dedicated type instead of being
indistinguishable from any other camera.

**Source:** Public IEEE MA-L registry, cross-checked via two
independent lookups (netify.ai and maclookup.app) that agree on the
same 13 prefixes for "Ring LLC" (registered 2019-03-01).

**Confidence in v1.0:** **High** — same evidentiary basis (real MA-L
registry OUI matches) as the generic `CAMERA` type. Since the OUI
audit, the 13 Ring LLC blocks are High and the two that came over from
`CAMERA` (`FC:65:DE`, `68:37:E9`) are Medium.

---

## Apple iBeacon — `IBEACON` — **High confidence**, off by default

**Why it works:** the format is fixed by Apple and every byte of the
header is specified, so this is an exact match rather than a judgement
call. Manufacturer data of `4C 00 02 15`, then a 16-byte proximity
UUID, a 2-byte major, a 2-byte minor and a measured-power byte — 25
bytes exactly.

Before this existed these were being thrown away. Apple's company ID
matched the AirTag rule, the AirTag payload check then correctly said
"not a tag", and the advert was dropped — so the most numerous
tracking transmitter most people walk past all day was the one thing
the detector deliberately ignored.

**What the log shows:** six hex digits of the proximity UUID, then
`major.minor`. The UUID is the *deployment* — every beacon a chain
owns shares it — so the same first half in two different places is the
same operator, which is the part worth seeing. Major and minor are
big-endian inside the block even though the company ID two bytes
earlier is little-endian; that is Apple's format, not a bug.

**Off by default,** and it is the only type that is. This is about
volume rather than importance: one shop can put more beacons in range
than this device would otherwise see all week, and the ALERT screen is
gated on confidence rather than type — so an exact-match signature
would mean every shelf in a supermarket taking over the display. It is
one tap away in DETECTION FILTER.

**Not the same thing as a tracker.** A beacon does not follow you. It
shouts an identifier, and an app you already installed decides to care.
The tracking is real; the beacon is only half of it.

**Source:** Apple's iBeacon specification, cross-checked against the
layout used by every open-source beacon library.

**Confidence:** **High** for "this is an iBeacon". That is all it
claims.

---

## Pentest hardware — `HACKER` — **graded per signature**

One type covering Flipper Zero, Pwnagotchi, WiFi Pineapple and the ESP
deauther family. Four products in one bucket because the useful statement
is "there is a tool for attacking radios in this room", not which model it
is — the model goes in the vendor label, and where the device announces a
name, that name goes in the log row.

Everything else this device finds is equipment that *watches*. This is
equipment that transmits at other radios, which is why it shares the red
of `DEAUTH` and `EVILTWIN` rather than the cyan of the cameras.

### Flipper Zero — two signatures, one of them exact

| Signature | Value | Grade |
|---|---|---|
| BLE service UUID | `0x3081` / `0x3082` / `0x3083` — one per case colour | **High** |
| Advertised name | begins `Flipper ` | **Medium** — the owner can change it |

**Flipper's own company ID and address block are not a Flipper Zero.**
Both used to be on this list. A Zero never sends Flipper's Bluetooth
company ID `0x0E29`, and it has no WiFi, so Flipper's IEEE block
`0C:FA:22` (which only WiFi frames are checked against) can never be one.
The thing both actually caught was the **BUSY Bar**, Flipper's desk sign:
its WiFi uses `0C:FA:22`, and until it is paired its Bluetooth advert
carries `0x0E29` beside its own UUID `0x308A`. Bench, 2026-10-14: a BUSY
Bar flagged as a hacker tool, three real Zeros beside it advertising
`0x3082`/`0x3083` from `80:E1:26` addresses and no company ID at all.

**Two constants the ecosystem gets wrong, and we do not.**

ESP32 Marauder's source comments Flipper's BLE company ID as `0x0FBA`.
`0x0FBA` is registered to **Cosonic Intelligent Technologies**, who make
headsets; Flipper Devices is `0x0E29` in the Bluetooth SIG list. Every
project that copied that constant inherited the error.

Wall-of-Flippers checks MAC prefixes `80:E1:26` and `80:E1:27`. Neither
appears anywhere in the IEEE registry — not under Flipper, not under
anybody. Real Zeros do advertise from `80:E1:26`, but an unregistered
prefix is anybody's to use, and the service UUIDs already catch every Zero.

Both are the same failure as the `00:0E:58` entry that sat here for eleven
releases labelled "Vigilant" while belonging to Sonos: a plausible constant
copied between detectors until it reads as fact. Neither is shipped, and
`test/hacker_test.cpp` pins both so a future re-import turns red.

### Pwnagotchi — the strongest signature here, because it volunteers

A pwnagotchi finds other pwnagotchis by putting a JSON blob into a vendor
information element in its own beacon frames. It is not obfuscated: the
blob is plain ASCII carrying the unit's **name, version, uptime, handshake
count and whether deauth is enabled**.

We do not parse JSON — pulling a parser into the promiscuous callback would
cost heap and time on every beacon in the air. Two byte scans do the job:
the key `pwnd_tot` is what makes this a pwnagotchi rather than any other
device with a brace in its beacon, and the `name` value is what the log
row shows. The name is attacker-controlled and lands in a rendered field,
so anything outside printable ASCII rejects it outright.

This posts the transmitter address rather than the BSSID, and skips the
evil-twin tracker: the SSID on these frames is throwaway, and feeding it in
as a network somebody might be impersonating would be wrong twice over.

**Confidence: High.** Nothing else on this device is a self-declaration.

### WiFi Pineapple and the deauthers — circumstantial

| Signature | Value | Grade |
|---|---|---|
| Pineapple management AP | SSID prefix `Pineapple_` | **Medium** |
| Deauther control AP | SSID prefix `pwned` | **Medium** |
| Hak5 MACs | `02:C0:CA`, `02:13:37` | **Low** |

**Hak5 hold no IEEE registration.** The whole 40,000-row registry has no
entry for them, because their hardware is OpenWRT on ODM boards — anything
claiming a "Hak5 OUI" is naming a contract manufacturer. What they do use
are those two locally administered addresses, which is a real habit and a
real hint; but bit 1 of the first octet is set on both, so by construction
they identify no vendor and anyone can set them. The test suite enforces
that no locally-administered row is ever graded above Low.

### What is deliberately NOT in this bucket

**Bare Espressif and other generic silicon.** A nyanBOX, an ESP32 Marauder,
an M5Stack and a SquachWatch are the same chip. Seventeen Espressif prefixes
already sit under `FLOCK`; adding them here as well would have the two
types fight over the same evidence and make every SquachWatch flag every
other one. HACKER takes exact signatures only, and that is what makes it
worth alerting on.

**Receive-only and wired gear**, which no firmware change can reach:
HackRF, Ubertooth One, RTL-SDR, Proxmark3, Wi-Fi Coconut, any Alfa card in
monitor mode; Bash Bunny, Rubber Ducky, Key Croc, Shark Jack, LAN Turtle,
Packet Squirrel; and the O.MG cable except while its AP is up.

**The Pineapple Pager**, for now. It does 2.4/5/6 GHz plus BT/BLE and very
likely leaks more than the Mark VII, but no signature for it has been
published and a guess is exactly how the Sonos entry happened.

**nyanBOX**, for now — with a caveat, because it has a genuine hook. Its
"Device Networking" feature broadcasts the unit's level and version so that
other nyanBOXes can discover it, which is the Pwnagotchi situation exactly.
The format is not published, so it needs one capture from real hardware
before anything ships.

### On the counter row

`EVILTWIN` has no column of its own; its count is folded into `HACK`. A
rogue AP is not a category of hardware, it is a thing this hardware *does* —
a Pineapple running PineAP karma is an evil twin, the same box seen by its
behaviour instead of its signature. Counting them apart would split one
device across two columns and read as two problems. `DEAUTH` keeps its own
column for the same reason in reverse: a deauth flood is a burst rate, and
plenty of things that are not a Pineapple produce one.

---

## License / attribution

| Source | License | Used for |
|---|---|---|
| flock-you (NitekryDPaul) | MIT (project) | Flock OUI list; v1.24.0's Flock SSID, GATT service, serial-name rule and Raven range |
| ESP32Marauder | GPL-2 | AirTag / skimmer pattern references |
| Eye Spy (simeononsecurity) | (article code) | UUID table and scoring |
| Cardputer-CSI-Human-Detector | MIT | Generic camera OUI list |
| Sparkfun Skimmer Scanner | MIT | Skimmer BT name list |
| Apple FindMy spec | public | AirTag manufacturer format |
| Google Gemini | (compilation assistance) | SSID prefixes, SPP UUID, Sierra Wireless OUI |
| arXiv:2210.14702 (academic paper) | public | Samsung SmartTag UUID |
| Google Find My Device Network spec | public | Google tracker service UUID |
| maclookup.app (IEEE MA-L registry) | public data | Verkada / Avigilon / Axis / Ring OUIs |
| netify.ai (IEEE MA-L registry) | public data | Ring OUI cross-check |
| Bluetooth SIG assigned numbers registry | public | Tile service UUIDs |
| OUI-SPY AXEOFF (colonelpanichacks) | MIT | Axon company ID `0x034D` and service UUIDs `0xFC81` / `0xFE6B` / `0xFE6C` |
| all-cameras-are-beacons (soyboi1312) | Apache-2.0 | Axon `BWCDEVICE` tag, `FS-` Flock names, BodyWorn name, the false-positive work that retired the Espressif Flock rows |
| AirGuard (seemoo-lab) | Apache-2.0 | Tracker service-data matching, DULT, Chipolo |
| Google Fast Pair FMDN spec | public | Find Hub frame types `0x40` / `0x41` |

We use signature *data* (OUIs, UUIDs, names) as facts; we don't
copy GPL code into this project.

---

## 2026-10-14 additions and fixes

Every value below was checked against the IEEE MA-L registry or the Bluetooth
SIG's assigned numbers, except where a row says it comes from field captures.

**Added**

| Type | Signature | Value | Grade | Source |
|---|---|---|---|---|
| `AXON` | BLE company ID | `0x034D` (TASER International) | High | SIG; OUI-SPY AXEOFF |
| `AXON` | BLE service UUIDs | `0xFC81`, `0xFE6B`, `0xFE6C` (Axon / TASER) | High | SIG; OUI-SPY AXEOFF |
| `AXON` | Advert payload | ASCII `BWCDEVICE`, either byte order | High | Field captures, five owner-confirmed (all-cameras-are-beacons) |
| `AXON` | BLE name | contains `BodyWorn` (Utility's body cam remote) | High | Field capture (nite-oui-collection) |
| `AXON` | WiFi OUI | `00:1D:96` WatchGuard Video, `00:23:BD` Digital Ally | Medium | IEEE |
| `RAVEN` | WiFi OUI | `D4:11:D6` ShotSpotter, Inc. | High | IEEE |
| `FLOCK` | SSID prefix | `Flock Camera` (a camera's own hotspot, "Flock Camera net.") | High | Flock-You issue #43 |
| `FLOCK` | BLE name | `FS-` and 4-12 hex digits | High | Field capture at Flock sites |
| `META` | WiFi OUI | `98:59:49` Luxottica Group | Medium | IEEE |
| `META` | BLE UUIDs / company ID | `0xFEB7`, `0xFEB8` (Meta), `0xFE45` (Snap), `0x060C` (Vuzix) | Medium | SIG |
| `GOOGLE_TAG` | Find Hub | service data `0xFEAA` with frame `0x40` or `0x41` | Medium | Google FMDN spec |
| `GOOGLE_TAG` | DULT | UUID `0xFCB2`, the cross-maker unwanted-tracker standard | Medium | SIG; AirGuard |
| `GOOGLE_TAG` | Chipolo | UUIDs `0xFE33`, `0xFE65`; company ID `0x08C3` | Medium | SIG |
| `TILE` | BLE UUID | `0xFD84` | High | SIG |
| `CAMERA` | WiFi OUI | Blink: `3C:A0:70`, `70:AD:43`, `74:13:48`, `74:AB:93`, `C8:19:D8`, `F0:74:C1` (Medium); i-PRO `D4:2D:C5` (High) | | IEEE |
| `DRONE` | WiFi OUI | DJI (10 blocks, Medium; 4 Baiwang blocks, Low), Skydio `38:1D:14` (High), Parrot (3 Medium, 2 Low) | | IEEE |

Service UUIDs now also match when they arrive as **service data** (AD `0x16`),
which is how most trackers advertise, not only in the UUID list.

**Fixed**

- The seventeen Espressif blocks filed as "Flock-ESP32" are gone. They named
  every ESP32 a Flock camera, every SquachWatch included; field-tested projects
  no longer trust them.
- `0xFEAA` on its own was any Eddystone shop beacon. Google trackers are now
  matched on Find Hub's own frame types instead.
- Skimmer prefix `98:D3:00` (held by nobody) is now `98:D3:31`, Shenzhen
  Bolutek, the HC-05 module's maker.
- Company ID `0x058E` (Meta Platforms Technologies) is gone: it is what Quest
  headsets send, and real Ray-Ban Meta glasses send `0x01AB` with `0xFD5F`.

**Considered and left out:** Samsung `0xFD59` / `0xFD69` (Galaxy phones send
them for their own offline finding), the circulating "Meta glasses" OUIs
`7C:2A:9E` / `CC:66:0A` / `F4:03:43` / `5C:E9:1E` (Apple, HPE and nobody),
WatchGuard Technologies' `00:01:21` / `00:90:7F` (a firewall maker), and
Axon's `D8:1F:65`, which field captures support but the registry lists as
"Private".
