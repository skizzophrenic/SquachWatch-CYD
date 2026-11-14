// SquachWatch-CYD — the receive profiles. See include/lora_profiles.h and
// docs/LORA.md section 4 for where each number comes from.
#include "lora_profiles.h"
#include "lora_crypto.h"
#include <stdio.h>

namespace Lora {

// name, freq, bw(x100Hz), sf, cr, sync, preamble, flags, implicitLen, hint, group
static const Profile PROFILES_EU[] = {
    // ---- Meshtastic and MeshCore on 869 MHz: what Germany actually runs ----
    // Every 250 kHz Meshtastic preset in EU_868 lands on 869.525; the SF is
    // what tells LongFast, MediumFast and ShortSlow apart.
    { "MT LongFast",    869525000, 2500, 11, 5, 0x2B, 16, PF_CRC,          0, Proto::MESHTASTIC, PG_MESH_EU },
    { "MT MediumFast",  869525000, 2500,  9, 5, 0x2B, 16, PF_CRC,          0, Proto::MESHTASTIC, PG_MESH_EU },
    { "MT ShortSlow",   869525000, 2500,  8, 5, 0x2B, 16, PF_CRC,          0, Proto::MESHTASTIC, PG_MESH_EU },
    // MeshCore's EU/UK Narrow: SF8 at 62.5 kHz, 32-symbol preamble since 1.16.
    { "MC EU Narrow",   869618000,  625,  8, 8, 0x12, 32, PF_CRC,          0, Proto::MESHCORE,   PG_MESH_EU },
    // The old MeshCore EU preset: the same PHY as LongFast, only the sync word differs.
    { "MC EU old",      869525000, 2500, 11, 5, 0x12, 16, PF_CRC,          0, Proto::MESHCORE,   PG_MESH_EU },
    { "MT NarrowSlow",  869442000,  625,  8, 6, 0x2B, 16, PF_CRC,          0, Proto::MESHTASTIC, PG_MESH_EU },
    { "MT LongSlow",    869462500, 1250, 12, 8, 0x2B, 16, PF_CRC,          0, Proto::MESHTASTIC, PG_MESH_EU },
    { "MT LongMod",     869587500, 1250, 11, 8, 0x2B, 16, PF_CRC,          0, Proto::MESHTASTIC, PG_MESH_EU },
    { "MT MediumSlow",  869525000, 2500, 10, 5, 0x2B, 16, PF_CRC,          0, Proto::MESHTASTIC, PG_MESH_EU },
    { "MT ShortFast",   869525000, 2500,  7, 5, 0x2B, 16, PF_CRC,          0, Proto::MESHTASTIC, PG_MESH_EU },
    { "MC CZ Narrow",   869432000,  625,  7, 5, 0x12, 32, PF_CRC,          0, Proto::MESHCORE,   PG_MESH_EU },

    // ---- LoRaWAN EU868 ------------------------------------------------------
    // Uplinks: sync 0x34, CRC on, normal IQ. One channel and one SF at a
    // time; the three mandatory channels first, the TTN five after.
    { "WAN 868.1 SF7",  868100000, 1250,  7, 5, 0x34,  8, PF_CRC,          0, Proto::LORAWAN,    PG_LORAWAN },
    { "WAN 868.1 SF12", 868100000, 1250, 12, 5, 0x34,  8, PF_CRC,          0, Proto::LORAWAN,    PG_LORAWAN },
    { "WAN 868.3 SF7",  868300000, 1250,  7, 5, 0x34,  8, PF_CRC,          0, Proto::LORAWAN,    PG_LORAWAN },
    { "WAN 868.5 SF7",  868500000, 1250,  7, 5, 0x34,  8, PF_CRC,          0, Proto::LORAWAN,    PG_LORAWAN },
    { "WAN 868.1 SF9",  868100000, 1250,  9, 5, 0x34,  8, PF_CRC,          0, Proto::LORAWAN,    PG_LORAWAN },
    { "WAN 868.3 SF12", 868300000, 1250, 12, 5, 0x34,  8, PF_CRC,          0, Proto::LORAWAN,    PG_LORAWAN },
    { "WAN 868.5 SF12", 868500000, 1250, 12, 5, 0x34,  8, PF_CRC,          0, Proto::LORAWAN,    PG_LORAWAN },
    { "WAN 867.1 SF7",  867100000, 1250,  7, 5, 0x34,  8, PF_CRC,          0, Proto::LORAWAN,    PG_LORAWAN },
    { "WAN 867.3 SF7",  867300000, 1250,  7, 5, 0x34,  8, PF_CRC,          0, Proto::LORAWAN,    PG_LORAWAN },
    { "WAN 867.5 SF7",  867500000, 1250,  7, 5, 0x34,  8, PF_CRC,          0, Proto::LORAWAN,    PG_LORAWAN },
    { "WAN 867.7 SF7",  867700000, 1250,  7, 5, 0x34,  8, PF_CRC,          0, Proto::LORAWAN,    PG_LORAWAN },
    { "WAN 867.9 SF7",  867900000, 1250,  7, 5, 0x34,  8, PF_CRC,          0, Proto::LORAWAN,    PG_LORAWAN },
    // RX2 downlinks: inverted IQ, no CRC, 10-symbol preamble. TTN uses SF9
    // here, the spec (and Helium) SF12.
    { "WAN RX2 SF9",    869525000, 1250,  9, 5, 0x34, 10, PF_INVERT,       0, Proto::LORAWAN,    PG_LORAWAN },
    { "WAN RX2 SF12",   869525000, 1250, 12, 5, 0x34, 10, PF_INVERT,       0, Proto::LORAWAN,    PG_LORAWAN },
    // The Class B beacon: implicit header, 17 bytes, no CRC, every 128 s.
    { "WAN Beacon",     869525000, 1250,  9, 5, 0x34, 10, PF_IMPLICIT,    17, Proto::LORAWAN,    PG_LORAWAN },
    { "WAN 868.3 SF7B", 868300000, 2500,  7, 5, 0x34,  8, PF_CRC,          0, Proto::LORAWAN,    PG_LORAWAN }, // DR6

    // ---- Aviation -------------------------------------------------------------
    { "FANET",          868200000, 2500,  7, 5, 0xF1,  8, PF_CRC,          0, Proto::FANET,      PG_AIR },

    // ---- 433 MHz: the amateur allocations and the 433 mesh presets ---------
    // Below the module's band. It hears strong signals here and nothing else,
    // and it must never transmit here; see docs/LORA.md section 1.
    { "APRS 433.775",   433775000, 1250, 12, 5, 0x12,  8, PF_CRC|PF_HAM|PF_433, 0, Proto::APRS,    PG_HAM_433 },
    { "APRS 433.900",   433900000, 1250, 12, 5, 0x12,  8, PF_CRC|PF_HAM|PF_433, 0, Proto::APRS,    PG_HAM_433 },
    { "MeshCom",        433175000, 2500, 11, 6, 0x2B, 32, PF_CRC|PF_HAM|PF_433, 0, Proto::MESHCOM, PG_HAM_433 },
    { "MT EU433 LF",    433875000, 2500, 11, 5, 0x2B, 16, PF_CRC|PF_433,   0, Proto::MESHTASTIC, PG_HAM_433 },
    { "MT 70cm Narrow", 433650000,  625,  8, 6, 0x2B, 16, PF_CRC|PF_HAM|PF_433, 0, Proto::MESHTASTIC, PG_HAM_433 },
    { "MC EU433 Nrw",   433650000,  625,  8, 8, 0x12, 32, PF_CRC|PF_433,   0, Proto::MESHCORE,   PG_HAM_433 },
    { "MC EU433 LR",    433650000, 2500, 11, 5, 0x12, 16, PF_CRC|PF_433,   0, Proto::MESHCORE,   PG_HAM_433 },
    { "MT 434.100",     434100000, 1250, 11, 8, 0x2B, 16, PF_CRC|PF_HAM|PF_433, 0, Proto::MESHTASTIC, PG_HAM_433 }, // DARC band plan

    // ---- Other ---------------------------------------------------------------
    { "RadioLib dflt",  434000000, 1250,  9, 7, 0x12,  8, PF_CRC|PF_433,   0, Proto::UNKNOWN,    PG_OTHER },
    { "Heltec demo",    868000000, 1250,  7, 5, 0x12,  8, PF_CRC,          0, Proto::UNKNOWN,    PG_OTHER },
    { "LoRaMesher",     869900000, 1250,  7, 5, 0x14,  8, PF_CRC,          0, Proto::UNKNOWN,    PG_OTHER },
    { "RNode 867.2",    867200000, 1250,  8, 5, 0x12, 18, PF_CRC,          0, Proto::RETICULUM,  PG_OTHER },
    { "MT LiteFast",    866300000, 1250,  9, 5, 0x2B, 16, PF_CRC,          0, Proto::MESHTASTIC, PG_OTHER },
    { "TS011 WOR",      865100000, 1250,  9, 5, 0x34,  8, PF_CRC|PF_INVERT, 0, Proto::LORAWAN,   PG_OTHER },
};

// The Americas: the 902-928 MHz ISM band. Meshtastic's US presets sit where
// its slot rule puts each preset's own name (meshtasticSlotHz(), checked
// against LongFast's well-known 906.875); MeshCore's US/Canada preset;
// LoRaWAN US915 sub-band 2, the one TTN and Helium use, uplinks and
// downlinks. The 433 rows are the same amateur allocations as the EU table.
static const Profile PROFILES_US[] = {
    { "MT LongFast",    906875000, 2500, 11, 5, 0x2B, 16, PF_CRC,          0, Proto::MESHTASTIC, PG_MESH_US },
    { "MC US",          910525000,  625,  7, 5, 0x12, 32, PF_CRC,          0, Proto::MESHCORE,   PG_MESH_US },
    { "MT MediumFast",  913125000, 2500,  9, 5, 0x2B, 16, PF_CRC,          0, Proto::MESHTASTIC, PG_MESH_US },
    { "MT MediumSlow",  914875000, 2500, 10, 5, 0x2B, 16, PF_CRC,          0, Proto::MESHTASTIC, PG_MESH_US },
    { "MT ShortFast",   918875000, 2500,  7, 5, 0x2B, 16, PF_CRC,          0, Proto::MESHTASTIC, PG_MESH_US },
    { "MT ShortSlow",   920625000, 2500,  8, 5, 0x2B, 16, PF_CRC,          0, Proto::MESHTASTIC, PG_MESH_US },
    { "MT LongSlow",    905312500, 1250, 12, 8, 0x2B, 16, PF_CRC,          0, Proto::MESHTASTIC, PG_MESH_US },
    { "MT LongMod",     912812500, 1250, 11, 8, 0x2B, 16, PF_CRC,          0, Proto::MESHTASTIC, PG_MESH_US },

    // ---- LoRaWAN US915, sub-band 2 ---------------------------------------------
    { "WAN 903.9 SF7",  903900000, 1250,  7, 5, 0x34,  8, PF_CRC,          0, Proto::LORAWAN,    PG_LORAWAN },
    { "WAN 903.9 SF10", 903900000, 1250, 10, 5, 0x34,  8, PF_CRC,          0, Proto::LORAWAN,    PG_LORAWAN },
    { "WAN 904.1 SF7",  904100000, 1250,  7, 5, 0x34,  8, PF_CRC,          0, Proto::LORAWAN,    PG_LORAWAN },
    { "WAN 904.3 SF7",  904300000, 1250,  7, 5, 0x34,  8, PF_CRC,          0, Proto::LORAWAN,    PG_LORAWAN },
    { "WAN 904.5 SF7",  904500000, 1250,  7, 5, 0x34,  8, PF_CRC,          0, Proto::LORAWAN,    PG_LORAWAN },
    { "WAN 904.7 SF7",  904700000, 1250,  7, 5, 0x34,  8, PF_CRC,          0, Proto::LORAWAN,    PG_LORAWAN },
    { "WAN 904.9 SF7",  904900000, 1250,  7, 5, 0x34,  8, PF_CRC,          0, Proto::LORAWAN,    PG_LORAWAN },
    { "WAN 905.1 SF7",  905100000, 1250,  7, 5, 0x34,  8, PF_CRC,          0, Proto::LORAWAN,    PG_LORAWAN },
    { "WAN 905.3 SF7",  905300000, 1250,  7, 5, 0x34,  8, PF_CRC,          0, Proto::LORAWAN,    PG_LORAWAN },
    { "WAN 904.6 SF8W", 904600000, 5000,  8, 5, 0x34,  8, PF_CRC,          0, Proto::LORAWAN,    PG_LORAWAN }, // DR4, 500 kHz
    // Downlinks: 500 kHz, inverted IQ, no CRC. RX2 is 923.3 at SF12.
    { "WAN RX2 SF12",   923300000, 5000, 12, 5, 0x34,  8, PF_INVERT,       0, Proto::LORAWAN,    PG_LORAWAN },
    { "WAN DN 923.9",   923900000, 5000,  9, 5, 0x34,  8, PF_INVERT,       0, Proto::LORAWAN,    PG_LORAWAN },

    // ---- 433 MHz: the amateur allocations, as in the EU table -------------------
    { "APRS 433.775",   433775000, 1250, 12, 5, 0x12,  8, PF_CRC|PF_HAM|PF_433, 0, Proto::APRS,    PG_HAM_433 },
    { "MT 70cm Narrow", 433650000,  625,  8, 6, 0x2B, 16, PF_CRC|PF_HAM|PF_433, 0, Proto::MESHTASTIC, PG_HAM_433 },
};

static const Profile* s_tab = PROFILES_EU;
static uint8_t        s_n   = (uint8_t)(sizeof(PROFILES_EU) / sizeof(PROFILES_EU[0]));
static uint8_t        s_region = REGION_EU;

void setRegion(uint8_t r) {
    s_region = (r == REGION_US) ? REGION_US : REGION_EU;
    if (s_region == REGION_US) { s_tab = PROFILES_US; s_n = (uint8_t)(sizeof(PROFILES_US) / sizeof(PROFILES_US[0])); }
    else                       { s_tab = PROFILES_EU; s_n = (uint8_t)(sizeof(PROFILES_EU) / sizeof(PROFILES_EU[0])); }
}
uint8_t     region()     { return s_region; }
const char* regionName() { return s_region == REGION_US ? "US 915" : "EU 868"; }

const Profile* profiles()     { return s_tab; }
uint8_t        profileCount() { return s_n; }
const Profile& profile(uint8_t i) { return s_tab[i < s_n ? i : 0]; }
uint8_t        defaultProfile() { return 0; }

int16_t meshPresetFind(Proto p, const char* preset) {
    int16_t first = -1;
    for (uint8_t i = 0; i < s_n; i++) {
        const Profile& r = s_tab[i];
        if (r.hint != p || (r.group != PG_MESH_EU && r.group != PG_MESH_US)) continue;
        if (first < 0) first = i;
        if (!preset || !*preset) return first;
        // "MT LongFast" answers to "LongFast"; the prefix is the table's.
        const char* n = r.name;
        if (n[0] && n[1] && n[2] == ' ') n += 3;
        bool same = true;
        for (size_t k = 0; ; k++) {
            char a = n[k], b = preset[k];
            if (a >= 'a' && a <= 'z') a = (char)(a - 32);
            if (b >= 'a' && b <= 'z') b = (char)(b - 32);
            if (a != b) { same = false; break; }
            if (!a) break;
        }
        if (same) return i;
    }
    return first;
}

uint8_t meshPresetRows(Proto p, uint8_t* out, uint8_t cap) {
    uint8_t n = 0;
    for (uint8_t i = 0; i < s_n && n < cap; i++) {
        const Profile& r = s_tab[i];
        if (r.hint == p && (r.group == PG_MESH_EU || r.group == PG_MESH_US)) out[n++] = i;
    }
    return n;
}

uint32_t meshtasticSlotHz(uint32_t startHz, uint32_t endHz, uint16_t bwKhz10,
                          uint32_t spacingHz, uint32_t paddingHz, const char* name) {
    const uint32_t bwHz = (uint32_t)bwKhz10 * 100u;
    const uint32_t slotWidth = spacingHz + 2 * paddingHz + bwHz;
    if (!slotWidth) return startHz;
    // round((end - start + spacing) / slotWidth)
    const uint32_t span = endHz - startHz + spacingHz;
    uint32_t numSlots = (span + slotWidth / 2) / slotWidth;
    if (!numSlots) numSlots = 1;
    const uint32_t slot = LoraCrypto::djb2(name) % numSlots;
    return startHz + bwHz / 2 + paddingHz + slot * slotWidth;
}

uint16_t dutyLimitPermille(uint32_t freqHz) {
    // BNetzA Vfg. 91/2025, the sub-band table copied out of docs/LORA.md
    // section 8 (which carries the URL). Ascending, first match wins, both
    // edges inclusive -- so at a shared edge (865.0, 868.0) the earlier and
    // stricter row is the one that answers, which is the right way round for a
    // ceiling. The unlisted slivers between rows -- 868.6-868.7, 869.2-869.4,
    // 869.65-869.7 -- and anything off the band get 0: no figure rather than a
    // guessed one, and a node heard there is not flagged at all.
    struct Band { uint32_t lo, hi; uint16_t permille; };
    static const Band BANDS[] = {
        // 433.05-434.79: 1 mW with no duty limit, or 10 mW at 10 %. The 10 %
        // row is the one a transmitter heard here is most likely under. An
        // amateur station on 433.775 / 433.175 is not under this general
        // licence at all and has no duty limit, so a flag raised here says
        // "over the SRD ceiling for this band", never "unlawful".
        { 433050000u, 434790000u, 100 },
        {  863000000u, 865000000u,   1 },   // 25 mW, 0.1 %
        {  865000000u, 868000000u,  10 },   // 25 mW, 1 %. The four 200 kHz
        // network channels inside 865.6-867.6 are allowed 2.5 % (10 % for a
        // network access point), but docs/LORA.md does not give their centre
        // frequencies, so whether WAN 867.1/867.3/867.5 are those channels is
        // unverified and the 1 % row stands for them. The stricter reading is
        // the one to be wrong in.
        {  868000000u, 868600000u,  10 },   // 25 mW, 1 %
        {  868700000u, 869200000u,   1 },   // 25 mW, 0.1 %
        {  869400000u, 869650000u, 100 },   // 500 mW, 10 % -- the mesh band
        {  869700000u, 870000000u,  10 },   // 5 mW at no limit, or 25 mW at 1 %
    };
    for (size_t i = 0; i < sizeof BANDS / sizeof BANDS[0]; i++)
        if (freqHz >= BANDS[i].lo && freqHz <= BANDS[i].hi) return BANDS[i].permille;
    return 0;
}

void formatMHz(uint32_t hz, char* out, size_t cap) {
    const uint32_t khz = (hz + 500) / 1000;
    snprintf(out, cap, "%lu.%03lu", (unsigned long)(khz / 1000), (unsigned long)(khz % 1000));
}

void formatBw(uint16_t bwKhz10, char* out, size_t cap) {
    if (bwKhz10 % 10 == 0) snprintf(out, cap, "%uk", (unsigned)(bwKhz10 / 10));
    else                   snprintf(out, cap, "%u.%uk", (unsigned)(bwKhz10 / 10), (unsigned)(bwKhz10 % 10));
}

}
