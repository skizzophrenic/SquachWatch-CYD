// The remembered Meshtastic names -- src/lora_names.cpp -- on their own:
// what is kept, what gives way when it is full, and that an image written to
// flash comes back as the same names, and that a bad one is refused.
#include "lora_names.h"
#include "test_util.h"
#include <cstring>
#include <cstdio>

using namespace Lora;

int main() {
    char tag[12], name[24];

    suite("Learning and looking up");
    {
        Names::clear();
        ck("empty to start", Names::count() == 0 && !Names::dirty());
        ck("nothing known about a stranger", !Names::lookup(0x11, tag, sizeof tag, name, sizeof name));
        ck("a NodeInfo is learned", Names::learn(0x11, "YT", "Yeti"));
        ck("and makes the store dirty", Names::dirty());
        ck("it comes back", Names::lookup(0x11, tag, sizeof tag, name, sizeof name) && !strcmp(tag, "YT") && !strcmp(name, "Yeti"));
        Names::markSaved();
        ck("the same NodeInfo again changes nothing", !Names::learn(0x11, "YT", "Yeti") && !Names::dirty());
        ck("a new long name is a change", Names::learn(0x11, "", "Yeti of the Ridge") && Names::dirty());
        Names::lookup(0x11, tag, sizeof tag, name, sizeof name);
        ck("an empty short name kept the old one", !strcmp(tag, "YT") && !strcmp(name, "Yeti of the Ridge"));
        ck("node 0 and two empty names are ignored", !Names::learn(0, "X", "Y") && !Names::learn(0x22, "", ""));
        ck("a long name is cut to the row's size", Names::learn(0x33, "LONGSHORTNAMEX", "A name far longer than twenty-four letters")
           && Names::lookup(0x33, tag, sizeof tag, name, sizeof name) && strlen(tag) == 11 && strlen(name) == 23);
    }

    suite("Full: the one learned longest ago gives way");
    {
        Names::clear();
        for (uint32_t i = 1; i <= Names::CAP; i++) { char t[8]; snprintf(t, sizeof t, "N%u", (unsigned)i); Names::learn(i, t, t); }
        ck("full", Names::count() == Names::CAP);
        Names::learn(1, "N1", "N1");                         // node 1 announces again: now the newest
        Names::learn(1000, "NEW", "New");
        ck("still full", Names::count() == Names::CAP);
        ck("node 1, heard again, stayed", Names::lookup(1, tag, sizeof tag, nullptr, 0));
        ck("node 2, the oldest, went", !Names::lookup(2, tag, sizeof tag, nullptr, 0));
        ck("the newcomer is in", Names::lookup(1000, tag, sizeof tag, nullptr, 0) && !strcmp(tag, "NEW"));
    }

    suite("To flash and back");
    {
        Names::clear();
        Names::learn(0x3b1c9a2e, "YT", "Yeti");
        Names::learn(0x0badf00d, "MTH", "Mothman");
        uint8_t img[4000];
        ck("too small a buffer saves nothing", Names::save(img, 10) == 0);
        const size_t len = Names::save(img, sizeof img);
        ck("an image of two names", len == 3 + 2 * 40);
        ck("the full store fits bytesNeeded", Names::bytesNeeded() >= 3 + (size_t)Names::CAP * 40);
        Names::clear();
        ck("loads", Names::load(img, len) && Names::count() == 2 && !Names::dirty());
        ck("both names back", Names::lookup(0x3b1c9a2e, tag, sizeof tag, name, sizeof name) && !strcmp(name, "Yeti")
           && Names::lookup(0x0badf00d, tag, sizeof tag, name, sizeof name) && !strcmp(tag, "MTH"));
        // The order survives too: after a reload, the older one still gives way first.
        for (uint32_t i = 1; i <= Names::CAP - 1; i++) Names::learn(100 + i, "X", "X");
        ck("Yeti, saved first, is the one that went", !Names::lookup(0x3b1c9a2e, tag, sizeof tag, nullptr, 0)
           && Names::lookup(0x0badf00d, tag, sizeof tag, nullptr, 0));

        uint8_t bad[sizeof img]; memcpy(bad, img, len);
        bad[0] ^= 0xFF;
        ck("a wrong magic is refused and leaves it empty", !Names::load(bad, len) && Names::count() == 0);
        memcpy(bad, img, len); bad[1] = 99;
        ck("an unknown version is refused", !Names::load(bad, len));
        ck("a short image is refused", !Names::load(img, len - 1) && Names::count() == 0);
        memcpy(bad, img, len); memset(bad + 3 + 4, 'A', 12); memset(bad + 3 + 16, 'B', 24);
        ck("unterminated names from flash come back terminated", Names::load(bad, len)
           && Names::lookup(0x3b1c9a2e, tag, sizeof tag, name, sizeof name) && strlen(tag) == 11 && strlen(name) == 23);
    }

    return report();
}
