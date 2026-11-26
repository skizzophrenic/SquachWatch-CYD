// SquachWatch-CYD — signature tables and lookup functions
// Sources per docs/DETECTIONS.md and docs/DESIGN.md §6.
#pragma once
#include "state.h"
#include <stddef.h>   // size_t, for pwnagotchiName()

// Every OUI carries its own confidence, because the registrant matters as
// much as the prefix. Roughly:
//
//   HIGH  the block is registered to the company that makes the product.
//   MED   registered to a parent whose range is much wider than the
//         product -- Amazon owns Ring, and also Echo, Fire TV and Kindle.
//   LOW   a module or ODM vendor whose parts are in everything (Espressif,
//         Liteon, Murata, Realtek, Telink), or a block that is not in the
//         IEEE registry at all.
//
// The LOW rows are not mistakes and are not being deleted: Flock really
// does build on ESP32, so the prefix really is evidence. It is just
// evidence shared with every dev board on earth, and saying so is the
// difference between a detector and a rumour.
struct OuiEntry   { uint8_t  b[3];     const char* name; DetectionType type; Confidence conf; };
struct UuidEntry  { uint16_t uuid;     const char* name; DetectionType type; };
struct NameEntry  { const char* name;  DetectionType type; };
struct SsidEntry  { const char* prefix; const char* name; DetectionType type; };
struct MfgIdEntry { uint16_t mfgId;    const char* name; DetectionType type; };

extern const OuiEntry    kOuiTable[];
extern const uint16_t    kOuiCount;
extern const UuidEntry   kUuidTable[];
extern const uint16_t    kUuidCount;
extern const NameEntry   kBtClassicNames[];
extern const uint16_t    kBtClassicCount;
extern const SsidEntry   kSsidPrefixes[];
extern const uint16_t    kSsidCount;
extern const MfgIdEntry  kMfgIdTable[];
extern const uint16_t    kMfgIdCount;

// First-match-wins lookups. Precedence per DESIGN.md §6.2.
// Yields the matched entry's own confidence through `conf` when given one.
// Callers that do not care keep the old one-argument form.
DetectionType lookupOui(const uint8_t* mac, Confidence* conf = nullptr);
DetectionType lookupUuid(uint16_t uuid16);
DetectionType lookupBtName(const char* name);
// Exactly ten digits and nothing else: a Penguin battery pack's serial.
bool isBareSerialName(const char* name);
// "FS-" plus a hex serial: a Flock accessory's BLE name.
bool isFlockFsName(const char* name);
// Axon's body-cam tag, "BWCDEVICE", in either byte order, anywhere in a raw
// advertisement.
bool isAxonBwcPayload(const uint8_t* payload, uint8_t len);
// Google Find Hub (FMDN): a service-data structure for 0xFEAA whose frame
// type is 0x40 or 0x41. Eddystone's own frames (0x00-0x30) are not.
bool isFmdnPayload(const uint8_t* payload, uint8_t len);
// The first 16-bit UUID carried as SERVICE DATA (AD 0x16) that is in
// kUuidTable, or 0. Trackers mostly announce themselves this way rather
// than in the UUID list.
uint16_t serviceDataUuid(const uint8_t* payload, uint8_t len);
DetectionType lookupSsid(const char* ssid);   // case-insensitive prefix

// Friendly vendor label for whichever kSsidPrefixes entry matched
// (e.g. "Axon-Body2"), or nullptr if none did. Same matching rule as
// lookupSsid — kept separate rather than changing that function's
// signature, since other callers just want the DetectionType.
const char* ssidVendorName(const char* ssid);
DetectionType lookupMfgId(uint16_t mfgId);
// The matched row's own label ("RayBanMeta", "Snap", "Skim-SPP"...), or
// nullptr. For the types whose one DetectionType covers several devices,
// this is what tells them apart -- and which MORE INFO page they get.
const char* uuidName(uint16_t uuid16);
const char* mfgIdName(uint16_t mfgId);

