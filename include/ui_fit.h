// SquachWatch-CYD — fitting a name into a box, measured instead of guessed.
//
// Every screen here cuts a name to a hard-coded character count: %.7s and
// %.11s on the desk alert card, %.9s in the survey strip, %.12s in the panel
// that drops a run, %.16s in the survey's own header. Each of those numbers was
// arrived at by eye against one panel, and two things go wrong with that.
//
// The count does not follow the box. A cell sized from the screen width grows
// and the number in the format string does not, so the fit is only ever right
// on the panel it was eyeballed on.
//
// And the cut leaves NO mark, so two names that agree up to the cut draw the
// IDENTICAL fragment. The survey strip is the live case: its cells hold nine
// characters, and the two runs in the simulator's own fixture are labelled
// "sim walk A" and "sim walk B" -- eleven characters and ten -- so both cells
// read "sim walk", in the one strip whose entire job is telling two antenna
// runs apart. Names people type collide at the HEAD, because people prefix by
// category: "dipole at the mast" and "dipole at the balcony rail" are the same
// string for thirteen characters. Cutting the head off a label is the one cut
// that keeps nothing useful.
//
// So: fitMid keeps the head AND the tail and drops the middle, which is where
// the agreement is. fitHead keeps the head, for the fields where the front of
// the string is the identity (a MAC, a vendor, a type name).
//
// THE MARK IS '>' and it is one glyph wide, which is the whole reason it is not
// "...". A cell that holds fourteen characters cannot spend three of them
// saying it ran out. '>' reads as "it goes on" at 5x7 -- see sim/glcdfont_data.h
// -- where '~' is a squiggle on the top row that disappears next to lowercase.
//
// THE WIDTH ARITHMETIC. The built-in font (TFT_eSPI font 1, the GLCD face) is
// FIXED PITCH: six pixels of advance per glyph at size 1, five columns of glyph
// and one of gap, and it scales in whole multiples. So the glyph count a box
// holds is plain division and no width table is needed. Font 2 -- the speech
// bubbles, and only those -- has a real width table and is NOT this; measure
// that one with t.textWidth() the way Theme::wrapText does.
//
// Header-only and free of TFT_eSPI on purpose: this is arithmetic over a
// string, so test/uifit_test.cpp can hold it on a desktop. A test that needs a
// display driver to check a coordinate is not a test anybody runs.
#pragma once
#include <stddef.h>
#include <string.h>

namespace UiFit {

// The advance of one glyph of the built-in font at `textSize`, in pixels.
inline int glyphAdvance(unsigned textSize) { return 6 * (int)(textSize ? textSize : 1); }

// How many glyphs of the built-in font fit in `boxPx` pixels at `textSize`.
// Never negative; a box narrower than one glyph holds none.
inline int chars(int boxPx, unsigned textSize = 1) {
    if (boxPx <= 0) return 0;
    return boxPx / glyphAdvance(textSize);
}

// `src` into `out`, at most `want` glyphs and never past `cap`. Keeps the
// front; if anything was dropped the LAST glyph written is `mark`, so a cut
// name never passes for a short one. Returns the glyph count written.
inline size_t fitHead(char* out, size_t cap, const char* src, int want, char mark = '>') {
    if (!out || cap == 0) return 0;
    out[0] = '\0';
    if (!src || want <= 0) return 0;
    size_t room = cap - 1;
    if ((size_t)want < room) room = (size_t)want;
    const size_t len = strlen(src);
    if (len <= room) { memcpy(out, src, len); out[len] = '\0'; return len; }
    if (room == 0) return 0;
    memcpy(out, src, room);
    out[room - 1] = mark;
    out[room] = '\0';
    return room;
}

// The same, but the mark goes in the MIDDLE and the tail survives:
// "dipole at the balcony rail" at 14 is "dipole >y rail", which is a different
// cell from "dipole >e mast". The head gets the extra glyph when the room is
// odd, because the front of a name is read first.
inline size_t fitMid(char* out, size_t cap, const char* src, int want, char mark = '>') {
    if (!out || cap == 0) return 0;
    out[0] = '\0';
    if (!src || want <= 0) return 0;
    size_t room = cap - 1;
    if ((size_t)want < room) room = (size_t)want;
    const size_t len = strlen(src);
    if (len <= room) { memcpy(out, src, len); out[len] = '\0'; return len; }
    if (room == 0) return 0;
    // One glyph for the mark; the rest split head-heavy. room == 1 leaves the
    // mark alone, which is the honest answer for a box that holds one glyph.
    const size_t keep = room - 1;
    const size_t head = (keep + 1) / 2;
    const size_t tail = keep - head;
    memcpy(out, src, head);
    out[head] = mark;
    memcpy(out + head + 1, src + len - tail, tail);
    out[room] = '\0';
    return room;
}

}  // namespace UiFit
