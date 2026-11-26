// SquachWatch-CYD -- HUMAN SENSING's other three screens: SCOPE, SNEAK, VITALS
// (SENSE_RICH boards: the Freenove S3 3.5in at 480x320, the Freenove S3 2.8in
// and the T-Deck Plus at 320x240).
//
// Included into ui_sensing_cyd.cpp's unnamed namespace, so they share its
// helpers (text, headline, arc, barGeom, stateWord, ramp). Designed in the "S3
// Sensing Screens" review page from the Tab5 Monster C5 CSI Human Sensing
// project's screens, and approved there: the 480x320 layout keeps nearly all
// of the Tab5's, the 320x240 one drops Squachy's bubble on SCOPE and the heart
// ring on VITALS and keeps the waterfall.
//
//   SCOPE   a radar: a dome over a vaporwave sun, a sweep, one spoke per radio
//           path (the router and the busiest other transmitters, farther =
//           stronger), a MOTION contact on a disturbed one. A spoke is a path,
//           not a compass direction.
//   SNEAK   a game: START RUN, hold still 5 s while the room is read, then
//           cross it in 10 s without the score going over the line.
//   VITALS  sit still about a metre from the board: breaths a minute, rhythm,
//           depth, holds, the breathing spectrum and its waterfall, and a heart
//           rate only when it is sure (experimental). Not a medical device.

uint16_t pathColor(uint8_t k) {
    switch (k) { case 0: return Theme::CYAN; case 1: return Theme::VAPOR_PINK; case 2: return Theme::VAPOR_YELLOW; default: return Theme::GREEN; }
}
bool pathHot(const SenseStore::Router& p) { return p.live && p.score >= MOTION_LINE && p.cal >= 100; }
// A path's name, from its address, never the same as another live path's.
const char* pathName(uint8_t k) {
    uint32_t used = 0;
    const char* out = "PATH";
    for (uint8_t j = 0; j <= k; j++) {
        const SenseStore::Router& p = SenseStore::path(j);
        uint32_t i = hash32(p.tag + j * 977u) % 16;
        while (used & (1u << i)) i = (i + 1) % 16;
        used |= 1u << i;
        if (j == k) out = CRYPTIDS[i];
    }
    return out;
}
uint16_t slate() { return mixc(Theme::BG, Theme::WHITE, 150); }
// Bangers carries digits, capitals, ! and - only: a number's point is drawn by
// hand, outlined like the glyphs around it.
void bigNum(TFT_eSPI& t, int x, int y, const char* s, uint16_t col, uint8_t size) {
    const char* dot = strchr(s, '.');
    if (!dot) { headline(t, x, y, s, col, 0, size); return; }
    char a[12]; size_t n = (size_t)(dot - s); if (n > sizeof a - 1) n = sizeof a - 1;
    memcpy(a, s, n); a[n] = 0;
    headline(t, x, y, a, col, 0, size);
    const int w = headlineWidth(a, size), asc = size == 0 ? 31 : 27, d = size == 0 ? 6 : 5;
    const int dx = x + w + 1, dy = y + asc - d - 1;
    t.fillRect(dx - 2, dy - 2, d + 4, d + 4, Theme::BLACK);
    t.fillRect(dx, dy, d, d, col);
    headline(t, dx + d + 3, y, dot + 1, col, 0, size);
}
uint16_t dimc()  { return mixc(Theme::BG, Theme::VAPOR_PURPLE, 110); }
void stars(TFT_eSPI& t, uint32_t now, int y0, int y1) {
    const int W = t.width(), n = 40 * W / 320;
    for (int q = 0; q < n; q++) {
        const int x = (int)(hashf(q * 17 + 3) * W), y = y0 + (int)(hashf(q * 29 + 7) * (y1 - y0));
        const float tw = fabsf(sinf(now / (700.0f + q * 37) + q));
        t.drawPixel(x, y, mixc(Theme::BG, Theme::WHITE, (int)(25 + (q % 5 ? 70 : 160) * tw)));
    }
}

