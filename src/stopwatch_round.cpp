#if defined(M5_STOPWATCH)
#include "stopwatch_round.h"
#include "theme.h"
namespace StopWatchRound {
void background(TFT_eSPI& t, uint32_t now, const DetectionEngine& eng,
                bool advance, int floor, int textTop) {
    Theme::setBackgroundFloor(floor, textTop);
    Theme::drawActiveBackground(t, now, 0, t.height(), eng, advance);
}
void overlay(TFT_eSPI& t, uint32_t now) { Theme::drawBackgroundOverlay(t, now); }
const uint8_t* backdrop() { return nullptr; }
bool backgroundTap(int x, int y, uint32_t now) { return Theme::backgroundTap(x, y, now); }
}
#endif
