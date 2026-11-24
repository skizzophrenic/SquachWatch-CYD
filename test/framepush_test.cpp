// SquachWatch-CYD — the colours the overlapped frame push puts on the wire.
//
// What this guards: the overlapped push converts 8-bit colour to the panel's
// 16 bits itself, from a table, instead of letting TFT_eSPI do it a pixel at
// a time. A table built from arithmetic that differs by one bit shifts every
// colour on the device slightly; nothing would crash and no screen would look
// obviously wrong, and the only way to notice would be two boards side by
// side. So all 256 of them are checked here rather than by eye.
//
// The reference is derived from what the bits MEAN -- red and green are three
// bits each expanded to five and six, blue is two expanded to five -- rather
// than copied from the same expression the code uses, because a test that
// repeats the code's own arithmetic only proves the file was saved.
#include "test_util.h"
#include "frame_push.h"
#include "frame_shot_writer.h"
#include <cstdint>
#include <string>

// Expand an n-bit field the way a display driver does: shift up and
// replicate the high bits down, so all-ones stays all-ones rather than
// landing just short of full scale.
static uint8_t red5(uint8_t r3)   { return (uint8_t)((r3 << 2) | (r3 >> 1)); }
static uint8_t green6(uint8_t g3) { return (uint8_t)((g3 << 3) | g3); }

// The wire word has the panel's high byte in its LOW half, because the SPI
// hardware sends a word lowest address first. Undo that to read it back as
// an ordinary RGB565 value.
static uint16_t asRgb565(uint16_t wire) {
    return (uint16_t)(((wire & 0xFF) << 8) | (wire >> 8));
}

struct ShotPort {
    int room = 64;
    size_t writeLimit = 7;
    std::string received;
    int availableForWrite() const { return room; }
    size_t write(const uint8_t* bytes, size_t len) {
        if (len > writeLimit) len = writeLimit;
        received.append(reinterpret_cast<const char*>(bytes), len);
        return len;
    }
};

