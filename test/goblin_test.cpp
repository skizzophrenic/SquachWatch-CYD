// The Network Goblin Scout advert decoder (goblin.h). Like the SquachMesh
// codec beside it, this runs on every 0xFFFF advert in the air, so the
// refusals are the cases worth writing down.
#include "goblin.h"
#include "test_util.h"
#include <cstring>

using namespace Goblin;

int main() {
    suite("The example from issue #37");
    // FF FF 4E 47 01 78 56 34 12 0C 00 C8 00 4D 6F 63 6B, company ID first.
    const uint8_t ex[] = { 0x4E, 0x47, 0x01, 0x78, 0x56, 0x34, 0x12, 0x0C, 0x00, 0xC8, 0x00, 0x4D, 0x6F, 0x63, 0x6B };
    Visitor v{};
    ck("decodes", decode(ex, sizeof ex, v));
    ck("id 0x12345678", v.id == 0x12345678u);
    ck("level 12", v.level == 12);
    ck("hue 200", v.hue == 200);
    ck("no hat, no hoard", v.hat == 0 && v.hoard == 0);
    ck("name Mock", strcmp(v.name, "Mock") == 0);

    suite("Flags split into hat and hoard");
    uint8_t b[32];
    memcpy(b, ex, sizeof ex);
    b[10] = (uint8_t)(5 | (3 << 5));
    ck("hat 5, hoard 3", decode(b, sizeof ex, v) && v.hat == 5 && v.hoard == 3);
    b[10] = 0xFF;
    ck("hat 31, hoard 7", decode(b, sizeof ex, v) && v.hat == 31 && v.hoard == 7);

    suite("Refusals");
    Visitor keep{}; keep.id = 77;
    memcpy(b, ex, sizeof ex); b[0] = 'S';
    ck("wrong magic", !decode(b, sizeof ex, keep) && keep.id == 77);
    memcpy(b, ex, sizeof ex); b[2] = 0;
    ck("version 0", !decode(b, sizeof ex, keep) && keep.id == 77);
    memcpy(b, ex, sizeof ex); b[3] = b[4] = b[5] = b[6] = 0;
    ck("goblin ID 0", !decode(b, sizeof ex, keep) && keep.id == 77);
    ck("too short", !decode(ex, LEN_MIN - 1, keep) && keep.id == 77);
    ck("null", !decode(nullptr, 20, keep));
    const uint8_t sqm[] = { 0x53, 0x51, 0x4D, 0x31, 1, 0, 0, 0, 0, 0, 0, 0 };
    ck("a SquachMesh advert is not a goblin", !decode(sqm, sizeof sqm, keep));

    suite("Later versions and odd names");
    memcpy(b, ex, sizeof ex); b[2] = 4;
    ck("version 4 accepted", decode(b, sizeof ex, v) && v.id == 0x12345678u);
    ck("no name at all", decode(ex, LEN_MIN, v) && v.name[0] == 0);
    memcpy(b, ex, sizeof ex); b[13] = 0x07;
    ck("name stops at a control byte", decode(b, sizeof ex, v) && strcmp(v.name, "Mo") == 0);
    memcpy(b, ex, LEN_MIN);
    memcpy(b + LEN_MIN, "ABCDEFGHIJKLMNOP", 16);
    ck("name capped at twelve", decode(b, LEN_MIN + 16, v) && strcmp(v.name, "ABCDEFGHIJKL") == 0);

    suite("Round trip");
    Visitor w{}; w.id = 0xCAFEF00Du; w.level = 999; w.hue = 85; w.hat = 2; w.hoard = 6;
    strcpy(w.name, "Grubnik");
    uint8_t enc[LEN_MIN + NAME_LEN];
    const size_t n = encode(w, enc);
    ck("length", n == LEN_MIN + 7);
    ck("decodes back", decode(enc, n, v) && v.id == w.id && v.level == 999 && v.hue == 85 &&
                       v.hat == 2 && v.hoard == 6 && strcmp(v.name, "Grubnik") == 0);
    ck("skin colour is not black", skin565(85) != 0);
    return report();
}