// AirTag check, run against the RAW advertisement bytes rather than
// NimBLE's parsed manufacturer-data field -- pass adv->getPayload() and
// adv->getPayloadLength(). Scanning the raw advert also catches tags
// whose Find My structure sits behind other AD structures, or arrives
// in a scan response, where the parsed field alone would miss it.
//
// See the implementation for exactly which byte patterns match and what
// that costs; the short version is that this deliberately trades some
// precision for actually catching a tag that has just been powered on.
bool isAirTagPayload(const uint8_t* payload, uint8_t len);
// A HomePod's "Hey Siri" block: Apple's Continuity type 0x08, seven bytes,
// whose device class is 0x0007. `mfg` starts at the company ID (4C 00).
bool isHomePodSiri(const uint8_t* mfg, uint8_t len);
// The Find My status byte (0 if the advert has none); see signatures.cpp.
uint8_t airTagStatusByte(const uint8_t* payload, uint8_t len);
// Which Find My frame the advert carries: 0x02 (the short "nearby" frame a
// tag sends while its owner's phone is close), 0x19 (the full "separated"
// frame with the public key, sent once apart), 0x07 (Proximity Pairing: a
// tag not set up with a phone yet), or 0 for none. The byte that follows
// the frame type goes to *status when asked.
uint8_t airTagFrame(const uint8_t* payload, uint8_t len, uint8_t* status);
// The Find Hub frame type: 0x40 while the tag is in its normal mode, 0x41
// once its owner's phone has put it in unwanted-tracking-protection mode
// (apart from the owner). 0 when the advert has no Find Hub frame.
uint8_t fmdnFrameType(const uint8_t* payload, uint8_t len);
// A cheap P2P camera's setup hotspot, named after its own UID:
// "ABCD-123456-ABCDE" (3-4 capitals, 6 digits, 5 capitals). The LookCam,
// HDWifiCamPro, JXLCAM and TinyCam Pro families all do this.
bool isP2pCamSsid(const char* ssid);

// iBeacon check, run against the PARSED manufacturer-data field (company
// ID included, i.e. what NimBLE's getManufacturerData() returns).
//
// Unlike the AirTag test above this one is exact rather than heuristic:
// Apple's format fixes every byte of the header and the total length, so
// there is nothing to trade away. See the implementation.
bool isIBeacon(const uint8_t* mfg, uint8_t len);

// Pwnagotchi check, run against a whole received 802.11 BEACON frame
// (header included -- pass the frame and its length straight from the
// promiscuous callback).
//
// Unlike everything else in this file it matches on a payload a device
// wrote about itself rather than on an identifier somebody assigned it,
// which is what makes it the strongest signature here: a pwnagotchi is
// broadcasting its name and its handshake count on purpose, to be found
// by other pwnagotchis.
//
// On a match, `out` receives the unit's name. Returns false and leaves
// `out` untouched otherwise. See the implementation for why this does not
// parse JSON and what it refuses to copy out.
bool pwnagotchiName(const uint8_t* frame, uint32_t len, char* out, size_t outSz);

// How sure we are that a match is really what it claims to be — mirrors
// the per-signature grading in docs/DETECTIONS.md, collapsed to one
// value per DetectionType. Where a type bundles signatures of differing
// documented confidence (e.g. AIRTAG covers both Apple's High-confidence
// AirTag match and Tile's Medium-confidence one, and the engine doesn't
// currently distinguish which matched), this reports the conservative
// (lower) grade rather than overstating certainty.
// Note: plain LOW/MEDIUM/HIGH collide with Arduino core's LOW/HIGH
// pin-state macros via textual substitution (enum class scoping
// doesn't protect against the preprocessor), hence the _CONF suffix.
// Confidence itself now lives in state.h -- see the note there.
Confidence  confidenceFor(DetectionType t);
const char* confidenceLabel(Confidence c);     // "HIGH CONF" / "MED CONF" / "LOW CONF"
uint8_t     confidencePercent(Confidence c);   // ~90 / ~60 / ~30 — an honest approximation, not a measured stat