int main() {
    suite("USB screenshot writes have a bounded wait");
    {
        ShotPort port;
        uint32_t stamp = 0;
        const auto now = [&]() { return stamp; };
        const auto idle = [&]() { ++stamp; };
        const std::string row(648, 'a');
        ck("partial writes deliver the full row", FrameShot::write(port, row.data(), row.size(), stamp, now, idle)
            && port.received == row);

        port.room = 0;
        ck("an unread USB port yields and times out",
           !FrameShot::write(port, row.data(), row.size(), stamp, now, idle) && stamp == 250);
        port.room = 64;
        port.writeLimit = 0;
        ck("a write that accepts no bytes also times out",
           !FrameShot::write(port, row.data(), row.size(), stamp, now, idle) && stamp == 500);

        port.writeLimit = 7;
        port.received.clear();
        stamp = 11995;
        const auto slowNow = [&]() { return stamp++; };
        ck("a slow reader still hits the whole-shot deadline",
           !FrameShot::write(port, row.data(), row.size(), 0, slowNow, idle)
           && !port.received.empty() && port.received.size() < row.size());
    }

    suite("Every one of the 256 colours");
    {
        int wrongRed = 0, wrongGreen = 0, wrongBlue = 0;
        // The four blue levels are the library's own, and they are NOT the
        // replication the other two channels get: that would give 0/10/21/31
        // and the driver uses 11 for the second one. Named here so the
        // difference is a decision on the record rather than a typo.
        static const uint8_t BLUE5[4] = { 0, 11, 21, 31 };
        for (int i = 0; i < 256; i++) {
            const uint8_t c = (uint8_t)i;
            const uint16_t px = asRgb565(FramePush::rgb332Wire(c));
            if (((px >> 11) & 0x1F) != red5((uint8_t)((c >> 5) & 0x07)))   wrongRed++;
            if (((px >>  5) & 0x3F) != green6((uint8_t)((c >> 2) & 0x07))) wrongGreen++;
            if (( px        & 0x1F) != BLUE5[c & 0x03])                    wrongBlue++;
        }
        ck("red lands where the bits say",   wrongRed   == 0);
        ck("green lands where the bits say", wrongGreen == 0);
        ck("blue lands where the bits say",  wrongBlue  == 0);
    }

    suite("The ends of the scale");
    // Full scale has to stay full scale. An expansion that only shifts leaves
    // white at 248,252,248 -- a faintly dirty white that survives a look.
    ck("black is black", FramePush::rgb332Wire(0x00) == 0x0000);
    ck("white is white", FramePush::rgb332Wire(0xFF) == 0xFFFF);

    suite("Each channel on its own");
    // A swapped pair of shifts is the likeliest slip and it hides well in
    // the sweep, so the primaries are named.
    ck("full red is red alone",     asRgb565(FramePush::rgb332Wire(0xE0)) == 0xF800);
    ck("full green is green alone", asRgb565(FramePush::rgb332Wire(0x1C)) == 0x07E0);
    ck("full blue is blue alone",   asRgb565(FramePush::rgb332Wire(0x03)) == 0x001F);

    suite("Blue really does only have four steps");
    // The constraint the artwork is drawn around. If this ever reads five,
    // the backgrounds were designed against a screen that does not exist.
    {
        bool seen[32] = { false };
        int distinct = 0;
        for (int i = 0; i < 256; i++) {
            const uint8_t b = (uint8_t)(asRgb565(FramePush::rgb332Wire((uint8_t)i)) & 0x1F);
            if (!seen[b]) { seen[b] = true; distinct++; }
        }
        ck("four distinct blues across all 256 colours", distinct == 4);
    }

    suite("The high byte is in the low half of the word");
    // The words go into the FIFO as-is and the hardware sends the low byte
    // first, so this IS the wire order. Backwards gives a recognisably wrong
    // picture on the board and a right one in the emulator -- worth stating.
    ck("red's bits are in the byte that goes out first",
       (FramePush::rgb332Wire(0xE0) & 0xFF) == 0xF8);

    // ---- the span planner ----
    // What this guards: a span that misses a changed row by one is a stale
    // line on the panel that nothing downstream would ever notice.
    suite("Rows to send: nothing changed, everything changed, one row");
    {
        static bool ch[320];
        FramePush::RowSpan sp[96];
        for (int i = 0; i < 320; i++) ch[i] = false;
        ck("no change, no spans", FramePush::frameSpans(ch, 240, 1, sp, 96) == 0);
        for (int i = 0; i < 240; i++) ch[i] = true;
        int n = FramePush::frameSpans(ch, 240, 1, sp, 96);
        ck("all changed is one span of the whole frame", n == 1 && sp[0].r0 == 0 && sp[0].r1 == 240);
        for (int i = 0; i < 320; i++) ch[i] = false;
        ch[7] = true;
        n = FramePush::frameSpans(ch, 240, 2, sp, 96);
        ck("one row, aligned to pairs, is the pair that holds it", n == 1 && sp[0].r0 == 6 && sp[0].r1 == 8);
        ch[7] = false; ch[239] = true;
        n = FramePush::frameSpans(ch, 240, 2, sp, 96);
        ck("the last row, aligned, stays inside the frame", n == 1 && sp[0].r0 == 238 && sp[0].r1 == 240);
        for (int i = 0; i < 320; i++) ch[i] = (i % 2) == 1;
        n = FramePush::frameSpans(ch, 240, 2, sp, 96);
        ck("alternate rows at pair alignment merge into one span", n == 1 && sp[0].r0 == 0 && sp[0].r1 == 240);
    }

    suite("Rows to send: a thousand random patterns");
    {
        static bool ch[320];
        FramePush::RowSpan sp[96];
        uint32_t seed = 12345;
        int bad = 0;
        for (int t = 0; t < 1000 && bad == 0; t++) {
            const int32_t h = (t & 1) ? 320 : 240;
            const int32_t align = (t & 2) ? 2 : 1;
            const int maxOut = (t % 7 == 0) ? 3 : 96;     // sometimes starve it
            seed = seed * 1664525u + 1013904223u;
            const int density = (int)((seed >> 24) % 100);
            for (int32_t i = 0; i < h; i++) {
                seed = seed * 1664525u + 1013904223u;
                ch[i] = (int)((seed >> 16) % 100) < density;
            }
            const int n = FramePush::frameSpans(ch, h, align, sp, maxOut);
            // every changed row is covered
            for (int32_t i = 0; i < h; i++) {
                if (!ch[i]) continue;
                bool cov = false;
                for (int k = 0; k < n; k++) if (i >= sp[k].r0 && i < sp[k].r1) { cov = true; break; }
                if (!cov) bad++;
            }
            // spans are ordered, disjoint, aligned, in bounds, non-empty
            for (int k = 0; k < n; k++) {
                if (sp[k].r0 < 0 || sp[k].r1 > h || sp[k].r0 >= sp[k].r1) bad++;
                if (sp[k].r0 % align) bad++;
                if (sp[k].r1 % align && sp[k].r1 != h) bad++;
                if (k > 0 && sp[k].r0 < sp[k - 1].r1) bad++;
            }
            if (n > maxOut) bad++;
        }
        ck("every changed row is inside a span, every span is sound", bad == 0);
    }

    return report();
}