// ============================================================================
// SCOPE
// ============================================================================
void scopeScreen(TFT_eSPI& t, uint32_t now) {
    const int W = t.width();
    const bool big = W >= 460;
    const int T = 21;
    const Bar g = barGeom(W, t.height());
    const int B = g.y - 3;
    const Word w = stateWord(now);
    const SenseReport& s = SenseStore::last();
    const bool live = SenseStore::fresh(now) && senseModeLive(s.mode);
    stars(t, now, T + 2, B - 40);
    // the far hills
    {
        int px = 0, py = 0;
        for (int x = 0; x <= W; x += 4) {
            const int y = T + (big ? 52 : 40) + (int)(6 * sinf(x / 37.0f + now / 5000.0f));
            if (x) t.drawLine(px, py, x, y, mixc(Theme::BG, Theme::VAPOR_PINK, 40));
            px = x; py = y;
        }
    }
    const int cx = big ? 178 : 122, cy = B, rad = big ? 158 : 110;
    t.drawFastHLine(0, cy, W, Theme::PURPLE);
    // rings: STILL, MOTION, MOVING, WALKING
    static const float RINGS[4] = { 0.28f, 0.55f, 0.8f, 1.0f };
    for (int k = 0; k < 4; k++)
        arc(t, cx, cy, (int)(rad * RINGS[k]), 3.14159f, 6.28318f,
            k == 0 ? mixc(Theme::BG, Theme::AMBER, 120) : k == 3 ? Theme::PURPLE : mixc(Theme::BG, Theme::PURPLE, 150));
    for (int a = 0; a <= 36; a++) {   // ticks round the outside
        const float th = 3.14159f + a * 3.14159f / 36;
        const int r1 = rad + (a % 3 ? 3 : 6);
        t.drawLine(cx + (int)(cosf(th) * rad), cy + (int)(sinf(th) * rad), cx + (int)(cosf(th) * r1), cy + (int)(sinf(th) * r1),
                   mixc(Theme::BG, Theme::VAPOR_PURPLE, 150));
    }
    // the vaporwave sun, striped
    {
        const int sr = (int)(rad * 0.27f);
        const uint16_t sunLo = mixc(Theme::AMBER, Theme::PINK, 110);
        for (int y = 0; y < sr; y++) {
            if (y % 5 == 1) continue;
            const int half = (int)sqrtf((float)(sr * sr - y * y));
            t.drawFastHLine(cx - half, cy - y - 1, half * 2, mixc(sunLo, Theme::VAPOR_PINK, y * 256 / sr));
        }
    }
    // the sweep, with its afterglow
    {
        const float ph = fmodf(now / 1100.0f, 2.0f);
        const float ang = 3.14159f + (ph < 1 ? ph : 2 - ph) * 3.14159f;
        const float dir = ph < 1 ? 1.0f : -1.0f;
        for (int k = 24; k >= 1; k--) {
            const float a = ang - dir * k * 0.022f;
            if (a < 3.14159f || a > 6.28318f) continue;
            t.drawLine(cx, cy, cx + (int)(cosf(a) * rad), cy + (int)(sinf(a) * rad), mixc(Theme::BG, Theme::VAPOR_PURPLE, 70 - k * 2));
        }
        t.drawLine(cx, cy, cx + (int)(cosf(ang) * rad), cy + (int)(sinf(ang) * rad), mixc(Theme::BG, Theme::WHITE, 220));
    }
    // one spoke per path: slot 0 the router; farther out = a stronger signal
    static const float ANG[4] = { 1.38f, 1.62f, 1.12f, 1.88f };
    int hotK = -1, hotScore = 0;
    for (uint8_t k = 0; k < SenseStore::PATHS; k++) {
        const SenseStore::Router& p = SenseStore::path(k);
        if (!p.live) continue;
        const float th = ANG[k] * 3.14159f;
        const float strength = clampf((p.rssi + 90) / 55.0f, 0, 1);
        const int L = (int)(rad * (0.5f + 0.42f * strength));
        const int ex = cx + (int)(cosf(th) * L), ey = cy + (int)(sinf(th) * L);
        const bool hot = pathHot(p);
        if (hot && p.score > hotScore) { hotScore = p.score; hotK = k; }
        const uint16_t col = p.cal < 100 ? mixc(Theme::BG, pathColor(k), 110) : pathColor(k);
        if (hot) {   // the disturbed path's wedge
            for (int q = -6; q <= 6; q++) {
                const float a = th + q * 0.02f;
                t.drawLine(cx, cy, cx + (int)(cosf(a) * rad), cy + (int)(sinf(a) * rad), mixc(Theme::BG, Theme::PINK, 45));
            }
        }
        t.drawLine(cx, cy, ex, ey, hot ? Theme::VAPOR_PINK : col);
        if (hot) t.drawLine(cx + 1, cy, ex + 1, ey, Theme::VAPOR_PINK);
        for (int d = 1; d < 4; d++) t.fillRect(cx + (ex - cx) * d / 4 - 1, cy + (ey - cy) * d / 4 - 1, 3, 3, col);
        t.drawRect(ex - 3, ey - 3, 7, 6, col); t.fillRect(ex - 1, ey - 1, 3, 2, col);
        const char* nm = pathName(k);
        int nx = ex - (int)strlen(nm) * 3;
        if (nx < 2) nx = 2;
        if (nx + (int)strlen(nm) * 6 > W - 2) nx = W - 2 - (int)strlen(nm) * 6;
        textT(t, nx, ey - 12, nm, hot ? Theme::VAPOR_PINK : col, 1);
        if (hot) {   // the contact
            const int mx = cx + (ex - cx) * 72 / 100, my = cy + (ey - cy) * 72 / 100;
            const bool pulse = (now / 160) % 2;
            t.drawRect(mx - 9, my - 9, 18, 18, pulse ? Theme::RED : Theme::VAPOR_PINK);
            t.drawRect(mx - 6, my - 6, 12, 12, Theme::RED);
            char lb[16]; snprintf(lb, sizeof lb, "MOTION %u", (unsigned)p.score);
            const int lw = (int)strlen(lb) * 6 + 4;
            t.fillRect(mx - lw / 2, my + 11, lw, 10, Theme::BG); t.drawRect(mx - lw / 2, my + 11, lw, 10, Theme::RED);
            textT(t, mx, my + 12, lb, Theme::VAPOR_PINK, 1, 1);
        }
    }
    if (big) {   // what the rings mean, along the floor
        static const char* const RL[4] = { "STILL", "MOTION", "MOVING", "WALKING" };
        for (int k = 0; k < 4; k++) {
            const int lw = (int)strlen(RL[k]) * 6 + 4;
            int x = cx - (int)(rad * RINGS[k]); if (x - lw / 2 < 2) x = 2 + lw / 2;
            const int y = cy - (k % 2 ? 12 : 24);
            t.fillRect(x - lw / 2, y - 1, lw, 10, Theme::BG);
            t.drawRect(x - lw / 2, y - 1, lw, 10, k == 1 ? Theme::AMBER : dimc());
            textT(t, x, y, RL[k], k == 1 ? Theme::AMBER : slate(), 1, 1);
        }
    }
    // the word, and what moved
    const uint8_t n = SenseStore::livePaths();
    char line[40];
    if (hotK >= 0) snprintf(line, sizeof line, "%s %s'S PATH", big ? "ON" : "ON", pathName((uint8_t)hotK));
    else snprintf(line, sizeof line, "%s", w.sub);
    if (big) {
        char b[48];
        snprintf(b, sizeof b, "LIVE %u WIFI PATH%s, %u READS/S", (unsigned)n, n == 1 ? "" : "S", live ? (unsigned)s.rate : 0u);
        textT(t, 6, T + 4, b, live ? Theme::GREEN : Theme::AMBER, 1);
        t.drawRect(6, T + 14, 6, 6, Theme::RED); textT(t, 16, T + 14, "MOTION ON A PATH", slate(), 1);
        t.drawFastHLine(6, T + 27, 6, Theme::CYAN); textT(t, 16, T + 24, "LINE = WIFI PATH", slate(), 1);
        textT(t, 6, T + 34, "FARTHER = STRONGER", slate(), 1);
        const int hw = headlineWidth(w.w, 0);
        headline(t, 262 - hw / 2 < 130 ? 130 : 262 - hw / 2, T + 4, w.w, w.c, glitchFor(w, now), 0);
        // Squachy, and what he has to say about it
        char say[40];
        if (hotK >= 0) snprintf(say, sizeof say, "On %s's path!", pathName((uint8_t)hotK));
        else if (!live) snprintf(say, sizeof say, "Can't hear much.");
        else if (s.state == SENSE_STATE_CAL) snprintf(say, sizeof say, "Hold still...");
        else snprintf(say, sizeof say, "All quiet.");
        const int bw = (int)strlen(say) * 6 + 14, bx = W - bw - 6, by = T + 46;
        t.fillRect(bx, by, bw, 20, mixc(Theme::BG, Theme::WHITE, 240));
        t.drawRect(bx, by, bw, 20, Theme::VAPOR_PINK);
        t.fillTriangle(W - 44, by + 20, W - 36, by + 20, W - 38, by + 26, mixc(Theme::BG, Theme::WHITE, 240));
        textT(t, bx + 7, by + 6, say, Theme::BLACK, 1);
        using VP = Squachy::VisitPose;
        Squachy::drawWaving(t, W - 38, by + 92, now, 0.85f, nullptr, false, 0, hotK < 0, 34, false, false, false,
                            hotK >= 0 ? VP::POINT : (s.state == SENSE_STATE_CAL ? VP::CROUCH : VP::NONE));
        // the paths, side by side
        const int px = W - 122, py = cy - 96, pw = 116, ph = 92;
        t.fillRect(px, py, pw, ph, Theme::BG); t.drawRect(px, py, pw, ph, Theme::VAPOR_PURPLE);
        textT(t, px + 5, py + 5, w.w, w.c, 1);
        char sc[6]; snprintf(sc, sizeof sc, "%u", live && s.state >= SENSE_STATE_STILL ? (unsigned)s.score : 0u);
        text(t, px + pw - 5, py + 3, sc, Theme::WHITE, Theme::BG, 2, 2);
        textT(t, px + 5, py + 19, "PATH ACTIVITY", slate(), 1);
        for (uint8_t k = 0; k < SenseStore::PATHS; k++) {
            const SenseStore::Router& p = SenseStore::path(k);
            const int x = px + 6 + k * 27;
            const uint16_t c = !p.live ? dimc() : (pathHot(p) ? Theme::PINK : pathColor(k));
            const int h = p.live ? 3 + (int)(clampf(p.score / 90.0f, 0, 1) * 44) : 1;
            t.fillRect(x, py + 76 - h, 22, h, c);
            char ab[4]; memcpy(ab, pathName(k), 3); ab[3] = 0;
            textT(t, x + 11, py + 80, p.live ? ab : "--", c, 1, 1);
        }
    } else {
        headline(t, 6, T + 3, w.w, w.c, glitchFor(w, now), 1);
        textT(t, 7, T + 33, line, hotK >= 0 ? Theme::VAPOR_PINK : slate(), 1);
        const int px = W - 68, py = T + 4, pw = 66, ph = B - T - 10;
        t.fillRect(px, py, pw, ph, Theme::BG); t.drawRect(px, py, pw, ph, Theme::VAPOR_PURPLE);
        char sc[6]; snprintf(sc, sizeof sc, "%u", live && s.state >= SENSE_STATE_STILL ? (unsigned)s.score : 0u);
        text(t, px + pw / 2, py + 5, sc, Theme::WHITE, Theme::BG, 3, 1);
        textT(t, px + pw / 2, py + 30, "ACTIVITY", slate(), 1, 1);
        for (uint8_t k = 0; k < SenseStore::PATHS; k++) {
            const SenseStore::Router& p = SenseStore::path(k);
            const int y = py + 46 + k * ((ph - 52) / 4);
            const uint16_t c = !p.live ? dimc() : (pathHot(p) ? Theme::PINK : pathColor(k));
            char ab[4]; memcpy(ab, pathName(k), 3); ab[3] = 0;
            textT(t, px + 4, y, p.live ? ab : "--", c, 1);
            if (p.live) t.fillRect(px + 24, y, 2 + (int)(clampf(p.score / 90.0f, 0, 1) * 36), 7, c);
        }
    }
}

