// SquachWatch-CYD — settings screen implementation
#include "ui_settings.h"
#include "theme.h"
#include "settings.h"
#include "squachy.h"
#include <Arduino.h>

static const int TOP_MARGIN = 16;
static int g_scroll = 0;

// Fixed display order, grouped so a colored section header can sit
// above each cluster (see groupFor()/RowGroupId below) -- CALIBRATE/
// DIAGNOSTICS used to sit between BEHAVIOR and SQUACHY rows, which
// only worked because nothing before this cared about contiguous
// groups. Squachy-only rows (customizing a character "boring mode"
// has turned off) are filtered out by visibleRows() below rather than
// removed here, so their SettingsRow values stay stable regardless of
// which mode is active.
static const SettingsRow ALL_ROWS[] = {
    SettingsRow::THEME, SettingsRow::BACKGROUND, SettingsRow::BACKGROUND_LOCK, SettingsRow::BRIGHTNESS, SettingsRow::INVERT,
    SettingsRow::RGB_SWAP, SettingsRow::ROTATION_LOCK,
    SettingsRow::BORING_MODE, SettingsRow::CONFIDENCE, SettingsRow::DETECTION_FILTER,
    SettingsRow::NICKNAME, SettingsRow::SHADES_COLOR, SettingsRow::OUTFIT,
    SettingsRow::REPLAY_INTRO, SettingsRow::VIEW_DIARY,
    SettingsRow::CALIBRATE, SettingsRow::CHECK_COLORS, SettingsRow::DIAGNOSTICS, SettingsRow::RESET_STATS, SettingsRow::BACK,
};
static const uint8_t ALL_ROWS_N = sizeof(ALL_ROWS) / sizeof(ALL_ROWS[0]);

static bool isSquachyOnlyRow(SettingsRow r) {
    return r == SettingsRow::REPLAY_INTRO || r == SettingsRow::NICKNAME ||
           r == SettingsRow::SHADES_COLOR || r == SettingsRow::OUTFIT;
}

enum class RowGroupId : uint8_t { APPEARANCE, BEHAVIOR, SQUACHY, SYSTEM };

static RowGroupId groupFor(SettingsRow r) {
    switch (r) {
        case SettingsRow::THEME:
        case SettingsRow::BACKGROUND:
        case SettingsRow::BACKGROUND_LOCK:
        case SettingsRow::BRIGHTNESS:
        case SettingsRow::INVERT:
        case SettingsRow::RGB_SWAP:
        case SettingsRow::ROTATION_LOCK:
            return RowGroupId::APPEARANCE;
        case SettingsRow::BORING_MODE:
        case SettingsRow::CONFIDENCE:
        case SettingsRow::DETECTION_FILTER:
            return RowGroupId::BEHAVIOR;
        case SettingsRow::NICKNAME:
        case SettingsRow::SHADES_COLOR:
        case SettingsRow::OUTFIT:
        case SettingsRow::REPLAY_INTRO:
        case SettingsRow::VIEW_DIARY:
            return RowGroupId::SQUACHY;
        default:  // CALIBRATE, CHECK_COLORS, DIAGNOSTICS, RESET_STATS, BACK
            return RowGroupId::SYSTEM;
    }
}

static const char* groupName(RowGroupId g) {
    switch (g) {
        case RowGroupId::APPEARANCE: return "APPEARANCE";
        case RowGroupId::BEHAVIOR:   return "BEHAVIOR";
        case RowGroupId::SQUACHY:    return "SQUACHY";
        default:                     return "SYSTEM";
    }
}

// Picked from colors already in the palette -- nothing new to invent,
// and it means a custom theme preset re-tints these along with
// everything else instead of clashing with it.
static uint16_t groupColor(RowGroupId g) {
    switch (g) {
        case RowGroupId::APPEARANCE: return Theme::CYAN;
        case RowGroupId::BEHAVIOR:   return Theme::AMBER;
        case RowGroupId::SQUACHY:    return Theme::VAPOR_PINK;
        default:                     return Theme::VAPOR_PURPLE;
    }
}

