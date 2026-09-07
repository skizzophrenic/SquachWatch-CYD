// SquachWatch-CYD -- baked geometry for the VOID EYE outfit.
//
// The costume replaces his head with a sphere of deep space: a star field
// streaming outward from the middle, an iris that flicks around over it, and
// a fur lid that closes on a blink. None of that is allowed to compute a
// sine, a cosine or a square root on the draw path -- Squachy is redrawn
// every frame into the shared 8-bit sprite, so anything per-star per-frame is
// paid ~46 times a frame forever.
//
// Same reasoning and the same shape as CAUSTIC_TILE next door: `static const`
// at file scope lands in .rodata, which is memory-mapped on ESP32 and read
// directly, so this costs ~250 bytes of the spare flash and nothing at all of
// the heap -- the resource that is actually tight here.
//
// STAR_DX/DY are unit directions in 64ths (so |v| <= 64), STAR_PH staggers the
// stars so they do not all recycle on the same frame, and RSTEP is a
// square-root ramp of the radius in 64ths of the sphere. The ramp is what
// makes this read as a warp field rather than a drifting cloud: a star walking
// RSTEP at a constant phase speed accelerates as it goes, and because the
// square root spreads the samples by area, the field stays evenly dense
// instead of bunching in the middle the way a linear ramp does.
//
//   rad = RSTEP[(phase + STAR_PH[i]) & 63];
//   x   = cx + (STAR_DX[i] * rad * r) / 4096;
//
// Division, not a shift: the product is signed and a right shift on a
// negative value is not portable.
#pragma once
#include <stdint.h>

static const uint8_t VOID_STAR_N = 46;

static const int8_t VOID_STAR_DX[VOID_STAR_N] = {
      64,  -47,    6,   39,  -63,   54,  -17,  -29,   60,  -59,   27,   19,  -55,   63,
     -37,   -8,   49,  -64,   45,   -3,  -41,   63,  -53,   14,   32,  -61,   58,  -25,
     -22,   57,  -62,   35,   11,  -51,   64,  -43,    0,   43,  -64,   51,  -11,  -34,
      62,  -57,   22,   24,
};

static const int8_t VOID_STAR_DY[VOID_STAR_N] = {
       0,   43,  -64,   51,  -11,  -34,   62,  -57,   22,   24,  -58,   61,  -32,  -14,
      52,  -63,   41,    3,  -45,   64,  -49,    9,   37,  -62,   56,  -19,  -27,   59,
     -60,   30,   16,  -54,   63,  -39,   -5,   47,  -64,   47,   -6,  -39,   63,  -54,
      17,   29,  -60,   59,
};

static const uint8_t VOID_STAR_PH[VOID_STAR_N] = {
       0,   23,   46,    5,   28,   51,   10,   33,   56,   15,   38,   61,   20,   43,
       2,   25,   48,    7,   30,   53,   12,   35,   58,   17,   40,   63,   22,   45,
       4,   27,   50,    9,   32,   55,   14,   37,   60,   19,   42,    1,   24,   47,
       6,   29,   52,   11,
};

// sqrt ramp, 0 .. 56 (the field stops short of the rim so the sphere keeps an
// unbroken edge)
static const uint8_t VOID_RSTEP[64] = {
       0,    7,   10,   12,   14,   16,   17,   19,   20,   21,   22,   23,   24,   25,
      26,   27,   28,   29,   30,   31,   32,   32,   33,   34,   35,   35,   36,   37,
      37,   38,   39,   39,   40,   41,   41,   42,   42,   43,   43,   44,   45,   45,
      46,   46,   47,   47,   48,   48,   49,   49,   50,   50,   51,   51,   52,   52,
      53,   53,   54,   54,   55,   55,   56,   56,
};

// Iris spokes: x0, y0, x1, y1 in 64ths of the sphere radius, drawn relative to
// the iris centre. Eight rather than the flying eye's twelve -- at this size
// the extra four are not resolvable.
static const uint8_t VOID_SPOKE_N = 8;
static const int8_t VOID_SPOKE[VOID_SPOKE_N][4] = {
    {   13,    0,   31,    0 },
    {    9,    9,   22,   22 },
    {    0,   13,    0,   31 },
    {   -9,    9,  -22,   22 },
    {  -13,    0,  -31,    0 },
    {   -9,   -9,  -22,  -22 },
    {    0,  -13,    0,  -31 },
    {    9,   -9,   22,  -22 },
};
