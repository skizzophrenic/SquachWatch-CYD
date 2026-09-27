// Time on air and the duty ceilings -- src/lora_pkt.cpp and src/lora_profiles.cpp.
//
// Both are pure arithmetic over numbers in a table, which is exactly the kind
// of thing that drifts: a preamble edited in the profile table or a band edge
// mistyped produces a plausible wrong answer and nothing on the device would
// say so. Airtime feeds the duty figure and the duty figure has a legal
// meaning, so these are the two numbers in the LoRa code worth pinning hardest.
//
// The reference values are docs/LORA.md's own table (section 9, "Time on air,
// from Semtech's formula"), which was computed independently of this code:
//
//   Meshtastic LongFast, 50-byte payload   0.72 s
//   MeshCore Narrow, 60 bytes              0.71 s
//   LoRaWAN SF12, 23 bytes                 1.48 s
//   LoRaWAN SF7, 23 bytes                  62 ms
//   LoRa APRS, 80 bytes                    3.3 s
//   MeshCom, 80 bytes                      1.1 s
//
// The whole table only comes out of the formula when each frame is given its
// OWN network's preamble -- MeshCore's 32 symbols, MeshCom's 32, Meshtastic's
// 16 -- which is the check that caught src/lora_radio.cpp passing a flat 8 for
// every network. The two "at 8 symbols" rows below are that bug's magnitude,
// kept as a test so nobody reintroduces the flat value as a simplification.
//
// The duty ceilings are BNetzA Vfg. 91/2025, tabulated in docs/LORA.md
// section 8. One number for the whole band cannot be right: the mesh band's
// 10 % is a hundred times what 868.7-869.2 MHz allows.
#include "lora_pkt.h"
#include "lora_profiles.h"
#include "test_util.h"
#include <cstring>
#include <cstdio>

using namespace Lora;

// The profile of that name, so the test breaks on a renamed row rather than
// silently checking the wrong one.
static const Profile& byName(const char* name) {
    for (uint8_t i = 0; i < profileCount(); i++)
        if (strcmp(profile(i).name, name) == 0) return profile(i);
    printf("  !! no profile named \"%s\"\n", name);
    g_fails++;
    return profile(0);
}

// Time on air of a frame `len` bytes long heard on that profile, the way
// LoraRadio::readPacket computes it: the profile's CR and preamble, its CRC and
// implicit-header flags, its LDRO setting.
static uint32_t toaOn(const Profile& p, uint8_t len) {
    return timeOnAirUs(p.sf, p.bwKhz10, p.cr, p.preamble, len,
                       (p.flags & PF_CRC) != 0, (p.flags & PF_IMPLICIT) != 0,
                       (p.flags & PF_LDRO) != 0);
}