// ============================================================================
// SNEAK: the Tab5's game, the same rules
// ============================================================================
enum SPhase : uint8_t { S_IDLE, S_COUNT, S_RUN, S_CAUGHT, S_CLEAN };
// The countdown is also the room reading: hold still for 5 s while the score
// settles, and the last 3 s of it are the player's own baseline. The line to
// stay under is that baseline plus a margin, never below a floor (from the
// Tab5's recordings: an empty room peaks ~26, someone sitting still 33-43,
// walking 65-90).
const uint32_t COUNT_MS = 5000, BASE_FROM_MS = 2000, RUN_MS = 10000, RESULT_MS = 6000;
struct Level { const char* name; uint8_t floor, margin; };
const Level LEVELS[3] = { { "EASY", 70, 40 }, { "NORMAL", 55, 28 }, { "HARD", 42, 18 } };
uint8_t levelTarget(uint8_t level, float base) {
    const float t = base + LEVELS[level].margin;
    const float f = t < LEVELS[level].floor ? LEVELS[level].floor : t;
    return (uint8_t)(f > 95 ? 95 : f);
}
struct SRun { uint8_t peak, level, target, base; };
struct Sneak {
    SPhase  phase = S_IDLE;
    uint32_t at = 0;
    uint8_t level = 1;
    uint8_t peak = 0, target = 55;
    float   base = 0, baseSum = 0; uint16_t baseN = 0;
    uint8_t trace[100] = {0};
    SRun    runs[4] = {};
    uint8_t runN = 0;
    uint32_t caughtAt = 0;
} s_sn;

void sneakStart(uint32_t now) {
    if (s_sn.phase == S_COUNT || s_sn.phase == S_RUN) return;
    s_sn.phase = S_COUNT; s_sn.at = now; s_sn.peak = 0;
    s_sn.baseSum = 0; s_sn.baseN = 0;
    memset(s_sn.trace, 0, sizeof s_sn.trace);
}
void sneakLevel() { if (s_sn.phase != S_COUNT && s_sn.phase != S_RUN) s_sn.level = (uint8_t)((s_sn.level + 1) % 3); }

