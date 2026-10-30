#pragma once
#include "frame_layout.h"
#include "draw_band.h"

namespace BandFrame {

// Draw with full-screen coordinates into a reusable, smaller sprite. The
// final push uses only the remaining panel rows. State advances once even
// though a scene's pixels are visited in more than one drawing pass.
template <typename Sprite, typename Draw, typename Push>
void render(Sprite& frame, int screenW, int screenH, uint32_t background,
            Draw draw, Push push) {
    const int rows = FrameLayout::bufferHeight(screenW, screenH);
    if (screenW <= 0 || screenH <= 0 || rows <= 0) return;
    for (int y = 0; y < screenH; y += rows) {
        const int count = screenH - y < rows ? screenH - y : rows;
        DrawBand::set(y, y + count);
        frame.setViewport(0, -y, screenW, screenH, true);
        frame.fillRect(0, 0, screenW, screenH, background);
        draw(frame, y == 0);
        push(y, count);
    }
    DrawBand::all();
    frame.resetViewport();
}

} // namespace BandFrame