// One entry per thing actually drawn -- a header or a row -- built
// fresh each tick from whatever visibleRows() currently allows (mode-
// dependent) so a header only ever appears above a group that
// actually has visible rows in it. Shared by drawing and hit-testing
// so a tap always lands on whatever's actually on screen.
struct DisplayItem {
    bool       isHeader;
    RowGroupId group;
    SettingsRow row;   // only meaningful when !isHeader
};

static uint8_t buildDisplayList(DisplayItem* out) {
    SettingsRow rows[ALL_ROWS_N];
    uint8_t n = 0;
    bool boring = Settings::boringMode();
    for (uint8_t i = 0; i < ALL_ROWS_N; i++) {
        if (boring && isSquachyOnlyRow(ALL_ROWS[i])) continue;
        rows[n++] = ALL_ROWS[i];
    }

    uint8_t count = 0;
    bool haveLastGroup = false;
    RowGroupId lastGroup = RowGroupId::APPEARANCE;
    for (uint8_t i = 0; i < n; i++) {
        RowGroupId g = groupFor(rows[i]);
        if (!haveLastGroup || g != lastGroup) {
            out[count].isHeader = true;
            out[count].group = g;
            count++;
            lastGroup = g;
            haveLastGroup = true;
        }
        out[count].isHeader = false;
        out[count].group = g;
        out[count].row = rows[i];
        count++;
    }
    return count;
}

// Fixed heights at the bigger text size, not "shrink to fit everything
// on one screen" the way this used to work -- doubling the text size
// (the only step available with the built-in GLCD font; no fractional
// sizes) meant not everything could fit anymore regardless, so this
// scrolls now instead, same pattern LOG/raw-scan already use. Headers
// use the smaller size-1 text, both to distinguish them from real rows
// and to keep them from eating too much vertical space. Needs a live
// TFT_eSPI& since heights depend on actual font metrics -- shared by
// drawing and hit-testing so they can't drift apart.
static void computeGeom(TFT_eSPI& t, int screenH, int& top, int& bodyBottom, int& rowH, int& headerH) {
    top = TOP_MARGIN;
    bodyBottom = screenH - 4;
    t.setTextSize(2);
    rowH = t.fontHeight() + 8;
    t.setTextSize(1);
    headerH = t.fontHeight() + 6;
}

void uiSettingsInit(TFT_eSPI& t) {
    g_scroll = 0;
    t.fillRect(0, 0, t.width(), t.height(), Theme::BG);
}

void uiSettingsScroll(int delta) {
    g_scroll += delta;
    if (g_scroll < 0) g_scroll = 0;
}

static void drawHeader(TFT_eSPI& t, int w, int y, int hgt, RowGroupId g) {
    t.setTextSize(1);
    t.setTextColor(groupColor(g), Theme::BG);
    t.setCursor(8, y + (hgt - t.fontHeight()) / 2);
    t.print(groupName(g));
}

// `compact` drops the row text from size 2 to size 1. Used in portrait,
// where 240px of width isn't enough for a long label and its value at
// size 2's 12px-per-glyph: "BACKGROUND" ran straight into "MATRIX
// RAIN", and "LOCK BACKGROUND" into its "OFF", with the label drawn
// left-aligned and the value right-aligned into the same pixels. Size 1
// halves glyph width and gives every current row room to spare.
//
// Row *height* deliberately doesn't change with it -- rowH stays keyed
// to size 2 metrics in computeGeom() so tap targets keep their full
// height, and so hit-testing (which shares computeGeom) can't drift
// away from what was drawn.
static void drawRow(TFT_eSPI& t, int w, int y, int hgt, const char* label,
                    const char* value, bool danger, uint16_t labelColor,
                    bool compact) {
    // No full-row fillRect here anymore -- the dimmed background
    // effect behind this screen (see uiSettingsTick()) already
    // repaints the whole body region every frame, the same "let the
    // background do the erasing" pattern CLEAR's own counter row
    // relies on. Each print() call still gives its own glyph cells an
    // opaque BG backing (via the bg color param below) so text stays
    // crisp against a moving backdrop, without needing a full-row
    // fill that would just hide the effect entirely.
    t.setTextSize(compact ? 1 : 2);
    t.setTextColor(danger ? Theme::RED : labelColor, Theme::BG);
    t.setCursor(8, y + (hgt - t.fontHeight()) / 2);
    t.print(label);
    if (value) {
        t.setTextColor(Theme::WHITE, Theme::BG);
        int vw = t.textWidth(value);
        // 18px, not 8px, reserved on the right -- leaves room for the
        // scroll indicator without it overlapping right-aligned value
        // text.
        t.setCursor(w - 18 - vw, y + (hgt - t.fontHeight()) / 2);
        t.print(value);
    }
    t.drawFastHLine(4, y + hgt - 1, w - 8, Theme::PURPLE);
}