void sneakStep(uint32_t now) {
    const SenseReport& s = SenseStore::last();
    const bool live = SenseStore::fresh(now) && senseModeLive(s.mode) && s.state >= SENSE_STATE_STILL;
    if (s_sn.phase == S_IDLE && live) {   // a preview of the line, from the room as it is
        s_sn.base += (s.score - s_sn.base) * 0.02f;
        s_sn.target = levelTarget(s_sn.level, s_sn.base);
    }
    if (s_sn.phase == S_COUNT) {
        if (live && now - s_sn.at >= BASE_FROM_MS) { s_sn.baseSum += s.score; s_sn.baseN++; }
        if (s_sn.baseN) s_sn.target = levelTarget(s_sn.level, s_sn.baseSum / s_sn.baseN);
        if (now - s_sn.at >= COUNT_MS) {
            s_sn.base = s_sn.baseN ? s_sn.baseSum / s_sn.baseN : s_sn.base;
            s_sn.target = levelTarget(s_sn.level, s_sn.base);
            s_sn.phase = S_RUN; s_sn.at = now;
        }
    }
    if (s_sn.phase == S_RUN) {
        const uint32_t into = now - s_sn.at;
        if (live && s.score > s_sn.peak) s_sn.peak = s.score;
        const uint32_t slot = into / 100; if (slot < 100 && live) s_sn.trace[slot] = s.score;
        if (s_sn.peak >= s_sn.target || into >= RUN_MS) {
            s_sn.phase = s_sn.peak >= s_sn.target ? S_CAUGHT : S_CLEAN; s_sn.at = now;
            if (s_sn.phase == S_CAUGHT) s_sn.caughtAt = now;
            memmove(s_sn.runs + 1, s_sn.runs, sizeof(SRun) * 3);
            s_sn.runs[0] = { s_sn.peak, s_sn.level, s_sn.target, (uint8_t)s_sn.base };
            if (s_sn.runN < 4) s_sn.runN++;
        }
    }
    if ((s_sn.phase == S_CAUGHT || s_sn.phase == S_CLEAN) && now - s_sn.at >= RESULT_MS) s_sn.phase = S_IDLE;
}
char grade(uint8_t peak, uint8_t target, uint8_t base) {
    if (peak >= target) return 'D';
    const float f = peak <= base ? 0 : (peak - base) / (float)(target - base);
    return f < 0.25f ? 'S' : (f < 0.5f ? 'A' : (f < 0.75f ? 'B' : 'C'));
}

