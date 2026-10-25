// SquachWatch-CYD — the receive profiles: where each LoRa network lives.
//
// One radio, one profile at a time. The table is the listening plan in
// docs/LORA.md section 4, most valuable first, so "the first N" is always a
// sensible survey. Standalone, for the host tests.
#pragma once
#include "lora_pkt.h"

namespace Lora {

// Which table: EU 868 (with its 433 rows) or US 915. Chosen once, before the
// sniffer starts (Lora::begin), because profile indices are only stable
// within one table. EU until told otherwise, which is what the host tests get.
enum : uint8_t { REGION_EU = 0, REGION_US = 1 };
void        setRegion(uint8_t r);
uint8_t     region();
const char* regionName();   // "EU 868", "US 915"

// The Meshtastic override: a region, modem preset and frequency slot of the
// user's choosing, applied as one extra row at the END of the table (and
// returned by meshPresetFind for Meshtastic) whenever it differs from the band
// plan's own rows. `region` is 0 for "follow the band plan", else 1 + an index
// into the region list; `preset` a name from the preset list ("" = LongFast);
// `slot` 0 for Meshtastic's default (a hash of the preset's name), else 1..N.
// A preset too wide for the region falls back to the first that fits, as the
// firmware refuses it. Rebuilds the table, so call it before the sniffer
// starts or follow it with Lora::applyListen().
void        setMeshtasticOverride(uint8_t region, const char* preset, uint8_t slot);
bool        meshtasticOverrideActive();
uint8_t     mtRegionCount();                 // the lists the settings page steps through
const char* mtRegionName(uint8_t i);         // 0-based; override value is i + 1
uint8_t     mtPresetCount();
const char* mtPresetName(uint8_t i);
bool        mtPresetFits(uint8_t region, uint8_t preset);          // region as setMeshtasticOverride's
uint16_t    mtSlotCount(uint8_t region, const char* preset);       // slots in that region at that preset
uint32_t    mtSlotHz(uint8_t region, const char* preset, uint8_t slot);   // slot 0 = the default

// The table and its length. Indices are stable within a build and are what
// a Packet's `profile` field holds, so nothing persists them.
const Profile* profiles();
uint8_t        profileCount();
const Profile& profile(uint8_t i);   // clamped

// The index the survey starts on, and what FOCUS mode falls back to: the
// German Meshtastic default.
uint8_t defaultProfile();

// A network's mesh preset by name in this band plan: the row "MT <preset>"
// for Meshtastic (case aside), or the network's first mesh row -- LongFast,
// MeshCore's regional preset -- when `preset` is empty or this plan has no
// such row. -1 when the plan has no mesh row for the network at all.
int16_t meshPresetFind(Proto p, const char* preset);
// Every mesh row a network has in this band plan, table order (the first is
// the default). Returns how many went into out.
uint8_t meshPresetRows(Proto p, uint8_t* out, uint8_t cap);

// Meshtastic's frequency-slot rule (RadioInterface.cpp), for a channel with a
// name that is not a preset's: the slot is a djb2 hash of the name. Region
// edges in Hz, bandwidth in units of 100 Hz. Returns the centre frequency.
uint32_t meshtasticSlotHz(uint32_t startHz, uint32_t endHz, uint16_t bwKhz10,
                          uint32_t spacingHz, uint32_t paddingHz, const char* name);

// The duty-cycle ceiling that applies on a frequency, in per-mille of the
// hour, or 0 when the tables below have no figure for it.
//
// Source: BNetzA Vfg. 91/2025 (November 2025, replacing 133/2019), the SRD
// general licence for 868 MHz, tabulated in docs/LORA.md section 8 and
// sourced at docs/LORA.md's Sources list:
// https://www.bundesnetzagentur.de/DE/Fachthemen/Telekommunikation/Frequenzen/Allgemeinzuteilungen/_DL/vfg91_2025.pdf
//
// This is what an SRD transmitter on that frequency is allowed, which is the
// yardstick a duty figure heard off the air has to be read against. It is one
// number per sub-band and nothing else: the 500 mW / 10 % row that the mesh
// band sits in is a hundred times the 0.1 % that 868.7-869.2 MHz allows, so a
// single threshold for the whole table can only be right in one place.
uint16_t dutyLimitPermille(uint32_t freqHz);

// A short label for a frequency, "869.525", into a 10-byte buffer.
void formatMHz(uint32_t hz, char* out, size_t cap);
// "250k", "62.5k", "7.8k"
void formatBw(uint16_t bwKhz10, char* out, size_t cap);

}
