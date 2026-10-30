// SquachWatch-CYD — which rows are actually being drawn right now.
//
// The 3.5" cannot hold a whole frame in RAM. It draws
// its 480x320 screen as two 480x160 bands: one half-height sprite, filled and
// pushed at the top of the panel, then filled again and pushed halfway down.
// Elecrow GPS uses the same approach with a two-thirds buffer to leave
// room for scanning and SD recording. Both passes run the drawing code.
// The sprite's viewport
// throws away whatever lands outside the band, so the picture is right -- but
// it is thrown away one primitive at a time, after the work of asking for it.
// Measured on the board, the second pass costs exactly what the first does:
// 10.1 / 8.6 ms on FIREFLIES, 22.8 / 21.8 on the aquarium. Half the drawing
// on that board is for rows the pass cannot reach.
//
// This is the row range the current pass can reach, in panel coordinates, so
// that a block of drawing can decline in one call instead of being clipped a
// pixel at a time. It is a HINT ABOUT PIXELS ONLY: skipping a block must
// never skip the state behind it, or the two passes disagree about the world
// and the screen tears between them. Positions, physics, hit rectangles and
// anything else read after the frame are computed either way -- only the
// painting is skipped.
//
// On profiles with a full-height sprite there is one pass, so the
// band is the whole screen and no block would ever be skipped. Rather than
// leave a live comparison in those builds' hot loops, has() compiles to
// `true` there and the guards vanish -- verified by building cyd-fast before
// and after and byte-comparing the firmware.
#pragma once
#include <Arduino.h>
#include "frame_layout.h"

namespace DrawBand {

#if SQW_BANDED_FRAME

// Panel rows [y0, y1) that this pass can paint.
extern int16_t g_y0, g_y1;
extern bool    g_on;

// BAND OFF on the console holds the band open at the whole screen, which is
// exactly what the code did before any of this existed. It is the A/B: the
// room's radio traffic moves the frame time by more than this change saves,
// so measuring one build against another an hour apart says nothing, and
// measuring with it on and off a minute apart says everything.
inline void setEnabled(bool on) { g_on = on; if (!on) { g_y0 = 0; g_y1 = 32767; } }
inline bool enabled()           { return g_on; }

inline void set(int y0, int y1) { if (g_on) { g_y0 = (int16_t)y0; g_y1 = (int16_t)y1; } }
inline void all()               { g_y0 = 0; g_y1 = 32767; }

// Does anything in panel rows [y0, y1) land inside this pass? A block that
// straddles the boundary answers true in both passes and is simply drawn
// twice, which is what happens today -- the gate can only ever remove work,
// never move a pixel.
inline bool has(int y0, int y1) { return y1 > g_y0 && y0 < g_y1; }

// Clamp a row loop to the band: top() raises its first row, bot() lowers the
// row it stops before. The rows outside were being computed -- a colour, a
// blend, a couple of sines -- and then dropped by the viewport one line at a
// time. Only for a loop whose body does nothing but paint: one that also
// steps an animation has to run whole, or the two passes disagree.
inline int top(int y) { return y < g_y0 ? g_y0 : y; }
inline int bot(int y) { return y > g_y1 ? g_y1 : y; }

#else

inline void set(int, int) {}
inline void all()         {}
inline bool has(int, int) { return true; }
inline int  top(int y)    { return y; }
inline int  bot(int y)    { return y; }
inline void setEnabled(bool) {}
inline bool enabled()        { return false; }

#endif

}  // namespace DrawBand