// Fills in what a row actually shows. valBuf is scratch space for
// rows that need to format a number — only valid for the duration of
// the caller's own loop iteration, not held onto afterward.
static void rowContent(SettingsRow r, const DetectionEngine& eng, char* valBuf, size_t valBufN,
                       const char*& label, const char*& value, bool& danger) {
    danger = false;
    value  = nullptr;
    switch (r) {
        case SettingsRow::THEME:
            label = "THEME"; value = Theme::kPalettes[Settings::paletteIndex()].name;
            break;
        case SettingsRow::BACKGROUND:
            label = "BACKGROUND"; value = Settings::backgroundName(Settings::background());
            break;
        case SettingsRow::BACKGROUND_LOCK:
            label = "LOCK BACKGROUND"; value = Settings::backgroundLocked() ? "ON" : "OFF";
            break;
        case SettingsRow::BRIGHTNESS:
            label = "BRIGHT -  +";
            snprintf(valBuf, valBufN, "%u%%", (unsigned)(Settings::brightness() * 100 / 255));
            value = valBuf;
            break;
        case SettingsRow::INVERT:
            label = "INVERT COLORS"; value = Settings::inverted() ? "ON" : "OFF";
            break;
        case SettingsRow::RGB_SWAP:
            label = "COLOR ORDER"; value = Settings::rgbSwapped() ? "SWAPPED" : "NORMAL";
            break;
        case SettingsRow::ROTATION_LOCK:
            label = "ROTATION LOCK"; value = Settings::rotationLocked() ? "ON" : "OFF";
            break;
        case SettingsRow::BORING_MODE:
            label = "BORING MODE"; value = Settings::boringMode() ? "ON" : "OFF";
            break;
        case SettingsRow::CONFIDENCE:
            label = "ALERT FILTER"; value = Settings::minConfidenceLabel();
            break;
        case SettingsRow::DETECTION_FILTER:
            // "DETECTION FILTER" (the row's own screen title, no width
            // constraint there) overlaps its own "14/14" value in
            // portrait's 240px width at this row's size-2 text --
            // confirmed with the emulator before ever touching
            // hardware. Shortened here only; the destination screen
            // keeps the full name in its title bar.
            label = "TYPE FILTER";
            snprintf(valBuf, valBufN, "%u/%u", (unsigned)Settings::enabledTypeCount(),
                     (unsigned)DetectionType::COUNT - 1);
            value = valBuf;
            break;
        case SettingsRow::CALIBRATE:
            label = "CALIBRATE TOUCH";
            break;
        case SettingsRow::CHECK_COLORS:
            label = "CHECK COLORS";
            break;
        case SettingsRow::DIAGNOSTICS:
            label = "DIAGNOSTICS";
            break;
        case SettingsRow::REPLAY_INTRO:
            label = "REPLAY INTRO";
            break;
        case SettingsRow::NICKNAME:
            label = "NICKNAME"; value = Squachy::nickname();
            break;
        case SettingsRow::SHADES_COLOR:
            label = "SHADES COLOR"; value = Squachy::shadesColorName();
            break;
        case SettingsRow::OUTFIT:
            label = "OUTFIT";
            snprintf(valBuf, valBufN, "%s (%u/%u)", Squachy::outfitName(),
                     (unsigned)Squachy::unlockedOutfitCount(), (unsigned)Squachy::outfitCount());
            value = valBuf;
            break;
        case SettingsRow::VIEW_DIARY:
            label = "SQUACHY'S DIARY";
            break;
        case SettingsRow::RESET_STATS:
            label = "RESET STATS";
            snprintf(valBuf, valBufN, "%lu total", (unsigned long)eng.lifetimeTotal());
            value = valBuf;
            danger = true;
            break;
        case SettingsRow::BACK:
            label = "< BACK";
            break;
        default:
            label = "?";
            break;
    }
}

