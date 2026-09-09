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
the IEEE registry. Of 29 entries, exactly **one** is registered to Flock
Safety: `B4:1E:52`. The rest break down as sixteen Espressif blocks, nine
Liteon, one Shenzhen Intellirocks, one SPECTRA - TEK (labelled
"Flock-Sierra" for a long time, which is neither Sierra nor Flock), one
not in the registry at all, and one locally-administered address, which
by definition identifies no vendor.

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

Across all 73 OUI rows in the firmware that comes out at 31 High, 4
Medium, 38 Low.

**This is what makes ALERT FILTER work.** It has always been a minimum-
confidence filter, and until now confidence was constant per type, so it
had nothing to filter on. Set it to High and a passing ESP32 stays in the
log without taking over the screen.

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
implementations). BT Classic inquiry is **not** enabled in v1.0
(it conflicts with NimBLE on a single radio — documented as a
v1.1 task).

---

## Raven gunshot detector — `RAVEN` — **Medium confidence**

**Why it works:** Raven devices advertise custom service UUIDs in
the `0x3100`–`0x3500` range (proprietary, not in the Bluetooth
SIG assigned range).

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

## Tile trackers — `AIRTAG` (categorised here) — **Medium confidence**

**Why it works:** Tile devices advertise the proprietary service
UUID `0xFEED`.

**Source:** [Eye Spy](https://simeononsecurity.com/articles/eye-spy-passive-surveillance-detector-esp32-2026/).

**Confidence in v1.0:** **Medium**. Categorised as `AIRTAG` in v1.0
for simplicity; a `TILE` category is a v1.1 improvement.

---

## OpenDroneID drones — `DRONE` — **Medium confidence**

**Why it works:** ASTM F3411 Remote ID broadcasts over BLE using
the service UUID `0xFFFA`.

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

**What it cannot reach:** the Bluetooth *Legacy* form only. Bluetooth 5
Long Range is out of scope permanently on this board — the ESP32-WROOM
is BLE 4.2 and Espressif document no hardware support for Coded PHY or
extended advertising. The WiFi Beacon form, which packs several
messages into one frame, would need the promiscuous path rather than
the NimBLE one and is not implemented.

**Confidence:** **Medium**. Compliance is rolling out so detection is
opportunistic, and half the transport is unreachable per above.

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
Vigilant: `00:04:7D`, `00:18:85`, `00:1F:92`, `4C:CC:34`. Genetec,
whose AutoVu line is an LPR platform: `00:BF:15`, `0C:BF:15`.

**Source:** the IEEE registry itself, read per vendor rather than
copied from another detector — which is how the Sonos entry got in.
Vendor list cross-checked against
[FlipDeFlock](https://github.com/ReconGrunt/FlipDeFlock).

**Confidence:** **Medium**. The blocks are certain; what is not
certain is that a given device on one is a plate reader rather than
some other product from a large vendor. That is the same caveat
`CAMERA` carries, and it is why this is not High.

---

## Generic / covert IP cameras — `CAMERA` — **High confidence**

**Why it works:** Most consumer IP cameras use WiFi modules from a
small set of manufacturers. We match OUIs from Wyze, Ring, Arlo,
Blink, Reolink, Hikvision, Amazon, Realtek, and Tuya on the consumer
side, plus Verkada, Avigilon (Alta), and Axis Communications on the
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
registry OUI matches) as the generic `CAMERA` type.

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

### Flipper Zero — four signatures, three of them exact

| Signature | Value | Grade |
|---|---|---|
| BLE service UUID | `0x3081` / `0x3082` / `0x3083` — one per case colour | **High** |
| BLE company ID | `0x0E29`, Flipper Devices Inc. | **High** |
| MAC OUI | `0C:FA:22`, FLIPPER DEVICES INC | **High** |
| Advertised name | begins `Flipper ` | **Medium** — the owner can change it |

**Two constants the ecosystem gets wrong, and we do not.**

ESP32 Marauder's source comments Flipper's BLE company ID as `0x0FBA`.
`0x0FBA` is registered to **Cosonic Intelligent Technologies**, who make
headsets; Flipper Devices is `0x0E29` in the Bluetooth SIG list. Every
project that copied that constant inherited the error.

Wall-of-Flippers checks MAC prefixes `80:E1:26` and `80:E1:27`. Neither
appears anywhere in the IEEE registry — not under Flipper, not under
anybody. Only `0C:FA:22` is really theirs.

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
an M5Stack and a SquachWatch are the same chip. Sixteen Espressif prefixes
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
| flock-you (NitekryDPaul) | MIT (project) | Flock OUI list |
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

We use signature *data* (OUIs, UUIDs, names) as facts; we don't
copy GPL code into this project.
