// The name this board goes by on LoRa -- src/lora_myname.cpp -- on its own:
// which name goes on the air, the Meshtastic short name rule (SQWT while he is
// still SQUACHY), and what a typed name is allowed to keep.
#include "lora_myname.h"
#include "lora_profiles.h"
#include "test_util.h"
#include <cstring>

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

    return report();
}
