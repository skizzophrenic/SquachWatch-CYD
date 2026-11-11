#include "ui_cryptid_guide.h"

#include "pet.h"
#include "squachy.h"
#include "theme.h"

void uiCryptidGuideDraw(TFT_eSPI& t, uint32_t now) {
    const int w = t.width(), h = t.height();
    const auto bar = Theme::computeButtonBar(w, h);
    const bool found = Squachy::mothmanUnlocked();
    t.fillRect(0, 0, w, h, Theme::BG);
    Theme::drawTitleBar(t, ">> FIELD GUIDE <<");

    Theme::drawListHeading(t, "CRYPTID FIELD GUIDE", Theme::CYAN);
    const int top = Theme::LIST_TOP + Theme::listHeadingH() + 2;
    const int bottom = bar.y - 5;
    if (bottom > top) t.drawRect(5, top, w - 10, bottom - top, Theme::VAPOR_PURPLE);
    const int scale = bottom - top >= 132 ? 2 : 1;
    const int spacing = bottom - top < 100 ? 9 : 13;
    const int artY = top + (spacing == 9 ? 13 : 18 + 10 * scale);
    if (found) Pet::drawMothman(t, w / 2, artY, scale, now);
    else {
        t.fillCircle(w / 2 - 6, artY, 2, 0xFA08);
        t.fillCircle(w / 2 + 6, artY, 2, 0xFA08);
    }

    t.setTextFont(1);
    t.setTextSize(1);
    auto line = [&](const char* message, int y, uint16_t color) {
        if (y + 8 >= bottom) return;
        t.setTextColor(color, Theme::BG);
        t.setCursor((w - t.textWidth(message)) / 2, y);
        t.print(message);
    };
    const int textY = artY + 12 * scale;
    line(found ? "MOTHMAN" : "UNDISCOVERED", textY, Theme::RED);
    line("Habitat: Fireflies", textY + spacing, Theme::CYAN);
    if (found) {
        line("Tap him to power up.", textY + 2 * spacing, Theme::WHITE);
        line("Moth Hood / Red Wings", textY + 3 * spacing, Theme::WHITE);
    } else {
        line("Tap the red eyes", textY + 2 * spacing, Theme::WHITE);
        line("in the treeline.", textY + 3 * spacing, Theme::WHITE);
    }

    Theme::drawButton(t, bar.x[0], bar.y, bar.w[0], bar.h, "[ BACK ]", false);
    if (found && !Squachy::jailed())
        Theme::drawButton(t, bar.x[2], bar.y, bar.w[2], bar.h, "[ EQUIP ]", false);
}
