# Detection signatures — provenance and confidence

This file documents every signature SquachWatch-CYD matches, with
the source it came from, the confidence level, and any caveats.

Confidence:
- **High** — multiple independent sources, verified against the
  canonical research (Flock You, Marauder, Eye Spy).
- **Medium** — one or two sources, plausible but not cross-verified.
- **Low** — single source, anecdotal, may have false positives.

---

## Flock Safety — `FLOCK` — **High confidence**

**Why it works:** Flock Safety cameras have on-board WiFi modules
(typically ESP32) that periodically emit probe requests searching
for available networks. The probe requests use OUIs that fall in
well-known Espressif and other manufacturer ranges. The `addr1`
(receiver) trick catches "sleeper" cameras that aren't actively
transmitting — they still leak their MAC when the promiscuous
listener sends them a frame.

**Source:** [`colonelpanichacks/flock-you`](https://github.com/colonelpanichacks/flock-you)
(MIT) — the canonical Flock detector project. OUI research by
`@NitekryDPaul` and the DeFlockJoplin community. Our OUI table
contains 28 prefixes drawn from the flock-you repo and adjacent
public research.

**Confidence in v1.0:** **High** for the OUI match (we cover 28+
prefixes). **Medium** for the wildcard probe signature (we detect
the OUI on any management frame, not specifically the DeFlockJoplin
high-precision signature — that's a v1.1 improvement).

**Note:** Flock has reportedly turned off Bluetooth on newer
hardware, so BLE-name based detection is opportunistic.

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

## Meta Ray-Ban Smart Glasses — `META` — **High confidence**

**Why it works:** The Meta Ray-Ban smart glasses advertise a custom
BLE service with UUID `0xFD5F`.

**Source:** [Eye Spy project](https://simeononsecurity.com/articles/eye-spy-passive-surveillance-detector-esp32-2026/)
+ the [HN "glasses to detect smart-glasses" project](https://news.ycombinator.com/item?id=46075882).

**Confidence in v1.0:** **High** for the UUID match. Detection
works when the glasses are in pairing / first-power-on mode.

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

**Confidence in v1.0:** **Medium**. Compliance is rolling out so
detection is opportunistic. Note: we don't decode the actual drone
location payload, just flag the presence.

---

## Motorola / Vigilant ALPR — `ALPR` — **Medium confidence**

**Why it works:** Vigilant Solutions (now owned by Motorola
Solutions) uses OUI `00:0E:58` for some of its hardware.

**Source:** [Eye Spy](https://simeononsecurity.com/articles/eye-spy-passive-surveillance-detector-esp32-2026/).

**Confidence in v1.0:** **Medium**. Limited OUI coverage — only
the one prefix.

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
