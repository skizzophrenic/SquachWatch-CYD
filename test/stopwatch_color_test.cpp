// Run against the installed M5GFX headers, not a simulated display driver.
#define _USE_MATH_DEFINES
#include "test_util.h"
#include "frame_push.h"
#include "theme.h"
#include <cstring>
// Host shim lacks the 32-bit PROGMEM read used by M5GFX's other color types.
#ifndef pgm_read_dword
static inline uint32_t pgm_read_dword(const void* p) {
    uint32_t value;
    std::memcpy(&value, p, sizeof value);
    return value;
}
#endif
#include <lgfx/v1/misc/colortype.hpp>

static uint16_t native(uint8_t c) {
    const uint16_t wire = FramePush::rgb332Wire(c);
    return (wire >> 8) | (wire << 8);
}
int main() {
    suite("StopWatch native pixels through actual M5GFX color types");
    static_assert(lgfx::rgb565_t::depth == lgfx::rgb565_nonswapped,
                  "Typed row must select the native RGB565 converter");
    bool all = true;
    for (unsigned c = 0; c < 256; ++c) {
        lgfx::rgb565_t pixel(native(c));
        lgfx::swap565_t wire;
        wire = pixel; // actual driver's native-to-wire conversion
        const unsigned r = c >> 5, g = (c >> 2) & 7;
        const unsigned blue[] = {0, 11, 21, 31};
        const uint16_t expected = (((r << 2) | (r >> 1)) << 11)
            | (((g << 3) | g) << 5) | blue[c & 3];
        all &= pixel.raw == expected;
        all &= wire.raw == uint16_t((expected >> 8) | (expected << 8));
        all &= pixel.R8() == wire.R8() && pixel.G8() == wire.G8()
            && pixel.B8() == wire.B8();
    }
    ck("all 256 colors survive native-to-wire conversion", all);
    // Fixed expected quantized values, including brown's loss of low blue bits.
    const uint16_t source[] = {Theme::FUR_DARK, Theme::FUR_MAIN,
        Theme::FUR_LIGHT, Theme::SKIN_TAN, Theme::SKIN_DARK};
    const uint16_t expected[] = {0x2000, 0x4920, 0x9360, 0xFECB, 0xDB6B};
    const char* names[] = {"fur shadow", "fur main", "fur highlight", "skin tan", "skin dark"};
    for (unsigned i=0; i<5; ++i) {
        const uint16_t c = source[i];
        const uint8_t sprite = ((c & 0xE000) >> 8) | ((c & 0x0700) >> 6) | ((c & 0x0018) >> 3);
        ck(names[i], native(sprite) == expected[i]);
        printf("    source=%04X sprite=%02X native=%04X\n", c, sprite, native(sprite));
    }
    return report();
}
