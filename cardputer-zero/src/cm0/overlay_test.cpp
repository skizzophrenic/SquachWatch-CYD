// overlay_test -- the ESC-hold exit modal (overlay.cpp): it is drawn on a
// copy, centred, with a bar proportional to the hold, and never outside the
// frame. Writes build/overlay_test.png (half-way through a hold, over a
// striped frame) for a look.
#include "overlay.h"
#include "png_writer.h"

#include <cstdio>
#include <vector>

static int g_checks = 0, g_failures = 0;
#define CHECK(cond, what) do { g_checks++; if (cond) std::printf("ok  : %s\n", what); \
    else { g_failures++; std::printf("FAIL: %s\n", what); } } while (0)

static const int W = 320, H = 170;

// Pixels of the bar's fill colour on the bar's middle row.
static int barFill(const std::vector<uint16_t>& px) {
    const int y = (H - 70) / 2 + 36 + 5;
    int n = 0;
    for (int x = 0; x < W; x++) if (px[(size_t)y * W + x] == 0x07FF) n++;
    return n;
}

int main() {
    std::vector<uint16_t> in((size_t)W * H);
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) in[(size_t)y * W + x] = (y / 8) % 2 ? 0x8410 : 0x2104;
    const std::vector<uint16_t> orig = in;
    std::vector<uint16_t> out;

    cm0::composeExitHint(in, W, H, 0.5f, out);
    CHECK(in == orig, "the input frame is not touched (drawn on a copy)");
    CHECK(out.size() == in.size(), "same size out");
    const int bx = (W - 224) / 2, by = (H - 70) / 2;
    CHECK(out[(size_t)by * W + bx] == 0xF81F, "pink frame at the box's corner");
    CHECK(out[(size_t)(by + 20) * W + bx + 10] != in[(size_t)(by + 20) * W + bx + 10] ||
          in[(size_t)(by + 20) * W + bx + 10] == 0x0000, "panel drawn inside the box");
    CHECK(out[5] == in[5] && out[(size_t)(H - 1) * W + W - 1] == in[(size_t)(H - 1) * W + W - 1],
          "outside the box unchanged");
    bool text = false;
    for (int x = bx; x < bx + 224 && !text; x++)
        for (int y = by + 12; y < by + 28; y++) if (out[(size_t)y * W + x] == 0xFFFF) { text = true; break; }
    CHECK(text, "white title text drawn");

    const int track = 224 - 32;
    const int half = barFill(out);
    // The bar's middle row also crosses the inner cyan frame twice.
    CHECK(half >= track / 2 && half <= track / 2 + 3, "bar half full at progress 0.5");
    cm0::composeExitHint(in, W, H, 0.0f, out);
    CHECK(barFill(out) <= 2, "bar empty at progress 0 (only the frame's two pixels)");
    cm0::composeExitHint(in, W, H, 1.7f, out);
    CHECK(barFill(out) >= track && barFill(out) <= track + 3, "progress clamped to 1: bar full");

    // A frame smaller than the box: nothing written out of bounds.
    std::vector<uint16_t> tiny(100 * 40, 0x1234), tout;
    cm0::composeExitHint(tiny, 100, 40, 0.5f, tout);
    CHECK(tout.size() == tiny.size(), "small frame: no overrun, same size");
    std::vector<uint16_t> empty, eout;
    cm0::composeExitHint(empty, W, H, 0.5f, eout);
    CHECK(eout.empty(), "short buffer: returned as is");

    cm0::composeExitHint(in, W, H, 0.5f, out);
    std::vector<uint8_t> rgb;
    for (uint16_t v : out) {
        const uint8_t r = (v >> 11) & 0x1F, g = (v >> 5) & 0x3F, b = v & 0x1F;
        rgb.push_back((uint8_t)((r << 3) | (r >> 2)));
        rgb.push_back((uint8_t)((g << 2) | (g >> 4)));
        rgb.push_back((uint8_t)((b << 3) | (b >> 2)));
    }
    PngWriter::write("build/overlay_test.png", W, H, rgb.data());

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
