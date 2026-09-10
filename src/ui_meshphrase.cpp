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
char        s_letter = 0;              // 0 = choosing a letter; else choosing its word
char        s_pending[MeshMsg::PHRASE_TEXT_MAX];
bool        s_stretchShown = false;
const char* s_status = nullptr;
uint16_t    s_statusCol = 0;

// Filled by the draw, read by the touch -- one computation, so the two cannot
// disagree about where anything is.
Rect     s_btn[3];
bool     s_btnOn[3] = { false, false, false };
Rect     s_letterRect[26];
bool     s_letterOn[26] = { false };
Rect     s_wordRect[18];
uint16_t s_wordIdx[18];
uint8_t  s_wordN = 0;

// Which letters start at least one word. The list never changes, so this is
// worked out once, the first time the picker opens.
uint8_t  s_letterWords[26];
bool     s_lettersCounted = false;

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

void countLetters() {
    if (s_lettersCounted) return;
    uint16_t tmp[18];
    for (int i = 0; i < 26; i++)
        s_letterWords[i] = MeshMsg::wordsStartingWith((char)('A' + i), tmp, 18);
    s_lettersCounted = true;
}

// The phrase so far on one small line: the words already chosen, then the one
// being chosen (its letter, once there is one), then a dash per word to go.
// It is only there to say where you are, and every row it does not take goes
// to the keys.
void progress(TFT_eSPI& t, int y) {
    t.setTextSize(1);
    // One dark strip under the whole line. Each piece prints with the ground
    // colour behind it, and over a bright background those separate little
    // boxes read as blocks rather than as dashes.
    t.fillRect(4, y - 2, t.width() - 8, t.fontHeight() + 4, Theme::BG);
    int x = 8;
    for (int i = 0; i < MeshMsg::PHRASE_WORDS; i++) {
        char cur[3] = { s_letter ? s_letter : '_', s_letter ? '_' : '\0', '\0' };
        const char* s = i < s_pickN ? MeshMsg::WORDS[s_picked[i]]
                      : i == s_pickN ? cur : "-";
        t.setTextColor(i < s_pickN ? Theme::CYAN : i == s_pickN ? Theme::VAPOR_PINK
                                                                : Theme::W95_SHADOW, Theme::BG);
        t.setCursor(x, y);
        t.print(s);
        x += t.textWidth(s) + 6;
    }
}

