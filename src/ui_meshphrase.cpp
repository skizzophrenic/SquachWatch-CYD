// SquachWatch-CYD — the SquachMesh phrase screen. See include/ui_meshphrase.h.
#include "ui_meshphrase.h"

#if SQUACH_MESH
#include "theme.h"
#include "meshtalk.h"
#include "meshmsg.h"
#include "detection.h"
#include <Arduino.h>
#include <stdio.h>
#include <string.h>

namespace {

enum class Mode : uint8_t { SHOW, ROLLED, PICK, STRETCH };

struct Rect { int16_t x, y, w, h; };
inline bool inRect(const Rect& r, int x, int y) {
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

Mode        s_mode = Mode::SHOW;
bool        s_done = false;
uint16_t    s_rolled[MeshMsg::PHRASE_WORDS];
uint16_t    s_picked[MeshMsg::PHRASE_WORDS];
uint8_t     s_pickN = 0;
char        s_letter = 0;              // 0 = no letter chosen yet
char        s_pending[MeshMsg::PHRASE_TEXT_MAX];
bool        s_stretchShown = false;
const char* s_status = nullptr;
uint16_t    s_statusCol = 0;

// Filled by the draw, read by the touch -- one computation, so the two cannot
// disagree about where anything is.
Rect     s_btn[3];
bool     s_btnOn[3] = { false, false, false };
Rect     s_letterRect[26];
Rect     s_wordRect[18];
uint16_t s_wordIdx[18];
uint8_t  s_wordN = 0;

const int BW = 68, BH = 26;

// BACK's row and BACK's size, the same as every other screen here, with two
// more beside it on the right.
void chrome(TFT_eSPI& t, const char* l, const char* m, const char* r) {
    const int w = t.width(), h = t.height();
    const int y = h - BH - 6;
    s_btn[0] = { 4, (int16_t)y, BW, BH };
    s_btn[1] = { (int16_t)(w - 4 - 2 * BW - 6), (int16_t)y, BW, BH };
    s_btn[2] = { (int16_t)(w - 4 - BW), (int16_t)y, BW, BH };
    const char* labels[3] = { l, m, r };
    for (int i = 0; i < 3; i++) {
        s_btnOn[i] = labels[i] != nullptr;
        if (s_btnOn[i])
            Theme::drawButton(t, s_btn[i].x, s_btn[i].y, s_btn[i].w, s_btn[i].h, labels[i], false);
    }
}

void title(TFT_eSPI& t, const char* s) {
    t.setTextWrap(false);
    t.setTextSize(2);
    t.setTextColor(Theme::VAPOR_PINK, Theme::BG);
    t.setCursor(8, 6);
    t.print(s);
}

int para(TFT_eSPI& t, int y, const char* text, uint16_t col) {
    t.setTextSize(1);
    t.setTextColor(col, Theme::BG);
    // Theme::wrapText fills 48-character rows; cap the width at 47 of them.
    const int charW = t.textWidth("M");
    int maxW = t.width() - 16;
    if (maxW > 47 * charW) maxW = 47 * charW;
    char lines[6][48];
    const uint8_t n = Theme::wrapText(t, text, maxW, lines, 6);
    for (uint8_t i = 0; i < n; i++) {
        t.setCursor(8, y);
        t.print(lines[i]);
        y += t.fontHeight() + 1;
    }
    return y;
}

// Five words, numbered, one to a line -- read out loud, the numbers are what
// stop two people agreeing on the right words in the wrong order.
int bigWords(TFT_eSPI& t, int y, const char* const words[MeshMsg::PHRASE_WORDS], uint16_t col) {
    t.setTextSize(2);
    for (int i = 0; i < MeshMsg::PHRASE_WORDS; i++) {
        char nb[4];
        snprintf(nb, sizeof nb, "%d", i + 1);
        t.setTextColor(Theme::W95_SHADOW, Theme::BG);
        t.setCursor(8, y);
        t.print(nb);
        t.setTextColor(col, Theme::BG);
        t.setCursor(34, y);
        t.print(words[i]);
        y += 20;
    }
    return y;
}

void status(TFT_eSPI& t) {
    if (!s_status) return;
    t.setTextSize(1);
    t.setTextColor(s_statusCol, Theme::BG);
    t.setCursor(8, t.height() - BH - 6 - 12);
    t.print(s_status);
}

void drawShow(TFT_eSPI& t) {
    title(t, "PHRASE");
    int y = 30;
    if (MeshTalk::havePhrase()) {
        char buf[MeshMsg::PHRASE_TEXT_MAX];
        snprintf(buf, sizeof buf, "%s", MeshTalk::phrase());
        const char* words[MeshMsg::PHRASE_WORDS] = { "", "", "", "", "" };
        int n = 0;
        for (char* p = buf; *p && n < MeshMsg::PHRASE_WORDS;) {
            words[n++] = p;
            while (*p && *p != ' ') p++;
            if (*p) *p++ = '\0';
        }
        y = bigWords(t, y, words, Theme::VAPOR_YELLOW);
        para(t, y + 4, "Anyone who has these five words can read your messages. "
                       "Say them aloud only to people you trust.", Theme::W95_LIGHT);
    } else {
        t.setTextSize(2);
        t.setTextColor(Theme::WHITE, Theme::BG);
        t.setCursor(8, y);
        t.print("No phrase yet.");
        y = para(t, y + 24, "A phrase is five words you share with a friend. ROLL makes "
                            "a new one to read out; ENTER takes theirs.", Theme::W95_LIGHT);
        if (!MeshTalk::selfTestOk())
            para(t, y + 4, "Messages are off: this build's crypto failed its self-test "
                           "at boot.", Theme::RED);
    }
    status(t);
    // No way to make a key on a build whose crypto failed the self-test.
    const bool ok = MeshTalk::selfTestOk();
    chrome(t, "[ BACK ]", ok ? "[ ROLL ]" : nullptr, ok ? "[ ENTER ]" : nullptr);
}

void drawRolled(TFT_eSPI& t) {
    title(t, "NEW PHRASE");
    const char* words[MeshMsg::PHRASE_WORDS];
    for (int i = 0; i < MeshMsg::PHRASE_WORDS; i++) words[i] = MeshMsg::WORDS[s_rolled[i]];
    const int y = bigWords(t, 30, words, Theme::VAPOR_YELLOW);
    para(t, y + 4, "Read these to your friend. On theirs: PHRASE, ENTER, and the "
                   "same five in the same order.", Theme::W95_LIGHT);
    chrome(t, "[ CANCEL ]", "[ AGAIN ]", "[ USE ]");
}

void drawPick(TFT_eSPI& t) {
    const int w = t.width(), h = t.height();
    const bool port = h > w;
    const int M = 4, avail = w - 2 * M;
    char tb[20];
    snprintf(tb, sizeof tb, "WORD %u OF 5", (unsigned)(s_pickN + 1));
    title(t, tb);

    // The five slots: one row in landscape, three and two in portrait, where
    // five across would leave an eight-letter word no room.
    const int perRow = port ? 3 : 5;
    const int sw = (avail - (perRow - 1) * 4) / perRow, sh = 18;
    t.setTextSize(1);
    for (int i = 0; i < MeshMsg::PHRASE_WORDS; i++) {
        const int x = M + (i % perRow) * (sw + 4);
        const int y = 28 + (i / perRow) * (sh + 4);
        const bool filled = i < s_pickN, current = i == s_pickN;
        t.fillRect(x, y, sw, sh, Theme::BG);
        t.drawRect(x, y, sw, sh, current ? Theme::VAPOR_PINK
                               : filled  ? Theme::CYAN : Theme::W95_SHADOW);
        if (filled) {
            const char* wd = MeshMsg::WORDS[s_picked[i]];
            t.setTextColor(Theme::WHITE, Theme::BG);
            t.setCursor(x + (sw - t.textWidth(wd)) / 2, y + (sh - 8) / 2);
            t.print(wd);
        }
    }
    const int slotRows = (MeshMsg::PHRASE_WORDS + perRow - 1) / perRow;

    // The alphabet.
    const int cols = port ? 9 : 13;
    const int lw = (avail - (cols - 1) * 2) / cols, lh = port ? 22 : 20;
    const int ly0 = 28 + slotRows * (sh + 4) + 2;
    t.setTextSize(2);
    for (int i = 0; i < 26; i++) {
        const int x = M + (i % cols) * (lw + 2), y = ly0 + (i / cols) * (lh + 2);
        s_letterRect[i] = { (int16_t)x, (int16_t)y, (int16_t)lw, (int16_t)lh };
        const bool sel = s_letter == (char)('A' + i);
        const uint16_t bg = sel ? Theme::PURPLE : Theme::BG;
        t.fillRect(x, y, lw, lh, bg);
        t.drawRect(x, y, lw, lh, sel ? Theme::VAPOR_PINK : Theme::W95_SHADOW);
        const char s[2] = { (char)('A' + i), '\0' };
        t.setTextColor(sel ? Theme::WHITE : Theme::CYAN, bg);
        t.setCursor(x + (lw - t.textWidth(s)) / 2, y + (lh - t.fontHeight()) / 2);
        t.print(s);
    }
    const int gy0 = ly0 + ((26 + cols - 1) / cols) * (lh + 2) + 4;

    // That letter's words. At most 18 of them -- test/meshmsg_test.cpp holds
    // the list to that -- which is exactly one screen of this grid at either
    // rotation.
    s_wordN = 0;
    if (!s_letter) {
        t.setTextSize(1);
        t.setTextColor(Theme::W95_LIGHT, Theme::BG);
        t.setCursor(8, gy0 + 4);
        t.print("Tap the first letter of the word.");
        return;
    }
    const int gcols = port ? 3 : 4;
    const int gw = (avail - (gcols - 1) * 4) / gcols, gh = port ? 19 : 18;
    s_wordN = MeshMsg::wordsStartingWith(s_letter, s_wordIdx, 18);
    t.setTextSize(1);
    for (uint8_t i = 0; i < s_wordN; i++) {
        const int x = M + (i % gcols) * (gw + 4), y = gy0 + (i / gcols) * (gh + 3);
        s_wordRect[i] = { (int16_t)x, (int16_t)y, (int16_t)gw, (int16_t)gh };
        t.fillRect(x, y, gw, gh, Theme::BG);
        t.drawRect(x, y, gw, gh, Theme::CYAN);
        const char* wd = MeshMsg::WORDS[s_wordIdx[i]];
        t.setTextColor(Theme::WHITE, Theme::BG);
        t.setCursor(x + (gw - t.textWidth(wd)) / 2, y + (gh - 8) / 2);
        t.print(wd);
    }
}

void drawStretch(TFT_eSPI& t) {
    title(t, "STRETCHING");
    para(t, 34, "Turning five words into a key. It takes about a second on purpose: "
                "every guess an attacker makes has to take that long too.", Theme::W95_LIGHT);
}

void startStretch() {
    s_mode = Mode::STRETCH;
    s_stretchShown = false;
}

uint16_t wordIndex(const char* w) {
    for (uint16_t i = 0; i < MeshMsg::WORD_N; i++)
        if (!strcmp(MeshMsg::WORDS[i], w)) return i;
    return 0;
}

} // namespace

void uiMeshPhraseInit(TFT_eSPI& t) {
    t.fillRect(0, 0, t.width(), t.height(), Theme::BG);
    s_mode   = Mode::SHOW;
    s_done   = false;
    s_status = nullptr;
    s_pickN  = 0;
    s_letter = 0;
}

bool uiMeshPhraseDone() { return s_done; }

void uiMeshPhraseTick(TFT_eSPI& t, uint32_t now, const DetectionEngine& eng) {
    (void)now;
    const int w = t.width(), h = t.height();

    // The stretch blocks for about a second. It runs on the tick AFTER the
    // "stretching" frame has been drawn and pushed, so the screen says what
    // it is doing instead of freezing on whatever was there.
    if (s_mode == Mode::STRETCH && s_stretchShown) {
        const bool ok = MeshTalk::setPhrase(s_pending);
        s_status    = ok ? "Phrase set. Messages use it now." : "Could not set the phrase.";
        s_statusCol = ok ? Theme::GREEN : Theme::RED;
        s_mode      = Mode::SHOW;
    }

    Theme::Palette saved = Theme::dimPaletteForOverlay(150);
    Theme::drawActiveBackground(t, now, 0, h, eng);
    Theme::restorePalette(saved);
    Theme::dimRegion(t, 0, 0, w, h, 110);

    for (bool& b : s_btnOn) b = false;
    switch (s_mode) {
        case Mode::SHOW:    drawShow(t); break;
        case Mode::ROLLED:  drawRolled(t); break;
        case Mode::PICK:    drawPick(t); chrome(t, "[ CANCEL ]", nullptr, "[ DEL ]"); break;
        case Mode::STRETCH: drawStretch(t); s_stretchShown = true; break;
    }
}

void uiMeshPhraseTouch(int x, int y) {
    int b = -1;
    for (int i = 0; i < 3; i++) if (s_btnOn[i] && inRect(s_btn[i], x, y)) b = i;

    switch (s_mode) {
        case Mode::SHOW:
            if (b == 0) s_done = true;
            else if (b == 1) { MeshTalk::rollPhrase(s_rolled); s_status = nullptr; s_mode = Mode::ROLLED; }
            else if (b == 2) { s_pickN = 0; s_letter = 0; s_status = nullptr; s_mode = Mode::PICK; }
            break;
        case Mode::ROLLED:
            if (b == 0) s_mode = Mode::SHOW;
            else if (b == 1) MeshTalk::rollPhrase(s_rolled);
            else if (b == 2 && MeshMsg::phraseText(s_rolled, s_pending, sizeof s_pending)) startStretch();
            break;
        case Mode::PICK:
            if (b == 0) { s_mode = Mode::SHOW; return; }
            if (b == 2) {                      // DEL: the letter first, then a word
                if (s_letter) s_letter = 0;
                else if (s_pickN) s_pickN--;
                return;
            }
            for (int i = 0; i < 26; i++)
                if (inRect(s_letterRect[i], x, y)) { s_letter = (char)('A' + i); return; }
            for (uint8_t i = 0; i < s_wordN; i++) {
                if (!inRect(s_wordRect[i], x, y)) continue;
                s_picked[s_pickN++] = s_wordIdx[i];
                s_letter = 0;
                if (s_pickN == MeshMsg::PHRASE_WORDS &&
                    MeshMsg::phraseText(s_picked, s_pending, sizeof s_pending)) startStretch();
                return;
            }
            break;
        case Mode::STRETCH:
            break;                             // nothing to press while it runs
    }
}

void uiMeshPhraseDemo(uint8_t mode) {
    if (mode == 1) {
        MeshTalk::rollPhrase(s_rolled);
        s_mode = Mode::ROLLED;
    } else if (mode == 2) {
        s_picked[0] = wordIndex("GIBSON");
        s_picked[1] = wordIndex("MOTHMAN");
        s_pickN  = 2;
        s_letter = 'P';
        s_mode   = Mode::PICK;
    } else {
        s_mode = Mode::SHOW;
    }
}

#endif // SQUACH_MESH
