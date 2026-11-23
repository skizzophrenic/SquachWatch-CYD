// SquachWatch-CYD -- the Network Goblin Scout advert decoder. See goblin.h.
#include "goblin.h"
#include <string.h>

namespace Goblin {

bool decode(const uint8_t* in, size_t len, Visitor& out) {
    if (!in || len < LEN_MIN) return false;
    if (in[0] != MAGIC0 || in[1] != MAGIC1) return false;
    if (in[2] < 1) return false;   // version 0 never existed
    const uint32_t id = (uint32_t)in[3] | ((uint32_t)in[4] << 8) |
                        ((uint32_t)in[5] << 16) | ((uint32_t)in[6] << 24);
    if (id == 0) return false;
    Visitor v{};
    v.id    = id;
    v.level = (uint16_t)(in[7] | ((uint16_t)in[8] << 8));
    v.hue   = in[9];
    v.hat   = (uint8_t)(in[10] & 0x1F);
    v.hoard = (uint8_t)(in[10] >> 5);
    size_t n = 0;
    for (size_t i = LEN_MIN; i < len && n < NAME_LEN; i++) {
        const uint8_t c = in[i];
        if (c < 0x20 || c > 0x7E) break;   // printable ASCII only
        v.name[n++] = (char)c;
    }
    // Trailing spaces are padding, not a name.
    while (n && v.name[n - 1] == ' ') n--;
    v.name[n] = '\0';
    out = v;
    return true;
}

size_t encode(const Visitor& v, uint8_t* out) {
    out[0] = MAGIC0; out[1] = MAGIC1; out[2] = 1;
    out[3] = (uint8_t)v.id; out[4] = (uint8_t)(v.id >> 8);
    out[5] = (uint8_t)(v.id >> 16); out[6] = (uint8_t)(v.id >> 24);
    out[7] = (uint8_t)v.level; out[8] = (uint8_t)(v.level >> 8);
    out[9] = v.hue;
    out[10] = (uint8_t)((v.hat & 0x1F) | ((v.hoard & 7) << 5));
    size_t n = 0;
    while (n < NAME_LEN && v.name[n]) { out[LEN_MIN + n] = (uint8_t)v.name[n]; n++; }
    return LEN_MIN + n;
}

uint16_t skin565(uint8_t hue, uint8_t value) {
    // HSV at a middling saturation: a goblin is a colour, not a neon sign.
    const uint8_t sat = 150;
    const uint8_t region = hue / 43;
    const uint8_t rem = (uint8_t)((hue - region * 43) * 6);
    const uint8_t p = (uint8_t)((value * (255 - sat)) >> 8);
    const uint8_t q = (uint8_t)((value * (255 - ((sat * rem) >> 8))) >> 8);
    const uint8_t t = (uint8_t)((value * (255 - ((sat * (255 - rem)) >> 8))) >> 8);
    uint8_t r, g, b;
    switch (region) {
        case 0:  r = value; g = t; b = p; break;
        case 1:  r = q; g = value; b = p; break;
        case 2:  r = p; g = value; b = t; break;
        case 3:  r = p; g = q; b = value; break;
        case 4:  r = t; g = p; b = value; break;
        default: r = value; g = p; b = q; break;
    }
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

} // namespace Goblin
