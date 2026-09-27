// SquachWatch-CYD — fitting a name into a box, off the device.
//
// What this guards: a name that does not fit its cell does not crash anything.
// It draws a fragment that looks like a whole name, and two different runs,
// devices or channels draw the SAME fragment -- which on the survey strip means
// the control that picks between two antenna runs cannot tell them apart. The
// bug is invisible in a screenshot of one row and only shows up when two rows
// with a shared prefix sit side by side, so it is pinned here:
//   - a name that fits is copied whole and carries NO mark (a mark on a name
//     that fitted would be a lie in the other direction);
//   - a name that does not fit always carries the mark, in every mode;
//   - fitMid keeps the tail, so two labels that agree on their first thirteen
//     characters land in DIFFERENT cells -- the whole point of the middle cut;
//   - the real fixture pair, "sim walk A"/"sim walk B", at the width the
//     survey strip actually has;
//   - the buffer is never overrun and is always terminated, at every width
//     from zero up, including the degenerate one-glyph box.
#include "test_util.h"
#include "ui_fit.h"
#include <cstring>
#include <string>

using namespace UiFit;

static std::string head(const char* s, int want, size_t cap = 64) {
    char buf[128];
    memset(buf, '#', sizeof buf);
    const size_t n = fitHead(buf, cap, s, want);
    ck("  fitHead terminates inside cap", strlen(buf) < cap);
    ck("  fitHead returns the length it wrote", n == strlen(buf));
    return std::string(buf);
}

static std::string mid(const char* s, int want, size_t cap = 64) {
    char buf[128];
    memset(buf, '#', sizeof buf);
    const size_t n = fitMid(buf, cap, s, want);
    ck("  fitMid terminates inside cap", strlen(buf) < cap);
    ck("  fitMid returns the length it wrote", n == strlen(buf));
    return std::string(buf);
}

int main() {
    suite("glyph arithmetic -- the built-in font is fixed pitch");
    ck("six pixels a glyph at size 1", glyphAdvance(1) == 6);
    ck("twelve at size 2", glyphAdvance(2) == 12);
    ck("size 0 is treated as 1 (TFT_eSPI clamps it too)", glyphAdvance(0) == 6);
    // The survey strip's own cell on the CrowPanel: 400 logical px, four slots,
    // 4 px margins and 4 px gaps -> 95 px a box, less the 18 px the size-2 role
    // letter takes on the left and 3 px of right padding.
    ck("the survey strip's cell holds 12 glyphs", chars(95 - 18 - 3) == 12);
    ck("a box narrower than a glyph holds none", chars(5) == 0);
    ck("a zero box holds none", chars(0) == 0);
    ck("a negative box holds none", chars(-40) == 0);

    suite("a name that fits is left alone");
    ck("exact fit carries no mark", head("sim walk A", 10) == "sim walk A");
    ck("exact fit, middle cut too", mid("sim walk A", 10) == "sim walk A");
    ck("short of the box", head("roof", 12) == "roof");
    ck("empty stays empty", head("", 12).empty());
    ck("null source is empty, not a crash", head(nullptr, 12).empty());

    suite("a name that does not fit always says so");
    ck("head cut marks its end", head("dipole at the mast", 10) == "dipole at>");
    ck("mid cut marks its middle", mid("dipole at the mast", 10) == "dipol>mast");
    ck("one-glyph box is just the mark", head("anything", 1) == ">");
    ck("one-glyph box, middle cut", mid("anything", 1) == ">");
    ck("two-glyph box keeps one real glyph", mid("anything", 2) == "a>");

    suite("the defect this exists for: a shared prefix");
    // Thirteen characters of agreement. The survey strip holds ten of them.
    const char* a = "dipole at the mast";
    const char* b = "dipole at the balcony rail";
    ck("head cut draws the IDENTICAL cell", head(a, 10) == head(b, 10));
    ck("middle cut does not", mid(a, 10) != mid(b, 10));
    ck("...and both cells still start with the category", mid(a, 10).substr(0, 5) == "dipol");
    ck("...and both cells end with what tells them apart", mid(a, 10) == "dipol>mast");
    ck("...the other one too", mid(b, 10) == "dipol>rail");

    suite("the simulator's own fixture, at the strip's real width");
    // Ten and ten at twelve glyphs: both fit whole, which is the before/after
    // this change is measured by -- at nine they were both "sim walk ".
    ck("run A fits whole at 12", mid("sim walk A", 12) == "sim walk A");
    ck("run B fits whole at 12", mid("sim walk B", 12) == "sim walk B");
    ck("at the old nine they were the same cell",
       std::string("sim walk A").substr(0, 9) == std::string("sim walk B").substr(0, 9));

    suite("the buffer is the other bound");
    // A cell may ask for more glyphs than the caller's buffer holds; the buffer
    // wins and the mark still goes on, because the name is still cut.
    ck("cap wins over want, and marks", head("dipole at the mast", 40, 6) == "dipo>");
    ck("cap wins over want, middle", mid("dipole at the mast", 40, 6) == "di>st");
    ck("cap of 1 writes only the terminator", head("x", 12, 1).empty());
    {
        char buf[8];
        memset(buf, '#', sizeof buf);
        fitHead(buf, 0, "overflow me", 99);
        ck("cap of 0 writes nothing at all", buf[0] == '#');
    }

    suite("every width from 0 to 30, on a long label");
    {
        const char* s = "dipole at the balcony rail";   // 26, the survey label's own room is 23
        bool okLen = true, okMark = true, okTerm = true;
        for (int w = 0; w <= 30; w++) {
            char h[64], m[64];
            const size_t nh = fitHead(h, sizeof h, s, w);
            const size_t nm = fitMid(m, sizeof m, s, w);
            if (nh != strlen(h) || (int)nh > (w < 0 ? 0 : w)) okLen = false;
            if (nm != strlen(m) || (int)nm > (w < 0 ? 0 : w)) okLen = false;
            if (h[nh] != '\0' || m[nm] != '\0') okTerm = false;
            const bool cut = strlen(s) > (size_t)(w < 0 ? 0 : w);
            if (w >= 1) {
                if (cut != (strchr(h, '>') != nullptr)) okMark = false;
                if (cut != (strchr(m, '>') != nullptr)) okMark = false;
            }
        }
        ck("never writes more glyphs than the box holds", okLen);
        ck("always terminated", okTerm);
        ck("marked when and only when it was cut", okMark);
    }

    return report();
}