void sneakScreen(TFT_eSPI& t, uint32_t now) {
    sneakStep(now);
    const int W = t.width();
    const bool big = W >= 460;
    const int T = 21;
    const Bar g = barGeom(W, t.height());
    const int B = g.y - 3;
    const SenseReport& s = SenseStore::last();
    const bool live = SenseStore::fresh(now) && senseModeLive(s.mode) && s.state >= SENSE_STATE_STILL;
    int ox = 0;
    if (s_sn.phase == S_CAUGHT && now - s_sn.caughtAt < 500) { const int k = 5 - (int)((now - s_sn.caughtAt) / 100); ox = (int)(hash32(now / 30) % (2 * k + 1)) - k; }
    // the corridor
    const int vx = W / 2 + ox, hz = T + (B - T) * 38 / 100;
    const int wx = W * 18 / 100;
    const uint16_t wall = mixc(Theme::BG, Theme::PURPLE, 40), seam = mixc(Theme::BG, Theme::PURPLE, 90);
    t.fillTriangle(0, T, vx - wx, hz, 0, B, wall);
    t.fillTriangle(W - 1, T, vx + wx, hz, W - 1, B, wall);
    for (int k = 1; k < 5; k++) {
        const int xl = (vx - wx) * k / 5, xr = W - 1 - (W - 1 - vx - wx) * k / 5;
        const int yt = T + (hz - T) * k / 5, yb = B - (B - hz) * k / 5;
        t.drawLine(xl, yt, xl, yb, seam); t.drawLine(xr, yt, xr, yb, seam);
        const uint16_t lc = mixc(Theme::BG, k % 2 ? Theme::CYAN : Theme::VAPOR_PINK, 150 + (int)(80 * sinf(now / 300.0f + k)));
        t.fillRect(xl + 2, (yt + yb) / 2 - 6, 2, 12, lc); t.fillRect(xr - 4, (yt + yb) / 2 - 6, 2, 12, lc);
    }
    const uint16_t grid = mixc(Theme::BG, Theme::PURPLE, 130);
    for (int k = -10; k <= 10; k++) t.drawLine(vx + k * W / 45, hz, vx + k * W / 6, B, grid);
    for (int k = 0; k < 8; k++) { const float f = (k + (now % 700) / 700.0f) / 8.0f; t.drawFastHLine(0, hz + (int)(f * f * (B - hz)), W, grid); }
    for (int k = 0; k < 3; k++) {   // ceiling lamps
        const int x = W * (32 + k * 18) / 100 + ox;
        t.fillRect(x - (big ? 14 : 9), T + 1, big ? 28 : 18, big ? 4 : 3, mixc(Theme::BG, Theme::WHITE, 170));
    }
    // the lasers, pulsing faster as the room gets louder
    {
        const float rate = 0.003f + (live ? s.score : 0) / 5000.0f;
        static const float DEPTH[3] = { 0.45f, 0.66f, 0.88f };
        for (int b = 0; b < 3; b++) {
            const float d = DEPTH[b];
            const int y = hz + (int)(d * d * (B - hz));
            const int pulse = (int)(110 + 140 * fabsf(sinf(now * rate * 2 + b)));
            const int x0 = (big ? 40 : 20) + ox, x1 = W - (big ? 40 : 20) + ox, y1 = y - (b ? 6 : 3);
            const bool hit = s_sn.phase == S_CAUGHT && b == 1;
            t.drawLine(x0, y, x1, y1, hit ? Theme::RED : mixc(Theme::BG, Theme::PINK, pulse));
            if (big || hit) t.drawLine(x0, y + 1, x1, y1 + 1, hit ? Theme::RED : mixc(Theme::BG, Theme::PINK, pulse / 2));
            t.fillRect(x0 - 2, y - 2, 4, 4, Theme::PINK); t.fillRect(x1 - 2, y1 - 2, 4, 4, Theme::PINK);
        }
    }
    // the run's trace along the floor
    {
        const int x0 = W * 30 / 100, x1 = W * 70 / 100, fy = B - (big ? 30 : 22);
        int px = -1, py = 0;
        for (int i = 0; i < 100; i++) {
            const int v = s_sn.phase == S_IDLE ? (int)(4 + 3 * sinf(now / 250.0f + i * 0.6f)) : s_sn.trace[i];
            const int x = x0 + (x1 - x0) * i / 99, y = fy - v * (big ? 26 : 18) / 100;
            if (px >= 0) t.drawLine(px, py, x, y, Theme::CYAN);
            px = x; py = y;
        }
    }
    // the header: level, the word, time
    char b[32];
    const Level& L = LEVELS[s_sn.level];
    snprintf(b, sizeof b, "LEVEL %s", L.name);
    textT(t, 6, T + 4, b, Theme::VAPOR_PINK, big ? 2 : 1);
    const uint32_t into = now - s_sn.at;
    if (s_sn.phase == S_RUN) snprintf(b, sizeof b, "TIME %.1f s", (RUN_MS - (into > RUN_MS ? RUN_MS : into)) / 1000.0f);
    else if (s_sn.phase == S_COUNT) snprintf(b, sizeof b, "READY IN %lu", (unsigned long)((COUNT_MS - (into > COUNT_MS ? COUNT_MS : into)) / 1000 + 1));
    else snprintf(b, sizeof b, "TIME %.1f s", RUN_MS / 1000.0f);
    textT(t, W - 6, T + 4, b, Theme::CYAN, big ? 2 : 1, 2);
    const char* word = s_sn.phase == S_CAUGHT ? "CAUGHT!" : s_sn.phase == S_CLEAN ? "CLEAR!" : "SNEAK!";
    const uint16_t wc = s_sn.phase == S_CAUGHT ? Theme::PINK : s_sn.phase == S_CLEAN ? Theme::GREEN : Theme::CYAN;
    const uint8_t hs = big ? 0 : 1;
    headline(t, W / 2 - headlineWidth(word, hs) / 2, T + (big ? 14 : 12), word, wc, 0, hs);
    if (big) {   // the room's heartbeat: how close to the line it is right now
        const int lvl = live ? clampf(s.score * 100 / (s_sn.target ? s_sn.target : 1), 0, 100) : 0;
        textT(t, 6, T + 24, "[CAUTION]", lvl > 70 ? Theme::RED : Theme::AMBER, 1);
        t.fillRect(64, T + 25, 80, 6, mixc(Theme::BG, Theme::RED, 50));
        t.fillRect(64, T + 25, lvl * 80 / 100, 6, Theme::RED);
    }
    // the meter: the score now, the line, the peak
    const int mx = big ? 90 : 24, mw = W - mx * 2, my = T + (big ? 52 : 44), seg = big ? 24 : 20, sw = mw / seg, mh = big ? 12 : 9;
    const int scoreNow = (s_sn.phase == S_RUN || s_sn.phase == S_COUNT) && live ? s.score : (s_sn.phase == S_IDLE ? 0 : s_sn.peak);
    for (int i = 0; i < seg; i++) {
        const int v = (i + 1) * 100 / seg;
        const uint16_t col = v > s_sn.target ? Theme::RED : (v > s_sn.target * 3 / 4 ? Theme::AMBER : Theme::GREEN);
        t.fillRect(mx + i * sw, my, sw - 2, mh, v <= scoreNow ? col : mixc(Theme::BG, col, 50));
    }
    t.fillRect(mx + s_sn.target * seg / 100 * sw - 1, my - 3, 2, mh + 6, Theme::VAPOR_PINK);
    if (s_sn.peak) t.fillRect(mx + s_sn.peak * seg / 100 * sw - 2, my - 2, 2, mh + 4, Theme::VAPOR_YELLOW);
    snprintf(b, sizeof b, "PEAK %u", (unsigned)s_sn.peak);
    textT(t, mx, my + mh + 4, b, Theme::WHITE, big ? 2 : 1);
    snprintf(b, sizeof b, "STAY UNDER %u", (unsigned)s_sn.target);
    textT(t, mx + mw, my + mh + 4, b, Theme::VAPOR_PINK, big ? 2 : 1, 2);
    const char* say = !SenseStore::fresh(now) || !senseModeLive(s.mode) ? "WAITING FOR THE ROUTER"
                    : s.state == SENSE_STATE_CAL ? "CALIBRATING: HOLD STILL"
                    : s.state == SENSE_STATE_LOWDATA ? "LOW SIGNAL: NO RUN UNTIL THE ROOM IS HEARD"
                    : s_sn.phase == S_IDLE ? (s_L->btOn ? "TAP START RUN" : "PRESS START RUN")
                    : s_sn.phase == S_COUNT ? "HOLD STILL: READING THE ROOM"
                    : s_sn.phase == S_RUN ? "GO! CROSS THE ROOM"
                    : s_sn.phase == S_CAUGHT ? "THE ROOM SAW YOU" : "NOT A RIPPLE";
    textT(t, W / 2, my + mh + (big ? 24 : 16), say, s_sn.phase == S_RUN ? Theme::AMBER : slate(), 1, 1);
    // the last runs
    {
        const int hw = big ? 122 : 80, hh = big ? 98 : 64, hx = W - hw - 6, hy = B - hh - 2;
        t.fillRect(hx, hy, hw, hh, Theme::BG); t.drawRect(hx, hy, hw, hh, slate());
        text(t, hx + hw / 2, hy + 4, "LAST RUNS", Theme::VAPOR_YELLOW, Theme::BG, big ? 2 : 1, 1);
        if (!s_sn.runN) textT(t, hx + hw / 2, hy + hh / 2 + 4, "none yet", slate(), 1, 1);
        for (uint8_t k = 0; k < s_sn.runN; k++) {
            const SRun& r = s_sn.runs[k];
            const bool ok = r.peak < r.target;
            const uint16_t c = ok ? Theme::GREEN : Theme::RED;
            const int y = hy + (big ? 24 : 16) + k * (big ? 18 : 12);
            const char gr[2] = { grade(r.peak, r.target, r.base), 0 };
            const char lv[2] = { LEVELS[r.level].name[0], 0 };
            textT(t, hx + 6, y, gr, c, big ? 2 : 1);
            textT(t, hx + (big ? 30 : 18), y, lv, c, big ? 2 : 1);
            snprintf(b, sizeof b, "%u", (unsigned)r.peak);
            textT(t, hx + hw - 6, y, b, c, big ? 2 : 1, 2);
        }
    }
    using VP = Squachy::VisitPose;
    const VP pose = s_sn.phase == S_CAUGHT ? VP::STARTLED : s_sn.phase == S_CLEAN ? VP::CHEER
                  : s_sn.phase == S_COUNT ? VP::CROUCH : s_sn.phase == S_RUN ? VP::LOOK_AROUND : VP::NONE;
    Squachy::drawWaving(t, W * 47 / 100 + ox, B - 2, now, big ? 0.9f : 0.6f, nullptr, false, 0, s_sn.phase == S_IDLE, 34, false, false, false, pose);
}