// Picking is two steps, and only one of them is on screen at a time: the
// alphabet, then that letter's words. The first version showed the five
// slots, all 26 letters and up to eighteen words at once, which made every
// one of them too small to hit reliably with a finger on a resistive panel.
// Splitting them gives each step the whole screen.
void drawPick(TFT_eSPI& t) {
    const int w = t.width(), h = t.height();
    const bool port = h > w;
    const int M = 4, G = 4, avail = w - 2 * M;
    char tb[20];
    snprintf(tb, sizeof tb, "WORD %u OF 5", (unsigned)(s_pickN + 1));
    title(t, tb);
    progress(t, 28);

    const int top = 42, bottom = h - BH - 6 - 6, gridH = bottom - top;
    s_wordN = 0;
    for (bool& on : s_letterOn) on = false;

    if (!s_letter) {
        // The alphabet, as big as it will go. The short last row is centred,
        // so Z does not sit alone in a corner.
        countLetters();
        const int cols = port ? 5 : 7, rows = (26 + cols - 1) / cols;
        const int lw = (avail - (cols - 1) * G) / cols;
        const int lh = (gridH - (rows - 1) * G) / rows;
        t.setTextSize(3);
        for (int i = 0; i < 26; i++) {
            const int r = i / cols, c = i % cols;
            const int inRow = (26 - r * cols) < cols ? 26 - r * cols : cols;
            const int x = M + (cols - inRow) * (lw + G) / 2 + c * (lw + G);
            const int y = top + r * (lh + G);
            s_letterRect[i] = { (int16_t)x, (int16_t)y, (int16_t)lw, (int16_t)lh };
            // A letter no word starts with is drawn, so the alphabet still
            // reads as one, but cannot be pressed.
            s_letterOn[i] = s_letterWords[i] > 0;
            t.fillRect(x, y, lw, lh, Theme::BG);
            t.drawRect(x, y, lw, lh, s_letterOn[i] ? Theme::CYAN : Theme::W95_SHADOW);
            const char s[2] = { (char)('A' + i), '\0' };
            t.setTextColor(s_letterOn[i] ? Theme::WHITE : Theme::W95_SHADOW, Theme::BG);
            t.setCursor(x + (lw - t.textWidth(s)) / 2, y + (lh - t.fontHeight()) / 2);
            t.print(s);
        }
        return;
    }

    // That letter's words. At most 18 of them -- test/meshmsg_test.cpp holds
    // the list to that -- which is six rows of three in landscape and nine of
    // two in portrait, at twice the size the combined screen could afford.
    // Fewer words get taller keys, up to a limit that keeps them looking like
    // a list rather than a column of slabs.
    s_wordN = MeshMsg::wordsStartingWith(s_letter, s_wordIdx, 18);
    const int cols = port ? 2 : 3;
    const int rows = (s_wordN + cols - 1) / cols;
    const int gw = (avail - (cols - 1) * G) / cols;
    int gh = rows ? (gridH - (rows - 1) * G) / rows : gridH;
    if (gh > 34) gh = 34;
    for (uint8_t i = 0; i < s_wordN; i++) {
        const int x = M + (i % cols) * (gw + G), y = top + (i / cols) * (gh + G);
        s_wordRect[i] = { (int16_t)x, (int16_t)y, (int16_t)gw, (int16_t)gh };
        t.fillRect(x, y, gw, gh, Theme::BG);
        t.drawRect(x, y, gw, gh, Theme::CYAN);
        const char* wd = MeshMsg::WORDS[s_wordIdx[i]];
        // Eight letters at size 2 is 96 pixels, and the narrowest key is 101.
        // Size 1 is only the fallback in case a longer word is ever added.
        t.setTextSize(2);
        if (t.textWidth(wd) > gw - 4) t.setTextSize(1);
        t.setTextColor(Theme::WHITE, Theme::BG);
        t.setCursor(x + (gw - t.textWidth(wd)) / 2, y + (gh - t.fontHeight()) / 2);
        t.print(wd);
    }
}

// One step back, whatever the last step was: from a letter's words to the
// alphabet; from the alphabet to the previous word's list, with that word
// taken off; from the very start, out.
void pickBack() {
    if (s_letter) { s_letter = 0; return; }
    if (s_pickN) {
        s_pickN--;
        s_letter = MeshMsg::WORDS[s_picked[s_pickN]][0];
        return;
    }
    s_mode = Mode::SHOW;
}

void drawStretch(TFT_eSPI& t) {
    title(t, "STRETCHING");
    // Measured: 2.75 s at MeshMsg::ITERS on an ESP32. Said plainly, because
    // the screen does not move while it runs and a frozen screen with no
    // warning looks like a crash.
    const int y = para(t, 34, "Turning five words into a key. This takes about three seconds, "
                              "and the screen will freeze until it is done.", Theme::W95_LIGHT);
    para(t, y + 6, "That is on purpose: every guess an attacker makes has to take "
                "that long too. It only happens when you set a phrase.", Theme::W95_LIGHT);
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
        case Mode::PICK:    drawPick(t); chrome(t, "[ BACK ]", nullptr, "[ CANCEL ]"); break;
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
            if (b == 0) { pickBack(); return; }
            if (b == 2) { s_mode = Mode::SHOW; return; }
            // Only the step on screen answers: its rects are the only ones
            // this frame filled in.
            if (!s_letter) {
                for (int i = 0; i < 26; i++)
                    if (s_letterOn[i] && inRect(s_letterRect[i], x, y)) { s_letter = (char)('A' + i); return; }
                return;
            }
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
    } else if (mode == 2 || mode == 3) {
        // 2 is the word step, 3 the letter step, both two words in.
        s_picked[0] = wordIndex("GIBSON");
        s_picked[1] = wordIndex("MOTHMAN");
        s_pickN  = 2;
        s_letter = mode == 2 ? 'P' : 0;
        s_mode   = Mode::PICK;
    } else {
        s_mode = Mode::SHOW;
    }
}

#endif // SQUACH_MESH
