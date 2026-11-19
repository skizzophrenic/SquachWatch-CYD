// SquachWatch-CYD — the WiFi password keyboard. See include/ui_wifipass.h.
#include "ui_wifipass.h"
#include "theme.h"
#include "privacy.h"
#if defined(M5_STOPWATCH)
#include "stopwatch_ui.h"
#endif
#include <Arduino.h>
#include <stdio.h>
#include <string.h>

namespace {

// macOS's <limits.h> defines PASS_MAX as a macro (the legacy getpass()
// limit), which the emulator build there trips over. Nothing here wants
// that one.
#ifdef PASS_MAX
#undef PASS_MAX
#endif
const uint8_t PASS_MAX = 63;          // the WPA2 limit

// Keys that are not characters.
const char K_NONE  = 0;
const char K_SHIFT = 1;
const char K_SYM   = 2;
#if defined(M5_STOPWATCH)
const char K_PAGE = 3, K_NUM = 4, K_ABC = 5;
uint8_t s_group = 0, s_page = 0;
const char* const WATCH_CHARS[] = {"abcdefghijklmnopqrstuvwxyz ", "1234567890 ", "!@#$%^&*()-_=+[]{}\\|;:'\",.<>/?`~ "};
#endif
const char K_DEL   = '\b';
const char K_OK    = '\n';

struct Key { int16_t x, y, w, h; char ch; };
Key     s_keys[48];
uint8_t s_keyN = 0;

char     s_ssid[33] = "";
char     s_buf[PASS_MAX + 1] = "";
uint8_t  s_len     = 0;
bool     s_shift   = false;
bool     s_sym     = false;
bool     s_show    = false;
uint32_t s_typedAt = 0;               // the last character shows for a moment
WifiPassResult s_result = WifiPassResult::NONE;
// What the header asks for and the rules for OK: a password unless
// uiWifiPassInitText() said otherwise.
const char* s_prompt     = "PASSWORD FOR";
uint8_t     s_max        = PASS_MAX;
bool        s_allowEmpty = false;

// Top of the first key row, as layout() worked it out. The chassis is drawn
// from here rather than from TOP_H: the rows are bottom-aligned and their
// height is clamped, so in portrait a fixed top left a hand's width of bare
// steel between the readout and the keys.
int      s_keyTop   = 0;

int8_t   s_armed    = -1;
int8_t   s_lastKey  = -1;
uint32_t s_lastUpAt = 0;
const uint32_t BOUNCE_MS = 60;        // the panel dropping out for a frame

// The payphone's jump filter, in miniature: a single sample far from the last
// is contact loss, a run of agreeing ones is the finger really moving.
struct Filter {
    int16_t x = 0, y = 0, cx = 0, cy = 0;
    uint8_t cn = 0;
    void down(int px, int py) { x = px; y = py; cn = 0; }
    void move(int px, int py) {
        if (abs(px - x) + abs(py - y) <= 40) { x = px; y = py; cn = 0; return; }
        if (cn && abs(px - cx) + abs(py - cy) <= 8) {
            if (++cn >= 3) { x = px; y = py; cn = 0; }
        } else {
            cx = px; cy = py; cn = 1;
        }
    }
} s_filter;

#if defined(M5_STOPWATCH)
const int TOP_H = 138;
const int BACK_X = 162, BACK_Y = 28, BACK_W = 142, BACK_H = 40;
const int FIELD_Y = 96, FIELD_H = 28;
#else
const int TOP_H   = 52;               // header and field above the keys

const int BACK_X  = 4, BACK_Y = 3, BACK_W = 52, BACK_H = 18;
const int FIELD_Y = 28, FIELD_H = 20;
#endif
const int MARGIN = 4, GAP = 2, ROW_GAP = 4;
const int SHOW_W  = 50;

// Letters page and symbols page, row by row. A zero is a blank key.
const char* const PAGE_ABC[4] = { "1234567890", "qwertyuiop", "asdfghjkl", "zxcvbnm" };
const char* const PAGE_SYM[4] = { "!@#$%^&*()", "-_=+[]{}\\|", ";:'\",.<>/", "?`~\0\0\0\0" };

char charFor(uint8_t row, uint8_t col) {
    char c = s_sym ? PAGE_SYM[row][col] : PAGE_ABC[row][col];
    if (!s_sym && s_shift && c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
    return c;
}

void put(int x, int y, int w, int h, char ch) {
    if (s_keyN >= sizeof s_keys / sizeof s_keys[0]) return;
    s_keys[s_keyN++] = { (int16_t)x, (int16_t)y, (int16_t)w, (int16_t)h, ch };
}

void layout(int w, int h) {
    s_keyN = 0;
#if defined(M5_STOPWATCH)
    s_keyTop=138;
    const char tabs[]={K_ABC,K_NUM,K_SYM,K_PAGE};
    for(int i=0;i<4;++i) put(55+i*90,138,84,40,tabs[i]);
    const char* chars=WATCH_CHARS[s_group];
    const int len=strlen(chars);
    for(int row=0;row<3;++row) {
        const int y=186+row*50;
        const int margin=RoundLayout::inset(y,44,w,20);
        const int kw=(w-2*margin-4*6)/5;
        for(int col=0;col<5;++col) {
            const int at=s_page*15+row*5+col;
            if(at>=len) continue;
            char c=chars[at];
            if(s_shift && c>='a' && c<='z') c-=32;
            put(margin+col*(kw+6),y,kw,44,c);
        }
    }
    put(35,398,110,38,K_SHIFT); put(178,398,110,38,K_DEL); put(321,398,110,38,K_OK);
    return;
#endif
#if defined(CARDPUTER_ADV)
    // The Cardputer types on its own keyboard: no keys on the screen, which
    // at 135 rows covered the field being typed into.
    (void)w;
    s_keyTop = h;
    return;
#endif
    const int avail = w - 2 * MARGIN;
    const int bandTop = TOP_H + 4, bandBottom = h -
#if defined(M5_STOPWATCH)
        64;
#else
        4;
#endif
    int rh = (bandBottom - bandTop - 4 * ROW_GAP) / 5;
    if (rh > 40) rh = 40;
    if (rh < 22) rh = 22;
    int y = bandBottom - (5 * rh + 4 * ROW_GAP);
    s_keyTop = y;

    // Rows 0-1: ten across.
    const int w1 = (avail - 9 * GAP) / 10;
    const int x1 = MARGIN + (avail - (10 * w1 + 9 * GAP)) / 2;
    for (uint8_t r = 0; r < 2; r++) {
        for (uint8_t i = 0; i < 10; i++) put(x1 + i * (w1 + GAP), y, w1, rh, charFor(r, i));
        y += rh + ROW_GAP;
    }
    // Row 2: nine, centred on the same unit.
    const int x2 = MARGIN + (avail - (9 * w1 + 8 * GAP)) / 2;
    for (uint8_t i = 0; i < 9; i++) put(x2 + i * (w1 + GAP), y, w1, rh, charFor(2, i));
    y += rh + ROW_GAP;
    // Row 3: SHIFT, seven, DEL -- the two wide keys take what the seven leave.
    const int wide3 = (avail - 7 * w1 - 8 * GAP) / 2;
    int x = MARGIN;
    put(x, y, wide3, rh, s_sym ? K_NONE : K_SHIFT); x += wide3 + GAP;
    for (uint8_t i = 0; i < 7; i++) { put(x, y, w1, rh, charFor(3, i)); x += w1 + GAP; }
    put(x, y, MARGIN + avail - x, rh, K_DEL);
    y += rh + ROW_GAP;
    // Row 4: page switch, space, OK.
    const int side = w1 * 2 + GAP;
    put(MARGIN, y, side, rh, K_SYM);
    put(MARGIN + side + GAP, y, avail - 2 * side - 2 * GAP, rh, ' ');
    put(MARGIN + avail - side, y, side, rh, K_OK);
    RoundLayout::keys(s_keys, s_keyN, w);
}

int keyAt(int x, int y, int reach) {
    int best = -1;
    long bestD = (long)reach * reach + 1;
    for (uint8_t i = 0; i < s_keyN; i++) {
        const Key& k = s_keys[i];
        if (k.ch == K_NONE) continue;
        const int r = k.x + k.w - 1, b = k.y + k.h - 1;
        const int dx = (x < k.x) ? (k.x - x) : (x > r ? x - r : 0);
        const int dy = (y < k.y) ? (k.y - y) : (y > b ? y - b : 0);
        const long d = (long)dx * dx + (long)dy * dy;
        if (d < bestD) { bestD = d; best = i; }
    }
    return best;
}

const char* label(char ch, char* one) {
    switch (ch) {
        case K_SHIFT: return "SHIFT";
        case K_SYM:
#if defined(M5_STOPWATCH)
            return "#+=";
        case K_PAGE: return "NEXT";
        case K_NUM: return "123";
        case K_ABC: return "ABC";
#else
            return s_sym ? "abc" : "#+=";
#endif
        case K_DEL:   return "DEL";
        case K_OK:    return "OK";
        case ' ':     return "SPACE";
        default:      one[0] = ch; one[1] = '\0'; return one;
    }
}

bool inRect(int x, int y, int rx, int ry, int rw, int rh) {
    return x >= rx - 4 && x <= rx + rw + 4 && y >= ry - 4 && y <= ry + rh + 4;
}

int showX(int w) { return w - MARGIN - SHOW_W - RoundLayout::inset(FIELD_Y, FIELD_H, w); }

void press(int x, int y, uint32_t now) {
    s_filter.down(x, y);
    s_armed = (int8_t)keyAt(x, y, 10);
    if (s_armed >= 0 && s_armed == s_lastKey && now - s_lastUpAt < BOUNCE_MS) s_armed = -1;
}

void type(char ch, uint32_t now) {
    switch (ch) {
        case K_SHIFT: s_shift = !s_shift; break;
        case K_SYM:
#if defined(M5_STOPWATCH)
            s_group=2; s_page=0; break;
        case K_ABC: s_group=0; s_page=0; break;
        case K_NUM: s_group=1; s_page=0; break;
        case K_PAGE: s_page=(s_page+1)%((strlen(WATCH_CHARS[s_group])+14)/15); break;
#else
            s_sym = !s_sym; s_shift = false; break;
#endif
        case K_DEL:   if (s_len) s_buf[--s_len] = '\0'; break;
        case K_OK:    if (s_len || s_allowEmpty) s_result = WifiPassResult::OK; break;
        case K_NONE:  break;
        default:
            if (s_len < s_max) { s_buf[s_len++] = ch; s_buf[s_len] = '\0'; s_typedAt = now; }
            break;
    }
}


// What the glass shows now, so a tick redraws only what changed. The update
// flow runs this board with no frame buffer (given up for the download's
// TLS handshake), straight onto the panel, where clearing and redrawing the
// whole screen on every key was a flash on every key. So the board is drawn
// once, and after that a key is redrawn when the finger lands on or leaves
// it, the field when a character lands or the last one goes back to *, the
// cursor when it blinks, and every key when the page or SHIFT changes.
uint32_t s_initAt = 0;
bool     s_full   = true;
int8_t   s_drawnLit   = -1;
bool     s_drawnShift = false, s_drawnSym = false, s_drawnShow = false;
uint8_t  s_drawnLen   = 0;
bool     s_drawnReveal = false, s_drawnCursor = false;
int      s_cursorX = 0;
// The transition effect writes over the frame for its first 220 ms; the
// whole screen is drawn every frame until it is done.
const uint32_t SETTLE_MS = 400;

int fieldX(int w) { return MARGIN + RoundLayout::inset(FIELD_Y - 3, FIELD_H + 6, w); }
int fieldW(int w) { return showX(w) - GAP - fieldX(w); }

void drawKey(TFT_eSPI& t, uint8_t i) {
    const Key& k = s_keys[i];
    // A blank on the symbols page is chassis, not a hole in it.
    if (k.ch == K_NONE) { t.fillRect(k.x, k.y, k.w, k.h, Theme::W95_FACE); return; }
    const bool lit = (s_armed == (int8_t)i) || (k.ch == K_SHIFT && s_shift);
#if defined(M5_STOPWATCH)
    {
    char one[2];
    const char* lab=label(k.ch,one);
    const bool active=lit || (k.ch==K_ABC && s_group==0) || (k.ch==K_NUM && s_group==1) || (k.ch==K_SYM && s_group==2);
    if(StopWatchUI::button(k.x,k.y,k.w,k.h,lab,active)) return;
    t.fillRoundRect(k.x,k.y,k.w,k.h,12,active ? Theme::PURPLE:Theme::BG);
    t.drawRoundRect(k.x,k.y,k.w,k.h,12,Theme::CYAN);
    t.setTextFont(1); t.setTextSize(k.ch>='!' && k.ch<='~' ? 3:2);
    if(t.textWidth(lab)>k.w-8) t.setTextSize(1);
    t.setTextColor(Theme::WHITE);
    t.setCursor(k.x+(k.w-t.textWidth(lab))/2,k.y+(k.h-t.fontHeight())/2);t.print(lab);
    t.setTextSize(1);
    return;
    }
#endif
    Theme::drawSteelKey(t, k.x, k.y, k.w, k.h, lit);
    char one[2];
    const char* lab = label(k.ch, one);
    t.setTextSize(2);
    if (t.textWidth(lab) > k.w - 6) t.setTextSize(1);
    t.setTextColor(lit ? Theme::VAPOR_YELLOW : Theme::WHITE);
    t.setCursor(k.x + (k.w - t.textWidth(lab)) / 2, k.y + (k.h - t.fontHeight()) / 2);
    t.print(lab);
    t.setTextSize(1);
}

// The field: the password as stars but for the last character for a moment
// (or all of it with SHOW on), and the cursor.
void drawField(TFT_eSPI& t, int w, uint32_t now) {
    const int fw = fieldW(w);
    // The payphone's readout, to the pixel: a sunken steel bezel with a
    // black screen in it and green text on that. Drawn here rather than
    // once in drawAll() because the cursor brings us back every frame and
    // a bezel that is only painted on a full redraw gets eaten by it.
    Theme::drawSteelPanel(t, fieldX(w) - 3, FIELD_Y - 3, fw + 6, FIELD_H + 6, true);
    t.fillRect(fieldX(w), FIELD_Y, fw, FIELD_H, Theme::BLACK);
    const bool reveal = s_len && now - s_typedAt < 900;
    char shown[PASS_MAX + 1];
    for (uint8_t i = 0; i < s_len; i++) shown[i] = (s_show || (reveal && i + 1 == s_len)) ? s_buf[i] : '*';
    shown[s_len] = '\0';
    t.setTextSize(2);
    const int cw = t.textWidth("M");
    const int fits = (fw - 8) / cw;
    const char* tail = s_len > fits ? shown + (s_len - fits) : shown;
    t.setTextColor(Theme::GREEN, Theme::BLACK);
    t.setCursor(fieldX(w) + 4, FIELD_Y + (FIELD_H - t.fontHeight()) / 2);
    t.print(tail);
    s_cursorX = fieldX(w) + 4 + t.textWidth(tail) + 1;
    const bool cursor = (now / 500) % 2;
    t.fillRect(s_cursorX, FIELD_Y + 3, 2, FIELD_H - 6, cursor ? Theme::GREEN : Theme::BLACK);
    t.setTextSize(1);
    s_drawnLen = s_len; s_drawnReveal = reveal; s_drawnCursor = cursor;
}

void drawShow(TFT_eSPI& t, int w) {
    t.setTextSize(1);
    Theme::drawWin95Button(t, showX(w), FIELD_Y, SHOW_W, FIELD_H, s_show ? "HIDE" : "SHOW", false);
    s_drawnShow = s_show;
}

void drawKeys(TFT_eSPI& t) {
    for (uint8_t i = 0; i < s_keyN; i++) drawKey(t, i);
    s_drawnLit = s_armed; s_drawnShift = s_shift; s_drawnSym = s_sym;
}

void drawAll(TFT_eSPI& t, uint32_t now) {
    const int w = t.width(), h = t.height();
    t.fillRect(0, 0, w, h, Theme::BLACK);
    // The keys are set into a steel chassis, the way the payphone's board
    // is. They were always the payphone's keys -- drawSteelKey, same call --
    // but floating on black they read as a wireframe of a keyboard rather
    // than as one. The panel is the whole difference.
    const int panelTop = s_keyTop - ROW_GAP;
#if !defined(M5_STOPWATCH)
    if (s_keyN) Theme::drawSteelPanel(t, 0, panelTop, w, h - panelTop, false);
#endif
    t.setTextWrap(false);
    // Header: BACK, then which network this is for.
    Theme::drawWin95Button(t, BACK_X, BACK_Y, BACK_W, BACK_H, "BACK", false);
    // On the large panels the two lines become one, in the body font: the
    // 16-row face has no room to stack in the 25 rows above the field.
#if defined(M5_STOPWATCH)
    t.setTextSize(1); t.setTextColor(Theme::CYAN, Theme::BG);
    char title[34]; snprintf(title, sizeof title, "%.28s", s_ssid);
    t.setCursor((w - t.textWidth(title)) / 2, 72); t.print(title);
#else
    const bool big = Theme::large();
    Theme::bodyFontOn(t);
    t.setTextColor(Theme::VAPOR_PINK, Theme::BG);
    t.setCursor(BACK_X + BACK_W + 8, BACK_Y + 1);
    t.print(s_prompt);
    if (big) t.print(" ");
    t.setTextColor(Theme::CYAN, Theme::BG);
    const int sx = BACK_X + BACK_W + 8 + (big ? t.textWidth(s_prompt) + t.textWidth(" ") : 0);
    t.setCursor(sx, big ? BACK_Y + 1 : BACK_Y + 11);
    char ssid[40];
    const int maxChars = (w - sx - MARGIN) / t.textWidth("M");
    char pv[40];
    snprintf(ssid, sizeof ssid, "%.*s", maxChars > 32 ? 32 : maxChars, Privacy::name(s_ssid, pv, sizeof pv));
    t.print(ssid);
    Theme::bodyFontOff(t);
#endif
    drawField(t, w, now);
    drawShow(t, w);
    drawKeys(t);
#if defined(CARDPUTER_ADV)
    {
        static const char* const HINT[] = { "Type it on the keyboard.", "ENTER joins.  TAB shows it.", "ESC goes back." };
        t.setTextSize(1);
        t.setTextColor(Theme::CYAN, Theme::BLACK);
        for (int i = 0; i < 3; i++) {
            t.setCursor((w - t.textWidth(HINT[i])) / 2, FIELD_Y + FIELD_H + 18 + i * 14);
            t.print(HINT[i]);
        }
    }
#endif
    s_full = false;
}

}  // namespace

void uiWifiPassInit(TFT_eSPI& t, const char* ssid) {
    strncpy(s_ssid, ssid ? ssid : "", sizeof s_ssid - 1);
    s_ssid[sizeof s_ssid - 1] = '\0';
    uiWifiPassClear();
    s_shift = s_sym = s_show = false;
#if defined(M5_STOPWATCH)
    s_group=s_page=0;
#endif
    s_prompt = "PASSWORD FOR";
    s_max = PASS_MAX;
    s_allowEmpty = false;
    s_result = WifiPassResult::NONE;
    s_armed = s_lastKey = -1;
    layout(t.width(), t.height());
    s_full   = true;
    s_initAt = millis();
    t.fillRect(0, 0, t.width(), t.height(), Theme::BG);
}

void uiWifiPassInitText(TFT_eSPI& t, const char* prompt, const char* subject, const char* initial,
                        uint8_t maxLen, bool allowEmpty) {
    uiWifiPassInit(t, subject);
    s_prompt     = prompt ? prompt : "";
    s_max        = maxLen && maxLen < PASS_MAX ? maxLen : PASS_MAX;
    s_allowEmpty = allowEmpty;
    s_show       = true;
    if (initial) {
        snprintf(s_buf, (size_t)s_max + 1, "%s", initial);
        s_len = (uint8_t)strlen(s_buf);
    }
}

void uiWifiPassTick(TFT_eSPI& t, uint32_t now) {
#if defined(M5_STOPWATCH)
    // The curved footer and touch registry are rebuilt at each frame boundary.
    s_full = true;
#endif
    const int w = t.width(), h = t.height();
    layout(w, h);
    if (s_full || now - s_initAt < SETTLE_MS) { drawAll(t, now); return; }
    // Every key when the page or SHIFT changes: the labels are different
    // and the SHIFT key lights while it is on.
    if (s_drawnShift != s_shift || s_drawnSym != s_sym) drawKeys(t);
    else if (s_drawnLit != s_armed) {
        const int8_t old = s_drawnLit;
        s_drawnLit = s_armed;
        if (old >= 0)     drawKey(t, (uint8_t)old);
        if (s_armed >= 0) drawKey(t, (uint8_t)s_armed);
    }
    if (s_drawnShow != s_show) { drawShow(t, w); drawField(t, w, now); }
    const bool reveal = s_len && now - s_typedAt < 900;
    const bool cursor = (now / 500) % 2;
    if (s_drawnLen != s_len || s_drawnReveal != reveal) drawField(t, w, now);
    else if (s_drawnCursor != cursor) {
        // The cursor alone, painted over, so the blink never clears the field.
        t.fillRect(s_cursorX, FIELD_Y + 3, 2, FIELD_H - 6, cursor ? Theme::GREEN : Theme::BLACK);
        s_drawnCursor = cursor;
    }
}

// A key from a real keyboard (the T-Deck's): the same path a tapped key takes.
// '\b' deletes, '\n' is OK, anything else printable is typed as it came --
// the keyboard's own shift and symbol keys already chose the character.
void uiWifiPassKey(char c, uint32_t now) {
    if (c == '\r') c = K_OK;
    if (c == '\t') { s_show = !s_show; return; }
    if (c == K_DEL || c == K_OK || (c >= ' ' && c < 0x7F)) type(c, now);
}

void uiWifiPassTouch(int x, int y, uint32_t now, WifiPassTouch phase) {
    switch (phase) {
        case WifiPassTouch::DOWN:
            if (inRect(x, y, BACK_X, BACK_Y, BACK_W, BACK_H)) { s_result = WifiPassResult::BACK; return; }
            // SHOW sits right of the field; its x depends on the screen width,
            // which the last layout() pass left in the rightmost key.
            if (s_keyN && y >= FIELD_Y - 4 && y <= FIELD_Y + FIELD_H + 4 &&
                x >=
#if defined(M5_STOPWATCH)
                showX(466)-4 && x<=showX(466)+SHOW_W+4
#else
                (s_keys[9].x + s_keys[9].w) - SHOW_W - 4
#endif
                ) {
                s_show = !s_show;
                return;
            }
            press(x, y, now);
            break;
        case WifiPassTouch::MOVE:
            if (s_armed < 0) return;
            s_filter.move(x, y);
            s_armed = (int8_t)keyAt(s_filter.x, s_filter.y, 20);
            break;
        case WifiPassTouch::UP: {
            // Nothing is read from the lift itself -- see the header.
            const int k = s_armed;
            s_armed    = -1;
            s_lastKey  = (int8_t)k;
            s_lastUpAt = now;
            if (k >= 0) type(s_keys[k].ch, now);
            break;
        }
    }
}

void uiWifiPassRedrawAll() { s_full = true; }

WifiPassResult uiWifiPassResult() { return s_result; }
const char*    uiWifiPassText()   { return s_buf; }
const char*    uiWifiPassSsid()   { return s_ssid; }

void uiWifiPassClear() {
    memset(s_buf, 0, sizeof s_buf);
    s_len = 0;
}

// BACK's rectangle, for the boards that aim a cursor or use ESC.
bool uiWifiPassBackRect(int16_t& x, int16_t& y, int16_t& w, int16_t& h) {
    x = BACK_X; y = BACK_Y; w = BACK_W; h = BACK_H;
    return true;
}

#if defined(M5_STOPWATCH)
uint8_t uiWifiPassStopRects(int16_t* x,int16_t* y,int16_t* w,int16_t* h,uint8_t cap) {
    uint8_t n=0;
    auto add=[&](int a,int b,int c,int d) {if(n<cap){x[n]=a;y[n]=b;w[n]=c;h[n]=d;++n;}};
    add(BACK_X,BACK_Y,BACK_W,BACK_H);
    add(showX(466),FIELD_Y,SHOW_W,FIELD_H);
    for(uint8_t i=0;i<s_keyN;++i) {
        const auto& k=s_keys[i];
        if(k.ch==K_OK && !s_allowEmpty && !s_len) continue;
        add(k.x,k.y,k.w,k.h);
    }
    return n;
}
#endif
