// The name this board goes by on LoRa -- src/lora_myname.cpp -- on its own:
// which name goes on the air, the Meshtastic short name rule (SQWT while he is
// still SQUACHY), and what a typed name is allowed to keep.
#include "lora_myname.h"
#include "lora_profiles.h"
#include "test_util.h"
#include <cstring>
#include <cstdio>
#include <strings.h>

using namespace Lora;

int main() {
    char s[8];

    suite("The long name: typed, else the nickname, else SQUACHY");
    {
        ck("nothing typed, default nickname", !strcmp(MyName::longName("", "SQUACHY"), "SQUACHY"));
        ck("nothing typed, a named Squachy", !strcmp(MyName::longName("", "BIGFOOT"), "BIGFOOT"));
        ck("a typed name wins", !strcmp(MyName::longName("RIDGE RUNNER", "BIGFOOT"), "RIDGE RUNNER"));
        ck("spaces only count as nothing typed", !strcmp(MyName::longName("   ", "BIGFOOT"), "BIGFOOT"));
        ck("null everywhere is SQUACHY", !strcmp(MyName::longName(nullptr, nullptr), "SQUACHY"));
        ck("an empty nickname is SQUACHY", !strcmp(MyName::longName("", ""), "SQUACHY"));
    }

    suite("The short name: the rule announceIfDue always used");
    {
        MyName::shortName("", "SQUACHY", s, sizeof s);
        ck("SQUACHY goes as SQWT", !strcmp(s, "SQWT"));
        MyName::shortName(nullptr, "BIGFOOT", s, sizeof s);
        ck("any other name: its first four", !strcmp(s, "BIGF"));
        MyName::shortName("", "YO", s, sizeof s);
        ck("a short name is kept whole", !strcmp(s, "YO"));
        MyName::shortName("", "ABC DEF", s, sizeof s);
        ck("a cut that ends on a space drops it", !strcmp(s, "ABC"));
        MyName::shortName("YETI", "SQUACHY", s, sizeof s);
        ck("a typed short name wins over SQWT", !strcmp(s, "YETI"));
        MyName::shortName("SASQUATCH", "BIGFOOT", s, sizeof s);
        ck("a typed short name is cut to four", !strcmp(s, "SASQ"));
        MyName::shortName("  ", "BIGFOOT", s, sizeof s);
        ck("spaces only is nothing typed", !strcmp(s, "BIGF"));
        MyName::shortName("", nullptr, s, sizeof s);
        ck("no long name at all is SQWT", !strcmp(s, "SQWT"));
        char tiny[3];
        MyName::shortName("", "BIGFOOT", tiny, sizeof tiny);
        ck("a small buffer is never overrun", !strcmp(tiny, "BI"));
    }

    suite("What a typed name keeps");
    {
        char n[40];
        strcpy(n, "  Ridge Runner  ");
        ck("spaces trimmed both ends", MyName::clean(n, MyName::NAME_LEN) == 12 && !strcmp(n, "Ridge Runner"));
        strcpy(n, "YETI: HQ");
        ck("no colon (MeshCore splits on it)", MyName::clean(n, MyName::NAME_LEN) == 7 && !strcmp(n, "YETI HQ"));
        strcpy(n, "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123");
        ck("cut to twenty-four", MyName::clean(n, MyName::NAME_LEN) == 24 && strlen(n) == 24);
        strcpy(n, "   ");
        ck("spaces only is empty (the default)", MyName::clean(n, MyName::NAME_LEN) == 0 && n[0] == '\0');
        strcpy(n, "A\tB\x7f" "C");
        ck("control characters dropped", MyName::clean(n, MyName::NAME_LEN) == 3 && !strcmp(n, "ABC"));
        strcpy(n, "SASQUATCH");
        ck("a short name is cut to four", MyName::clean(n, MyName::SHORT_LEN) == 4 && !strcmp(n, "SASQ"));
    }

    suite("MESHTASTIC PRESET: found by name in either band plan");
    {
        for (uint8_t r = 0; r < 3; r++) {
            setRegion(r == 0 ? REGION_EU : r == 1 ? REGION_US : REGION_AU);
            const int16_t lf = meshPresetFind(Proto::MESHTASTIC, "");
            ck("nothing chosen is LongFast", lf >= 0 && !strcmp(profile((uint8_t)lf).name, "MT LongFast"));
            ck("null is LongFast too", meshPresetFind(Proto::MESHTASTIC, nullptr) == lf);
            const int16_t mf = meshPresetFind(Proto::MESHTASTIC, "MediumFast");
            ck("MediumFast is its own row", mf >= 0 && !strcmp(profile((uint8_t)mf).name, "MT MediumFast"));
            ck("case does not matter", meshPresetFind(Proto::MESHTASTIC, "mediumfast") == mf);
            ck("a preset this plan lacks falls back to LongFast", meshPresetFind(Proto::MESHTASTIC, "NoSuchPreset") == lf);
            ck("Medium is not MediumFast", meshPresetFind(Proto::MESHTASTIC, "Medium") == lf);
            const int16_t mc = meshPresetFind(Proto::MESHCORE, nullptr);
            ck("MeshCore keeps its regional row", mc >= 0 && profile((uint8_t)mc).hint == Proto::MESHCORE);
            uint8_t rows[32];
            const uint8_t n = meshPresetRows(Proto::MESHTASTIC, rows, 32);
            bool allMt = n >= 7;
            for (uint8_t i = 0; i < n; i++) allMt = allMt && profile(rows[i]).hint == Proto::MESHTASTIC;
            ck("the list is every Meshtastic preset, LongFast first", allMt && rows[0] == (uint8_t)lf);
        }
        setRegion(REGION_EU);
        const int16_t nf = meshPresetFind(Proto::MESHTASTIC, "NarrowFast");
        ck("EU NarrowFast is 869.442 MHz SF7 BW62.5 CR4/6",
           nf >= 0 && !strcmp(profile((uint8_t)nf).name, "MT NarrowFast") && profile((uint8_t)nf).freqHz == 869442000u &&
           profile((uint8_t)nf).sf == 7 && profile((uint8_t)nf).bwKhz10 == 625 && profile((uint8_t)nf).cr == 6);
        const int16_t ns = meshPresetFind(Proto::MESHTASTIC, "NarrowSlow");
        ck("NarrowSlow is still its own row", ns >= 0 && ns != nf && profile((uint8_t)ns).sf == 8);
        setRegion(REGION_US);
        const int16_t ms = meshPresetFind(Proto::MESHTASTIC, "MediumSlow");
        ck("US MediumSlow is 914.875 MHz SF10", ms >= 0 && profile((uint8_t)ms).freqHz == 914875000u && profile((uint8_t)ms).sf == 10);
        setRegion(REGION_AU);
        ck("AU is its own plan", region() == REGION_AU && !strcmp(regionName(), "AU 915"));
        // Every Meshtastic row where the slot rule puts it in ANZ's 915-928.
        bool slots = true;
        uint8_t rows[32];
        const uint8_t n = meshPresetRows(Proto::MESHTASTIC, rows, 32);
        for (uint8_t i = 0; i < n; i++) {
            const Profile& p = profile(rows[i]);
            static const struct { const char* row; const char* name; } NM[] = {
                { "MT LongFast", "LongFast" }, { "MT MediumFast", "MediumFast" }, { "MT MediumSlow", "MediumSlow" },
                { "MT ShortFast", "ShortFast" }, { "MT ShortSlow", "ShortSlow" }, { "MT LongSlow", "LongSlow" },
                { "MT LongMod", "LongModerate" } };
            for (const auto& m : NM)
                if (!strcmp(p.name, m.row))
                    slots = slots && p.freqHz == meshtasticSlotHz(915000000u, 928000000u, p.bwKhz10, 0, 0, m.name);
        }
        ck("AU Meshtastic rows sit on ANZ's slots", n >= 7 && slots);
        ck("AU LongFast is 919.875 (slot 20)", profile((uint8_t)meshPresetFind(Proto::MESHTASTIC, "")).freqHz == 919875000u);
        const int16_t mca = meshPresetFind(Proto::MESHCORE, nullptr);
        ck("AU MeshCore's own row is Mid, 915.075 SF9", mca >= 0 && profile((uint8_t)mca).freqHz == 915075000u && profile((uint8_t)mca).sf == 9);
        setRegion(REGION_EU);
    }

    suite("The Meshtastic override: region, preset, frequency slot");
    {
        setRegion(REGION_US);
        setMeshtasticOverride(0, "", 0);
        ck("nothing chosen, nothing added", !meshtasticOverrideActive());
        const uint8_t base = profileCount();
        // US LongFast on its default slot is the well-known 906.875.
        setMeshtasticOverride(1, "", 0);
        ck("a region makes the override row", meshtasticOverrideActive() && profileCount() == base + 1);
        int16_t i = meshPresetFind(Proto::MESHTASTIC, "");
        ck("Meshtastic resolves to it, MeshCore does not", i == base && meshPresetFind(Proto::MESHCORE, nullptr) != i);
        ck("US LongFast default slot is 906.875", profile((uint8_t)i).freqHz == 906875000u);
        setMeshtasticOverride(1, "", 1);
        ck("slot 1 is the first, 902.125", profile((uint8_t)meshPresetFind(Proto::MESHTASTIC, "")).freqHz == 902125000u);
        ck("US LongFast has 104 slots", mtSlotCount(1, "LongFast") == 104);
        setMeshtasticOverride(1, "LongTurbo", 0);
        const Profile& t = profile((uint8_t)meshPresetFind(Proto::MESHTASTIC, ""));
        ck("LongTurbo: 500 kHz SF11, named for the preset", t.bwKhz10 == 5000 && t.sf == 11 && !strcmp(t.name, "MT LongTurbo"));
        setMeshtasticOverride(3, "LongTurbo", 0);   // EU_868 is 250 kHz wide
        ck("a preset too wide for the region falls back",
           profile((uint8_t)meshPresetFind(Proto::MESHTASTIC, "")).bwKhz10 <= 2500 && !mtPresetFits(3, 3));
        setMeshtasticOverride(3, "", 0);
        ck("EU_868 LongFast is 869.525", profile((uint8_t)meshPresetFind(Proto::MESHTASTIC, "")).freqHz == 869525000u);
        setMeshtasticOverride(2, "", 0);
        ck("EU_433 is below the module's band", (profile((uint8_t)meshPresetFind(Proto::MESHTASTIC, "")).flags & PF_433) != 0);
        setMeshtasticOverride(0, "LongTurbo", 0);
        ck("a preset the band plan lacks is an override", meshtasticOverrideActive());
        setMeshtasticOverride(0, "MediumSlow", 0);
        ck("one it has is not", !meshtasticOverrideActive() && profileCount() == base);
        setMeshtasticOverride(0, "", 0);
        setRegion(REGION_EU);
    }

    suite("The override on every band plan: AU, narrow presets, power, duty, slots");
    {
        // Region numbers are 1 + the index in the list: 1 US, 2 EU_433, 3 EU_868, 4 CN, 5 JP,
        // 6 ANZ, 7 RU, 8 KR, 9 TW, 10 IN, 11 NZ_865, 12 TH, 13 UA_433, 14 UA_868, 18 EU_N_868.
        setRegion(REGION_AU);
        setMeshtasticOverride(0, "", 3);                    // AUTO on the AU plan, slot 3
        ck("AUTO on the AU plan is ANZ, not EU_868", meshtasticOverrideActive() &&
           profile((uint8_t)meshPresetFind(Proto::MESHTASTIC, "")).freqHz == 915625000u);
        setMeshtasticOverride(6, "", 0);
        ck("ANZ LongFast on its own slot is the AU table's 919.875",
           profile((uint8_t)meshPresetFind(Proto::MESHTASTIC, "")).freqHz == 919875000u);
        setRegion(REGION_EU);
        setMeshtasticOverride(0, "", 0);

        // Every Meshtastic mesh row of every band plan has a preset to step to.
        for (int plan = 0; plan < 3; plan++) {
            setRegion(plan == 0 ? REGION_EU : plan == 1 ? REGION_US : REGION_AU);
            bool all = true;
            for (uint8_t i = 0; i < profileCount(); i++) {
                const Profile& r = profile(i);
                if (r.hint != Proto::MESHTASTIC || (r.group != PG_MESH_EU && r.group != PG_MESH_US)) continue;
                bool found = false;
                for (uint8_t k = 0; k < mtPresetCount(); k++) if (!strcasecmp(r.name + 3, mtPresetName(k))) found = true;
                if (!found) { all = false; printf("    no preset for %s\n", r.name); }
            }
            ck(plan == 0 ? "EU: every mesh row (NarrowFast too) is a preset" : plan == 1 ? "US: every mesh row is a preset" : "AU: every mesh row is a preset", all);
        }
        setRegion(REGION_EU);

        // The EU's narrow presets land where the band plan's own rows put them.
        const int16_t slowRow = meshPresetFind(Proto::MESHTASTIC, "NarrowSlow");
        const int16_t fastRow = meshPresetFind(Proto::MESHTASTIC, "NarrowFast");
        const uint32_t slowHz = profile((uint8_t)slowRow).freqHz, fastHz = profile((uint8_t)fastRow).freqHz;
        setMeshtasticOverride(3, "NarrowSlow", 0);
        const uint32_t d1 = profile((uint8_t)meshPresetFind(Proto::MESHTASTIC, "")).freqHz;
        setMeshtasticOverride(3, "NarrowFast", 0);
        const uint32_t d2 = profile((uint8_t)meshPresetFind(Proto::MESHTASTIC, "")).freqHz;
        ck("EU_868 NarrowSlow is the table's 869.442 within 1 kHz", d1 + 1000 >= slowHz && d1 <= slowHz + 1000);
        ck("EU_868 NarrowFast is the table's 869.442 within 1 kHz", d2 + 1000 >= fastHz && d2 <= fastHz + 1000);
        ck("NarrowFast is SF7, 62.5 kHz", profile((uint8_t)meshPresetFind(Proto::MESHTASTIC, "")).sf == 7 &&
           profile((uint8_t)meshPresetFind(Proto::MESHTASTIC, "")).bwKhz10 == 625);

        // Power: what the region allows, never more than the EU rule it had.
        auto limit = [](uint8_t region, uint8_t want) {
            setMeshtasticOverride(region, "", 0);
            return txLimitDbm(profile((uint8_t)meshPresetFind(Proto::MESHTASTIC, "")), want);
        };
        ck("JP is held to 13 dBm", limit(5, 22) == 13);
        ck("TH is held to 16 dBm", limit(12, 22) == 16);
        ck("UA_868 is held to 14 dBm", limit(14, 22) == 14);
        ck("RU is held to 20 dBm", limit(7, 22) == 20);
        ck("a lower setting is left alone", limit(5, 10) == 10);
        ck("US sends what was asked, up to 22", limit(1, 22) == 22);
        ck("EU_868 keeps the EU ceiling of 22", limit(3, 255) == EU_MESH_DBM_MAX);

        // Duty cycle: the region's own, or the band's from the BNetzA table.
        auto duty = [](uint8_t region) {
            setMeshtasticOverride(region, "", 0);
            return txDutyPermille(profile((uint8_t)meshPresetFind(Proto::MESHTASTIC, "")));
        };
        ck("UA_868 is 1 % (10 permille)", duty(14) == 10);
        ck("EU_868 is 10 %", duty(3) == 100);
        ck("JP has no duty figure", duty(5) == 0);
        ck("US has no duty figure", duty(1) == 0);

        // Regions the 868/915 module cannot send in are receive-only.
        auto rxOnly = [](uint8_t region) {
            setMeshtasticOverride(region, "", 0);
            return (profile((uint8_t)meshPresetFind(Proto::MESHTASTIC, "")).flags & PF_433) != 0;
        };
        ck("CN is receive-only", rxOnly(4));
        ck("EU_433, UA_433 and MY_433 are receive-only", rxOnly(2) && rxOnly(13) && rxOnly(15));
        ck("JP, TH, KR, IN, RU and UA_868 can send", !rxOnly(5) && !rxOnly(12) && !rxOnly(8) && !rxOnly(10) && !rxOnly(7) && !rxOnly(14));

        // Slot numbers past 255.
        ck("US NarrowSlow has 416 slots", mtSlotCount(1, "NarrowSlow") == 416);
        setMeshtasticOverride(1, "NarrowSlow", 300);
        ck("slot 300 is 920.71875, not slot 44", profile((uint8_t)meshPresetFind(Proto::MESHTASTIC, "")).freqHz == 920718750u);
        setMeshtasticOverride(1, "NarrowSlow", 416);
        ck("slot 416 is the last one, 927.96875", profile((uint8_t)meshPresetFind(Proto::MESHTASTIC, "")).freqHz == 927968750u);
        setMeshtasticOverride(1, "NarrowSlow", 500);
        ck("a slot past the end stays on the last", profile((uint8_t)meshPresetFind(Proto::MESHTASTIC, "")).freqHz == 927968750u);
        setMeshtasticOverride(0, "", 0);
    }

    return report();
}