void uiSettingsTick(TFT_eSPI& t, uint32_t now, const DetectionEngine& eng) {
    int w = t.width(), h = t.height();

    int top, bodyBottom, rowH, headerH;
    computeGeom(t, h, top, bodyBottom, rowH, headerH);

    // Whatever background style CLEAR is showing, drawn at reduced
    // strength behind this screen's own rows -- see
    // Theme::dimPaletteForOverlay()'s comment for how (temporarily
    // blending the shared palette toward BG, not per-pixel alpha,
    // which TFT_eSPI can't do cheaply). 179/256 =~ 70% toward BG, i.e.
    // the effect reads at roughly 30% of its normal strength.
    Theme::Palette saved = Theme::dimPaletteForOverlay(179);
    switch (Settings::background()) {
        case Settings::Background::STARFIELD: Theme::drawStarfield(t, now, top, bodyBottom); break;
        case Settings::Background::TOASTERS:   Theme::drawFlyingToasters(t, now, top, bodyBottom); break;
        case Settings::Background::AQUARIUM:   Theme::drawAquarium(t, now, top, bodyBottom); break;
        case Settings::Background::TERMINAL:   Theme::drawTerminalLog(t, now, top, bodyBottom); break;
        case Settings::Background::FIREFLIES:  Theme::drawFireflies(t, now, top, bodyBottom); break;
        case Settings::Background::FIRE:       Theme::drawFire(t, now, top, bodyBottom); break;
        case Settings::Background::SNOWFALL:   Theme::drawSnowfall(t, now, top, bodyBottom); break;
        case Settings::Background::SPECTRUM:   Theme::drawSpectrumWaterfall(t, now, top, bodyBottom, eng); break;
        case Settings::Background::TUNNEL:     Theme::drawWireframeTunnel(t, now, top, bodyBottom); break;
        case Settings::Background::SYNTHWAVE: Theme::drawSynthwave(t, now, top, bodyBottom); break;
        default:                               Theme::drawMatrixRain(t, now, top, bodyBottom, true); break;
    }
    Theme::restorePalette(saved);

    Theme::drawTitleBar(t, ">> SETTINGS <<");

    DisplayItem items[ALL_ROWS_N + 4];
    uint8_t n = buildDisplayList(items);

    int y = top;
    int idx = g_scroll;
    int visibleCount = 0;
    while (idx < n) {
        int itemH = items[idx].isHeader ? headerH : rowH;
        if (y + itemH > bodyBottom) break;
        if (items[idx].isHeader) {
            drawHeader(t, w, y, itemH, items[idx].group);
        } else {
            char valBuf[24];
            const char* label;
            const char* value;
            bool danger;
            rowContent(items[idx].row, eng, valBuf, sizeof(valBuf), label, value, danger);
            drawRow(t, w, y, itemH, label, value, danger, groupColor(items[idx].group),
                    h > w);
        }
        y += itemH;
        idx++;
        visibleCount++;
    }

    Theme::drawScrollbar(t, w - 4, top, bodyBottom - top, n, visibleCount, g_scroll);
}

SettingsRow uiSettingsHitTest(TFT_eSPI& t, int x, int y, int screenW, int screenH) {
    (void)x; (void)screenW;
    int top, bodyBottom, rowH, headerH;
    computeGeom(t, screenH, top, bodyBottom, rowH, headerH);

    DisplayItem items[ALL_ROWS_N + 4];
    uint8_t n = buildDisplayList(items);

    int cy = top;
    int idx = g_scroll;
    while (idx < n) {
        int itemH = items[idx].isHeader ? headerH : rowH;
        if (cy + itemH > bodyBottom) break;
        if (y >= cy && y < cy + itemH) {
            return items[idx].isHeader ? SettingsRow::NONE : items[idx].row;
        }
        cy += itemH;
        idx++;
    }
    return SettingsRow::NONE;
}
