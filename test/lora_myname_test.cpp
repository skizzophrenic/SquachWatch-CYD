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
        for (uint8_t r = 0; r < 2; r++) {
            setRegion(r == 0 ? REGION_EU : REGION_US);
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
        setRegion(REGION_US);
        const int16_t ms = meshPresetFind(Proto::MESHTASTIC, "MediumSlow");
        ck("US MediumSlow is 914.875 MHz SF10", ms >= 0 && profile((uint8_t)ms).freqHz == 914875000u && profile((uint8_t)ms).sf == 10);
    }

    return report();
}