// ============================================================================
// VITALS: the Tab5's SPECTRUM LAB
// ============================================================================
void fmtClock(char* b, size_t n, float sec) { const int s = sec < 0 ? 0 : (int)sec; snprintf(b, n, "%d:%02d", s / 60, s % 60); }

void vitalsScreen(TFT_eSPI& t, uint32_t now) {
    Vitals::update(now);
    const Vitals::Metrics& m = Vitals::metrics();
    const int W = t.width();
    const bool big = W >= 460;
    const int T = 21;
    const Bar g = barGeom(W, t.height());
    const int B = g.y - 3;
    const SenseReport& s = SenseStore::last();
    const bool joined = SenseStore::fresh(now) && senseModeLive(s.mode);
    const bool breathOk = m.live && !m.motion && m.bpm > 0;
    const bool heartOk = breathOk && m.conf > 0.35f;
    char b[48];
    // the title row
    headline(t, 6, T + 2, "SPECTRUM LAB", Theme::CYAN, 0, 1);
    const int tw = headlineWidth("SPECTRUM LAB", 1);
    fmtClock(b, sizeof b, m.session); char rec[16]; snprintf(rec, sizeof rec, "REC %s", b);
    textT(t, 6 + tw + 10, T + (big ? 9 : 6), rec, ((now / 600) % 2) ? Theme::RED : mixc(Theme::BG, Theme::RED, 140), 1);
    if (big) {
        int x = 6 + tw + 10 + (int)strlen(rec) * 6 + 12;
        auto chip = [&](const char* sx, bool on, uint16_t c) {
            const int cw = (int)strlen(sx) * 6 + 8;
            t.fillRect(x, T + 5, cw, 12, on ? c : Theme::BG); t.drawRect(x, T + 5, cw, 12, c);
            textT(t, x + 4, T + 7, sx, on ? Theme::BG : c, 1);
            x += cw + 4;
        };
        chip("BREATH LOCK", breathOk && m.snr >= 12, Theme::CYAN);
        chip("HEART LOCK", heartOk, Theme::VAPOR_PINK);
    }
    textT(t, W - 4, T + (big ? 24 : 6), !joined ? "WAITING FOR WIFI" : s.mode == SENSE_MODE_PARK ? (big ? "NO WIFI: LISTENING" : "LISTENING") : (big ? "JOINED: PINGING ROUTER" : "PINGING ROUTER"),
          joined ? Theme::GREEN : Theme::AMBER, 1, 2);

    // left: the breathing spectrum over its waterfall
    const int lx = 4, lw = big ? 272 : 172;
    const int sy = T + (big ? 36 : 30), sh = big ? 54 : 28;
    t.drawRect(lx, sy, lw, sh, Theme::PURPLE);
    const float* sp = Vitals::spectrum();
    {
        int px = lx + 1, py = sy + sh - 2;
        for (int i = 0; i < lw - 2; i++) {
            const int bi = i * Vitals::BINS / (lw - 2);
            const int h = (int)(clampf(sp[bi], 0, 1) * (sh - (big ? 14 : 12)));
            if (h > 0) t.drawFastVLine(lx + 1 + i, sy + sh - 2 - h, h, mixc(Theme::BG, Theme::PINK, 70));
            const int y = sy + sh - 2 - h;
            if (i) t.drawLine(px, py, lx + 1 + i, y, Theme::VAPOR_PINK);
            px = lx + 1 + i; py = y;
        }
    }
    if (big) { textT(t, lx + 4, sy + 3, "BREATHING", Theme::CYAN, 1); textT(t, lx + lw * 55 / 100, sy + 3, "HEART", Theme::VAPOR_PINK, 1); }
    else textT(t, lx + lw - 4, sy + 3, "BREATHING", Theme::CYAN, 1, 2);
    auto binX = [&](float hz) { return lx + 1 + (int)(hz / (Vitals::binHz(1) * Vitals::BINS) * (lw - 2)); };
    if (breathOk && m.peakHz > 0) {
        const int x = binX(m.peakHz), y = big ? sy + 12 : sy + 3;
        t.fillRect(x - 1, y + 2, 3, 3, Theme::CYAN);
        snprintf(b, sizeof b, big ? "%.1f/MIN" : "%.1f", m.bpm); textT(t, x + 5, y, b, Theme::CYAN, 1);
    }
    if (heartOk) {
        const int x = binX(m.hr / 60.0f), y = big ? sy + sh * 58 / 100 : sy + sh - 12;
        t.fillRect(x - 1, y + 3, 3, 3, Theme::VAPOR_PINK);
        snprintf(b, sizeof b, big ? "%.0f/MIN" : "%.0f", m.hr); textT(t, x + 5, y, b, Theme::VAPOR_PINK, 1);
    }
    // the waterfall: the newest row on top, 0.3 s a row, 0-150 a minute across
    const int wy = sy + sh + (big ? 4 : 3), wh = big ? B - wy - 44 : B - wy - 26;
    t.drawRect(lx, wy, lw, wh, Theme::PURPLE);
    const int rows = Vitals::waterfallRows();
    // One line per band per row, its colour worked out once: a pixel at a
    // time, the full waterfall took the 3.5in from 41 to 17 frames a second
    // over its first minute (bench, 2026-11-17).
    const int ww = lw - 2;
    for (int r = 0; r < wh - 2 && r < rows; r++) {
        const uint8_t* row = Vitals::waterfallRow((uint8_t)r);
        for (int k = 0; k < Vitals::BINS; k++) {
            const uint8_t v = row[k];
            if (v <= 20) continue;
            const int x0 = (k * ww + Vitals::BINS - 1) / Vitals::BINS, x1 = ((k + 1) * ww + Vitals::BINS - 1) / Vitals::BINS;
            if (x1 > x0) t.drawFastHLine(lx + 1 + x0, wy + 1 + r, x1 - x0, ramp(v));
        }
    }
    textT(t, lx + 3, wy + 3, "NOW", slate(), 1);
    snprintf(b, sizeof b, "-%ds", (int)((wh - 2) * 0.3f)); textT(t, lx + 3, wy + wh - 11, b, slate(), 1);
    if (!rows) textT(t, lx + lw / 2, wy + wh / 2 - 4, m.live ? "LISTENING..." : "WAITING", slate(), 1, 1);
    {   // the lab's marks: holds and heart locks, where they happened
        Vitals::Event ev[8];
        const uint8_t ne = Vitals::events(ev, 8);
        int lastLabel = -100;   // two marks a moment apart get one label, not two on top of each other
        for (uint8_t k = 0; k < ne; k++) {
            if (ev[k].kind != Vitals::Ev::HOLD && ev[k].kind != Vitals::Ev::HEART_LOCK) continue;
            const int r = (int)((m.session - ev[k].t) / 0.3f);
            if (r < 0 || r >= wh - 12) continue;
            const bool hold = ev[k].kind == Vitals::Ev::HOLD;
            const uint16_t c = hold ? Theme::AMBER : Theme::VAPOR_PINK;
            t.drawFastHLine(lx + 1, wy + 1 + r, lw - 2, c);
            if (r - lastLabel < 11 && lastLabel - r < 11) continue;
            lastLabel = r;
            if (hold) snprintf(b, sizeof b, "HOLD %u s", (unsigned)ev[k].val);
            else snprintf(b, sizeof b, big ? "HEART LOCK %u" : "HEART %u", (unsigned)ev[k].val);
            textT(t, lx + (big ? 40 : 30), wy + 1 + r - 9 < wy + 2 ? wy + r + 3 : wy + 1 + r - 9, b, c, 1);
        }
    }
    {
        const int ay = wy + wh + 2;
        if (big) { static const char* const AX[6] = { "0", "30", "60", "90", "120", "150" }; for (int i = 0; i < 6; i++) { const int x = lx + 1 + i * (lw - 2) / 5; textT(t, i == 5 ? x - 18 : (i ? x - (int)strlen(AX[i]) * 3 : x), ay, AX[i], slate(), 1); } }
        else { static const char* const AX[4] = { "0", "50", "100", "150" }; for (int i = 0; i < 4; i++) { const int x = lx + 1 + i * (lw - 2) / 3; textT(t, i == 3 ? x - 18 : (i ? x - (int)strlen(AX[i]) * 3 : x), ay, AX[i], slate(), 1); } }
    }

    // the numbers
    const uint16_t dimCyan = breathOk ? Theme::CYAN : mixc(Theme::BG, Theme::CYAN, 110);
    const uint16_t dimPink = heartOk ? Theme::VAPOR_PINK : mixc(Theme::BG, Theme::VAPOR_PINK, 110);
    char bp[8], hr[8];
    if (!m.live || m.bpm <= 0) snprintf(bp, sizeof bp, "--"); else if (m.motion) snprintf(bp, sizeof bp, "%.0f", m.bpm); else snprintf(bp, sizeof bp, "%.1f", m.bpm);
    if (heartOk) snprintf(hr, sizeof hr, "%.0f", m.hr); else snprintf(hr, sizeof hr, "--");
    char sure[16]; if (heartOk) snprintf(sure, sizeof sure, "%d%% SURE", (int)(m.conf * 100)); else snprintf(sure, sizeof sure, "NOT YET");
    auto loop = [&](int lcx, int lcy, int lr) {   // the breath loop: the wave against its own recent past
        t.drawCircle(lcx, lcy, lr, Theme::PURPLE);
        t.drawFastHLine(lcx - lr, lcy, lr * 2, mixc(Theme::BG, Theme::PURPLE, 110)); t.drawFastVLine(lcx, lcy - lr, lr * 2, mixc(Theme::BG, Theme::PURPLE, 110));
        textT(t, lcx + lr - 10, lcy - lr - 2, "IN", slate(), 1); textT(t, lcx - lr - 2, lcy + lr - 6, "OUT", slate(), 1);
        if (!m.live) return;
        int px = -1, py = 0;
        for (int k = 60; k >= 0; k -= 2) {
            const float a = Vitals::breathAt((uint16_t)k), d = a - Vitals::breathAt((uint16_t)(k + 12));
            const int x = lcx + (int)(clampf(a, -1.2f, 1.2f) * lr * 0.7f), y = lcy - (int)(clampf(d, -1.2f, 1.2f) * lr * 0.7f);
            if (px >= 0) t.drawLine(px, py, x, y, mixc(Theme::BG, Theme::VAPOR_PURPLE, 230 - k * 3));
            px = x; py = y;
        }
        t.fillRect(px - 1, py - 1, 3, 3, Theme::WHITE);
    };
    auto wave = [&](int x, int y, int ww, int wh2) {
        t.drawRect(x, y, ww, wh2, Theme::VAPOR_PURPLE);
        int px = -1, py = 0;
        for (int i = 0; i < ww - 2; i++) {
            const uint16_t k = (uint16_t)((ww - 3 - i) * 2);   // newest on the right, 0.1 s a pixel
            const float v = m.live ? clampf(Vitals::breathAt(k), -1, 1) : 0;
            const int yy = y + wh2 / 2 - (int)(v * (wh2 / 2 - 2));
            if (px >= 0) t.drawLine(px, py, x + 1 + i, yy, Vitals::motionAt(k) ? Theme::AMBER : Theme::RED);
            px = x + 1 + i; py = yy;
        }
    };
    char st[4][2][12];
    snprintf(st[0][0], 12, "RHYTHM"); if (m.rhythm >= 0 && breathOk) snprintf(st[0][1], 12, "%d%%", m.rhythm); else snprintf(st[0][1], 12, "--");
    snprintf(st[1][0], 12, "DEPTH");  if (m.depth >= 0 && breathOk) snprintf(st[1][1], 12, "%d%%", m.depth); else snprintf(st[1][1], 12, "--");
    snprintf(st[2][0], 12, "IN:EX");  if (m.ie > 0 && breathOk) snprintf(st[2][1], 12, "1:%.1f", m.ie); else snprintf(st[2][1], 12, "--");
    snprintf(st[3][0], 12, "STILL");  fmtClock(st[3][1], 12, m.live ? m.still : 0);
    // the lab log: the newest few moments
    char log[64] = "";
    {
        Vitals::Event ev[3];
        const uint8_t ne = Vitals::events(ev, big ? 3 : 1);
        int n = 0;
        for (uint8_t k = 0; k < ne && n < (int)sizeof log - 24; k++) {
            char c[8]; fmtClock(c, sizeof c, ev[k].t);
            switch (ev[k].kind) {
                case Vitals::Ev::HOLD:       n += snprintf(log + n, sizeof log - n, "%s%s HOLD %u s", k ? "  " : "", c, (unsigned)ev[k].val); break;
                case Vitals::Ev::HEART_LOCK: n += snprintf(log + n, sizeof log - n, "%s%s HEART LOCK %u", k ? "  " : "", c, (unsigned)ev[k].val); break;
                case Vitals::Ev::SIGH:       n += snprintf(log + n, sizeof log - n, "%s%s SIGH", k ? "  " : "", c); break;
                default:                     n += snprintf(log + n, sizeof log - n, "%s%s MOVED", k ? "  " : "", c); break;
            }
        }
        if (!n) snprintf(log, sizeof log, "%s", !m.live ? "WARMING UP: SIT STILL ABOUT A METRE AWAY" : "LISTENING");
    }
    using VP = Squachy::VisitPose;
    const VP pose = m.motion ? VP::STARTLED : heartOk ? VP::SALUTE : VP::NONE;
    if (big) {
        const int rx = 286, ry = T + 36;
        textT(t, rx, ry, "BREATHS", Theme::CYAN, 2);
        bigNum(t, rx, ry + 16, bp, dimCyan, 0);
        textT(t, rx, ry + 56, breathOk && m.snr >= 12 ? "/MIN  LOCKED" : "/MIN", slate(), 1);
        const int hx = rx + 100;
        textT(t, hx, ry, "HEART", Theme::VAPOR_PINK, 2); textT(t, hx + 64, ry + 4, "EXP", slate(), 1);
        headline(t, hx, ry + 16, hr, dimPink, 0, 0);
        textT(t, hx, ry + 56, sure, slate(), 1);
        const int lcy = ry + 98;
        loop(rx + 40, lcy, 26);
        textT(t, rx + 40, lcy + 30, "BREATH LOOP", Theme::VAPOR_PURPLE, 1, 1);
        // the heart lock ring
        const int hcx = rx + 136;
        arc(t, hcx, lcy, 26, 0, 6.2832f, mixc(Theme::BG, Theme::VAPOR_PINK, 70), 4);
        arc(t, hcx, lcy, 26, -1.5708f, -1.5708f + 6.2832f * clampf(m.conf, 0.03f, 1), Theme::VAPOR_PINK, 4);
        const bool beat = heartOk && sinf(now / 1000.0f * m.hr / 60.0f * 6.2832f) > 0.6f;
        t.fillRect(hcx - 3, lcy - 8, 7, 6, beat ? Theme::RED : mixc(Theme::BG, Theme::RED, 140));
        textT(t, hcx, lcy + 2, heartOk ? "LOCKED" : "WAIT", Theme::VAPOR_PINK, 1, 1);
        textT(t, hcx, lcy + 30, "HEART LOCK", Theme::VAPOR_PINK, 1, 1);
        const int wy2 = ry + 142;
        wave(rx, wy2, W - rx - 6, 16);
        for (int i = 0; i < 4; i++) { const int x = rx + (i % 2) * 74, y = wy2 + 22 + (i / 2) * 12; textT(t, x, y, st[i][0], slate(), 1); textT(t, x + 70, y, st[i][1], Theme::WHITE, 1, 2); }
        const int ly = B - 24;
        t.fillRect(0, ly, W, 24, mixc(Theme::BG, Theme::AMBER, 20)); t.drawFastHLine(0, ly, W, Theme::AMBER);
        snprintf(b, sizeof b, "LAB LOG  %s", log); textT(t, 6, ly + 8, b, Theme::AMBER, 1);
        Squachy::drawWaving(t, W - 30, ly + 2, now, 0.55f, nullptr, false, 0, false, 34, false, false, false, pose);
    } else {
        const int rx = 182, ry = T + 30;
        textT(t, rx, ry, "BREATHS", Theme::CYAN, 1);
        bigNum(t, rx, ry + 8, bp, dimCyan, 1);
        textT(t, rx, ry + 36, breathOk && m.snr >= 12 ? "/MIN LOCKED" : "/MIN", slate(), 1);
        loop(W - 22, ry + 20, 16);
        const int hy = ry + 46;
        textT(t, rx, hy, "HEART", Theme::VAPOR_PINK, 1); textT(t, rx + 34, hy, "EXP", slate(), 1);
        headline(t, rx, hy + 8, hr, dimPink, 0, 1);
        textT(t, rx, hy + 36, sure, slate(), 1);
        Squachy::drawWaving(t, W - 22, hy + 44, now, 0.3f, nullptr, false, 0, false, 34, false, false, false, pose);
        const int wy2 = hy + 50;
        wave(rx, wy2, W - rx - 4, 10);
        for (int i = 0; i < 4; i++) { const int y = wy2 + 14 + i * 9; textT(t, rx, y, st[i][0], slate(), 1); textT(t, W - 6, y, st[i][1], Theme::WHITE, 1, 2); }
        const int ly = B - 12;
        t.fillRect(0, ly, W, 12, mixc(Theme::BG, Theme::AMBER, 20)); t.drawFastHLine(0, ly, W, Theme::AMBER);
        snprintf(b, sizeof b, "LOG %s", log); textT(t, 4, ly + 3, b, Theme::AMBER, 1);
    }
    // the word for when it cannot read, over the waterfall
    if (!breathOk) {
        const char* wd = !joined ? "WAITING" : m.motion ? "SIT STILL" : "WARMING UP";
        const uint16_t c = m.motion ? Theme::PINK : Theme::AMBER;
        const int hw = headlineWidth(wd, 1), cxw = lx + lw / 2, cyw = wy + wh / 2 - 14;
        t.fillRect(cxw - hw / 2 - 10, cyw - 4, hw + 20, 36, Theme::BG); t.drawRect(cxw - hw / 2 - 10, cyw - 4, hw + 20, 36, c);
        headline(t, cxw - hw / 2, cyw, wd, c, 0, 1);
    }
}