int main() {
    suite("The preambles the reference table depends on");
    {
        // Pinned because the airtime rows below are only right with these.
        ck("MeshCore EU Narrow sends 32 symbols", byName("MC EU Narrow").preamble == 32);
        ck("Meshtastic sends 16", byName("MT LongFast").preamble == 16);
        ck("MeshCom sends 32", byName("MeshCom").preamble == 32);
        ck("LoRaWAN uplinks send 8", byName("WAN 868.1 SF7").preamble == 8);
        ck("LoRa APRS sends 8", byName("APRS 433.775").preamble == 8);
    }

    suite("Time on air against docs/LORA.md's table");
    {
        // "50-byte payload" in the doc is the Data payload; the 16-byte
        // Meshtastic header goes on the air with it, so the frame is 66 bytes.
        const uint32_t mt = toaOn(byName("MT LongFast"), 66);
        ck("LongFast, 50-byte payload: 723 ms vs the doc's 0.72 s", mt == 722944u);
        const uint32_t mc = toaOn(byName("MC EU Narrow"), 60);
        ck("MeshCore Narrow, 60 bytes: 706 ms vs 0.71 s", mc == 705536u);
        const uint32_t w12 = toaOn(byName("WAN 868.1 SF12"), 23);
        ck("LoRaWAN SF12, 23 bytes: 1483 ms vs 1.48 s", w12 == 1482752u);
        const uint32_t w7 = toaOn(byName("WAN 868.1 SF7"), 23);
        ck("LoRaWAN SF7, 23 bytes: 61.7 ms vs 62 ms", w7 == 61696u);
        const uint32_t ap = toaOn(byName("APRS 433.775"), 80);
        ck("LoRa APRS, 80 bytes: 3285 ms vs 3.3 s", ap == 3284992u);
        const uint32_t mm = toaOn(byName("MeshCom"), 80);
        ck("MeshCom, 80 bytes: 1100 ms vs 1.1 s", mm == 1099776u);
    }

    suite("A flat 8-symbol preamble undercounts, and by how much");
    {
        const Profile& mcp = byName("MC EU Narrow");
        const uint32_t at8  = timeOnAirUs(mcp.sf, mcp.bwKhz10, mcp.cr, 8, 60, true, false, false);
        const uint32_t at32 = toaOn(mcp, 60);
        ck("MeshCore Narrow at 8 symbols is 607 ms", at8 == 607232u);
        // (705536 - 607232) / 705536 = 13.93 %
        ck("13.9 % low, and the doc's 0.71 s is unreachable that way",
           (at32 - at8) * 1000ull / at32 == 139);

        const Profile& mtp = byName("MT LongFast");
        const uint32_t mt8  = timeOnAirUs(mtp.sf, mtp.bwKhz10, mtp.cr, 8, 66, true, false, false);
        const uint32_t mt16 = toaOn(mtp, 66);
        ck("LongFast at 8 symbols is 657 ms", mt8 == 657408u);
        // (722944 - 657408) / 722944 = 9.06 %
        ck("9.1 % low", (mt16 - mt8) * 1000ull / mt16 == 90);

        // MeshCom is the row that makes the point hardest: the doc's 1.1 s is
        // only reachable with the 32 symbols the preset sends.
        const Profile& mmp = byName("MeshCom");
        ck("MeshCom at 8 symbols is 903 ms, not the doc's 1.1 s",
           timeOnAirUs(mmp.sf, mmp.bwKhz10, mmp.cr, 8, 80, true, false, false) == 903168u);
    }

    suite("The formula's own edges");
    {
        // LDRO switches itself on when a symbol lasts 16.38 ms or more, which
        // at 125 kHz is SF11 and up.
        ck("SF12 at 125 kHz is an LDRO symbol", autoLdro(12, 1250) && symbolUs(12, 1250) == 32768u);
        ck("SF7 at 125 kHz is not", !autoLdro(7, 1250));
        ck("62.5 kHz SF8 symbol is 4096 us", symbolUs(8, 625) == 4096u);
        ck("an impossible SF is zero, not a wild number", timeOnAirUs(4, 1250, 5, 8, 20, true, false, false) == 0);
        ck("a zero bandwidth is zero", timeOnAirUs(9, 0, 5, 8, 20, true, false, false) == 0);
        // The Class B beacon: implicit header, 17 bytes, no CRC.
        const Profile& bc = byName("WAN Beacon");
        ck("the beacon is implicit and 17 bytes", (bc.flags & PF_IMPLICIT) && bc.implicitLen == 17);
        ck("and its airtime comes out 152.6 ms", toaOn(bc, bc.implicitLen) == 152576u);
    }

    suite("Duty ceilings per sub-band, BNetzA Vfg. 91/2025");
    {
        ck("869.4-869.65 is 10 %: the mesh band", dutyLimitPermille(869525000u) == 100);
        ck("MeshCore Narrow on 869.618 is in it", dutyLimitPermille(byName("MC EU Narrow").freqHz) == 100);
        ck("868.0-868.6 is 1 %", dutyLimitPermille(868100000u) == 10);
        ck("FANET on 868.2 is 1 %", dutyLimitPermille(byName("FANET").freqHz) == 10);
        ck("868.7-869.2 is 0.1 %: a hundred times stricter", dutyLimitPermille(868900000u) == 1);
        ck("863-865 is 0.1 %", dutyLimitPermille(864000000u) == 1);
        ck("865-868 is 1 %", dutyLimitPermille(866300000u) == 10);
        ck("869.7-870 is 1 % for the 25 mW option", dutyLimitPermille(869900000u) == 10);
        ck("433.05-434.79 is 10 % for the 10 mW option", dutyLimitPermille(433775000u) == 100);
        // A shared edge answers with the stricter of the two rows.
        ck("865.0 exactly takes the 0.1 % row", dutyLimitPermille(865000000u) == 1);
        // The slivers the licence does not tabulate, and off the band
        // altogether: no figure, so nothing to flag a node against.
        ck("868.65 is in no row: no figure", dutyLimitPermille(868650000u) == 0);
        ck("869.3 is in no row", dutyLimitPermille(869300000u) == 0);
        ck("2.4 GHz is not in this table at all", dutyLimitPermille(2450000000u) == 0);
        ck("nor is 0", dutyLimitPermille(0) == 0);
    }

    suite("Every profile in the table has a ceiling to be judged against");
    {
        uint8_t missing = 0;
        for (uint8_t i = 0; i < profileCount(); i++)
            if (!dutyLimitPermille(profile(i).freqHz)) {
                printf("  !! %-14s %lu Hz falls in no tabulated sub-band\n",
                       profile(i).name, (unsigned long)profile(i).freqHz);
                missing++;
            }
        // A profile in an untabulated sliver would silently disable NF_DUTY for
        // every node heard on it, which is the failure this catches.
        ck("no profile sits in a gap", missing == 0);
        // The survey mask is a uint64 and Stats::byProfile is 64 wide, and both
        // are indexed with `i < 64` guards that drop the rest in silence. A
        // table that grew past 64 would stop counting without a word.
        printf("  (%u profiles in the table)\n", (unsigned)profileCount());
        ck("the table still fits the 64-bit survey mask", profileCount() <= 64);
    }

    return report();
}
