#include "lora_spectrum.h"
#include "test_util.h"

int main() {
    constexpr uint32_t columnMs = 1540;

    suite("The LoRa strip tolerates an older frame timestamp");
    {
        float heat[3][2] = {{1, 2}, {3, 4}, {5, 6}};
        uint8_t kind[3][2] = {{1, 1}, {2, 2}, {3, 3}};
        uint32_t at = 100;
        LoraSpectrum::advance(heat, kind, at, 99, columnMs);
        ck("no shift when the frame started before the page opened",
           at == 100 && heat[0][0] == 1 && heat[2][1] == 6 && kind[2][1] == 3);
        LoraSpectrum::advance(heat, kind, at, 100 + 2 * columnMs + 5, columnMs);
        ck("two steps preserve the newest column and clear both new columns",
           at == 100 + 2 * columnMs && heat[0][0] == 5 && heat[0][1] == 6 &&
           kind[0][0] == 3 && heat[1][1] == 0 && heat[2][0] == 0 &&
           kind[1][1] == 0 && kind[2][0] == 0);
        const uint32_t resumedAt = at + 120 * columnMs;
        LoraSpectrum::advance(heat, kind, at, resumedAt + 7, columnMs);
        ck("a long pause expires the visible history and preserves the interval phase",
           at == resumedAt && heat[0][1] == 0 && kind[0][0] == 0);
    }

    suite("The LoRa strip advances across the millis rollover");
    {
        float heat[2][1] = {{1}, {2}};
        uint8_t kind[2][1] = {{1}, {2}};
        uint32_t at = UINT32_MAX - 100;
        LoraSpectrum::advance(heat, kind, at, 1450, columnMs);
        ck("one interval elapsed through zero",
           at == 1439 && heat[0][0] == 2 && heat[1][0] == 0 && kind[0][0] == 2);
    }

    return report();
}
