// SquachWatch-CYD -- the lil guy who walks through the flying toasters.
//
// VAPOR SHAGGY: the walk cycle extracted pixel-exact from the source GIF (see
// extract_gif.py) and then recoloured and given trailing hair. He was drawn
// for a background that got shelved; this is the whole of him that survived,
// and he now makes a cameo on the mower's line instead.
//
// 8 frames, 10 x 10 art pixels, two bits per pixel packed little-end-first
// along each row: 0 transparent, 1 hair, 2 skin, 3 body. That is 320 bytes of
// flash and nothing of the heap.
//
// The cadence is 80 ms a frame, which is SLOWER than the device's own frame
// rate, so it must be driven off the clock and never off a frame counter:
//
//   frame = (millis() / 80) % 8;
//
// Hair, skin and body are the three colours below. The pale pink quantises to
// near-white on an 8-bit panel, which is fine against the toasters' deep space
// and would not be against anything bright.
#pragma once
#include <stdint.h>

static const uint8_t LILGUY_FRAMES = 8;
static const uint8_t LILGUY_W = 10;
static const uint8_t LILGUY_H = 10;

// row = LILGUY[frame * LILGUY_H + y]; pixel x = (row >> (x * 2)) & 3
static const uint32_t LILGUY[LILGUY_FRAMES * LILGUY_H] = {
    0x000000u, 0x001540u, 0x000950u, 0x002A40u, 0x000E40u, 0x000E40u, 0x000E00u, 0x000F00u,
    0x000F80u, 0x000A00u, 0x001100u, 0x000540u, 0x000950u, 0x002A40u, 0x000E40u, 0x000F90u,
    0x008F80u, 0x008F00u, 0x0023C0u, 0x000280u, 0x000000u, 0x000440u, 0x000550u, 0x000940u,
    0x002A40u, 0x000E90u, 0x000F80u, 0x003FA0u, 0x003FC0u, 0x00A0A0u, 0x000000u, 0x000500u,
    0x001950u, 0x002A50u, 0x000E40u, 0x000E40u, 0x000F80u, 0x000F00u, 0x003FE0u, 0x002820u,
    0x000000u, 0x001540u, 0x000950u, 0x002A50u, 0x000E40u, 0x000B40u, 0x000E00u, 0x000F00u,
    0x000F80u, 0x000A00u, 0x001100u, 0x000540u, 0x000950u, 0x002A40u, 0x000E40u, 0x000B40u,
    0x008B00u, 0x008F00u, 0x0023C0u, 0x000280u, 0x000000u, 0x000440u, 0x000550u, 0x000940u,
    0x002A40u, 0x000B40u, 0x000B00u, 0x002F00u, 0x003FC0u, 0x00A0A0u, 0x000000u, 0x000500u,
    0x001950u, 0x002A50u, 0x000E40u, 0x000E40u, 0x000B00u, 0x000F00u, 0x003FE0u, 0x002820u,
};
