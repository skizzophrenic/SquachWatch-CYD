#include "band_frame.h"
#include "ui_alert.h"
#include "ui_outfit_unlock.h"
#include "theme.h"
#include "detection.h"
#include "../test/test_util.h"

static int backgroundAdvances = 0, backgroundCalls = 0;
namespace Theme {
// Only the two UI objects under test are compiled with the background
// function redirected here. The UI code and shared render loop are real.
void recordBackground(TFT_eSPI&, uint32_t, int, int, const DetectionEngine&, bool advance) {
    ++backgroundCalls;
    if (advance) ++backgroundAdvances;
}
}

int main() {
    TFT_eSPI panel(320, 240);
    TFT_eSprite frame(&panel);
    frame.createSprite(320, 160);
    DetectionEngine engine;
    Detection d{}; d.type = DetectionType::FLOCK;
    uiAlertInit(panel, d);
    auto push = [](int, int) {};
    BandFrame::render(frame, 320, 240, TFT_BLACK,
        [&](TFT_eSPI& t, bool advance) { uiAlertTick(t, 10000, engine, false, nullptr, nullptr, advance); }, push);
    ck("alert background painted in both bands", backgroundCalls == 2);
    ck("alert background advances once", backgroundAdvances == 1);
    backgroundCalls = backgroundAdvances = 0;
    uiOutfitUnlockInit(panel, 0);
    BandFrame::render(frame, 320, 240, TFT_BLACK,
        [&](TFT_eSPI& t, bool advance) { uiOutfitUnlockTick(t, 10000, engine, advance); }, push);
    ck("unlock background painted in both bands", backgroundCalls == 2);
    ck("unlock background advances once", backgroundAdvances == 1);
    return report();
}
