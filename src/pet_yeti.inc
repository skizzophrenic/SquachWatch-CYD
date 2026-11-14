// SquachWatch-CYD — the yeti pet's dances and his throw. Included by pet.cpp,
// after the toaster and the cell block, so it has the shared grip, the throw
// counter and the balloon.
//
// From the Yeti Lab artifact (2026-10-11), each at its third pass:
//
//   SATURDAY NIGHT YETI  the disco point: a paw to the ceiling, then down
//                        across his body, hips popping, switching paws every
//                        four points, over a three-tile floor that lights up
//                        pink, cyan and yellow, with a glint at the top.
//   THE FLOSS            arms across his body one way, hips the other, one
//                        arm behind him and one in front, speeding up for
//                        four seconds, then arms up with sweat flying.
//   THROWABLE YETI       picked up he dangles, arms up; flung he lands
//                        face-down in a puff of dust with one small bounce
//                        ("OOF."), gets up, stomps twice ("NOT TOY.") and
//                        walks off.
//
// He dances about one visit in six instead of saying a line, and whenever he
// is standing beside Squachy while Squachy dances. Every pose is a
// Theme::YetiRig: the yeti's own rectangles, positioned by these numbers.

static uint8_t  s_yDance = 0;               // 0 disco, 1 floss
static bool     s_yDanceVisit = false;      // this visit he dances instead of talking
static int8_t   s_yForce = -1;              // the console's YETI DISCO / FLOSS: the next visit, now
static const uint32_t YETI_DISCO_MS = 6000, YETI_FLOSS_MS = 5200;

static Grip     s_yG;
static YPhase   s_yBefore = YPhase::AWAY;   // what he was doing when a finger landed on him
static Fling    s_yF;                 // centre and feet, off the ground
static uint32_t s_yLandAt = 0;
static uint8_t  s_yBounces = 0;
static bool     s_yFlung = false, s_yDust = false;
static int      s_yFloor = 0;               // last frame's floor, for the hit test
static const float    YETI_G = 0.0028f;     // C1iPPY's and the toaster's gravity
static const uint32_t YETI_FLOP_MS = 1700, YETI_STOMP_MS = 1300;

static bool yetiAirborne() { return s_yPhase == YPhase::HELD || s_yPhase == YPhase::AIR ||
                                    s_yPhase == YPhase::FLOPPED || s_yPhase == YPhase::STOMP; }

static float yetiEase(float u) {
    if (u <= 0.0f) return 0.0f;
    if (u >= 1.0f) return 1.0f;
    return u < 0.5f ? 2.0f * u * u : 1.0f - 2.0f * (1.0f - u) * (1.0f - u);
}
static void yetiArmsMix(int8_t out[2][4], const int8_t a[2][4], const int8_t b[2][4], float u) {
    for (int i = 0; i < 2; i++)
        for (int j = 0; j < 4; j++) out[i][j] = (int8_t)lroundf((float)a[i][j] + (float)(b[i][j] - a[i][j]) * u);
}
// The same pose with the other paw doing the pointing.
static void yetiArmsMirror(int8_t out[2][4], const int8_t in[2][4]) {
    out[0][0] = (int8_t)-in[1][0]; out[0][1] = in[1][1]; out[0][2] = (int8_t)-in[1][2]; out[0][3] = in[1][3];
    out[1][0] = (int8_t)-in[0][0]; out[1][1] = in[0][1]; out[1][2] = (int8_t)-in[0][2]; out[1][3] = in[0][3];
}
static Theme::YetiRig yetiRig() {
    Theme::YetiRig r;
    memset(&r, 0, sizeof r);
    r.mouth = 4;
    return r;
}
// Snow kicked up by a foot or a landing: n specks thrown out in a half ring.
static void yetiPuff(TFT_eSPI& t, int x, int y, float k, int n) {
    for (int i = 0; i < n; i++) {
        const float a = (n > 1 ? (float)i / (float)(n - 1) : 0.0f) * 3.14159f - 3.14159f;
        const float r = 2.0f + k * 7.0f;
        t.fillRect(x + (int)(cosf(a) * r), y + (int)(sinf(a) * r * 0.6f), 2, 2, (uint16_t)0xDF5F);
    }
}

// ---- SATURDAY NIGHT YETI ------------------------------------------------------
static void yetiDisco(TFT_eSPI& t, int x, int baseY, uint32_t e, uint32_t now) {
    static const int8_t UP[2][4]   = { { -15, -30, -9, -24 }, { 14, -48, 24, -66 } };
    static const int8_t DOWN[2][4] = { { -15, -30, -9, -24 }, { 10, -36, -14, -22 } };
    const uint32_t beat = e / 500;
    const int  k    = (int)(beat % 2);
    const int  side = ((e / 2000) % 2) ? -1 : 1;
    const float u   = yetiEase((float)(e % 500) / 120.0f);
    int8_t up[2][4], dn[2][4];
    if (side > 0) { memcpy(up, UP, sizeof up); memcpy(dn, DOWN, sizeof dn); }
    else          { yetiArmsMirror(up, UP); yetiArmsMirror(dn, DOWN); }
    Theme::YetiRig r = yetiRig();
    yetiArmsMix(r.arms, k ? up : dn, k ? dn : up, u);
    r.hop  = (e % 500) < 100 ? 1 : 0;
    r.lean = (int8_t)((k ? 3 : -3) * side);
    // The light-up floor, under his feet: three tiles stepping through the
    // colours on the beat.
    static const uint16_t TILE[3] = { 0xF81F, 0x07FF, 0xFFE0 };
    for (int i = 0; i < 3; i++) t.fillRect(x - 17 + i * 12, baseY - 1, 10, 2, TILE[(i + beat) % 3]);
    Theme::drawYetiRig(t, x, baseY, now, r);
    // A glint on the paw at the top of the point.
    if (k == 0 && u >= 1.0f) {
        const int8_t* h = r.arms[side > 0 ? 1 : 0];
        const int hx = x + h[2] + r.lean, hy = baseY - r.hop + h[3];
        t.fillRect(hx - 1, hy - 5, 2, 3, 0xFFFF);
        t.fillRect(hx - 3, hy - 4, 6, 1, 0xFFFF);
    }
}

// ---- THE FLOSS ------------------------------------------------------------------
static void yetiFloss(TFT_eSPI& t, int x, int baseY, uint32_t e, uint32_t now) {
    Theme::YetiRig r = yetiRig();
    if (e >= 4000) {
        // Ta-da: arms up, mouth open, sweat off the top of his head.
        static const int8_t TADA[2][4] = { { -16, -46, -24, -60 }, { 16, -46, 24, -60 } };
        memcpy(r.arms, TADA, sizeof TADA);
        r.mouth = 6;
        Theme::drawYetiRig(t, x, baseY, now, r);
        const float u = (float)(e - 4000) / 1200.0f;
        for (int d = -1; d <= 1; d += 2)
            t.fillRect(x + (int)((float)d * (8.0f + u * 14.0f)), baseY - 58 - (int)(u * 10.0f) + (int)(u * u * 18.0f), 2, 3, (uint16_t)0x7EBF);
        return;
    }
    // Speeding up: the swing's frequency climbs from one per 520 ms to one per
    // 260 ms over the four seconds, so its phase is the integral of that.
    const float ms = (float)e;
    const float ph = ms / 520.0f + (1.0f / 260.0f - 1.0f / 520.0f) * ms * ms / 8000.0f;
    const float s  = sinf(6.2831853f * ph);
    const int8_t a = (int8_t)lroundf(6.0f * s), b = (int8_t)lroundf(12.0f * s);
    const int8_t arms[2][4] = { { (int8_t)(-10 + a), -30, (int8_t)(-8 + b), -18 },
                                { (int8_t)( 10 + a), -30, (int8_t)( 8 + b), -18 } };
    memcpy(r.arms, arms, sizeof arms);
    r.hip    = (int8_t)-lroundf(3.0f * s);
    r.behind = s > 0.0f ? 1 : -1;            // one arm behind him, swapping each swing
    Theme::drawYetiRig(t, x, baseY, now, r);
}

static void yetiDanceDraw(TFT_eSPI& t, int x, int baseY, uint32_t now) {
    const uint32_t e = now - s_yAt;
    if (s_yDance) yetiFloss(t, x, baseY, e, now);
    else          yetiDisco(t, x, baseY, e, now);
}

// ---- THROWABLE YETI -----------------------------------------------------------
static bool yetiHit(int x, int y) {
    if (s_yPhase == YPhase::AWAY) return false;
    const bool off = yetiAirborne();
    const float cx = off ? s_yF.x : s_yX;
    const float fy = off ? s_yF.y : (float)s_yFloor;
    return (float)x >= cx - 16.0f && (float)x <= cx + 16.0f && (float)y >= fy - 58.0f && (float)y <= fy + 2.0f;
}
static bool yetiCenter(int& x, int& y) {
    if (s_yPhase == YPhase::AWAY) return false;
    const bool off = yetiAirborne();
    x = (int)(off ? s_yF.x : s_yX);
    y = (int)(off ? s_yF.y : (float)s_yFloor) - 28;
    return true;
}
// A tap makes him dance -- the other one, if he is already dancing. Tapping
// things is what everybody does first, so this is how the dances get found.
static uint8_t s_yNextDance = 0;
static void yetiPoke(uint32_t now) {
    s_yFlinchAt = now ? now : 1;
    if (s_yPhase == YPhase::IN || s_yPhase == YPhase::TALK || s_yPhase == YPhase::NAP ||
        s_yPhase == YPhase::OUT || s_yPhase == YPhase::DANCE) {
        s_yDance = (s_yPhase == YPhase::DANCE) ? (uint8_t)(1 - s_yDance) : s_yNextDance;
        s_yNextDance ^= 1;
        s_yPhase = YPhase::DANCE;
        s_yAt = now;
        s_yAnswered = false;
        Settings::markYetiFound(2);
    }
}
static void yetiGrab(int x, int y, uint32_t now) {
    if (s_yPhase == YPhase::AWAY || s_yPhase == YPhase::HELD) return;
    if (!yetiAirborne()) {
        s_yBefore = s_yPhase;
        s_yF.x = s_yX; s_yF.y = (float)s_yFloor;
    }
    s_yPhase = YPhase::HELD;
    gripStart(s_yG, x, y, (int)s_yF.x, (int)s_yF.y, now);
}
static void yetiDrag(int x, int y, uint32_t now) {
    if (s_yPhase != YPhase::HELD) return;
    gripMove(s_yG, x, y, now);
}
static void yetiLaunch(float vx, float vy, uint32_t now) {
    flingLaunch(s_yF, vx, vy, now);
    s_yPhase = YPhase::AIR;
    s_yAt = now;
    s_yBounces = 0;
}
static void yetiRelease(uint32_t now) {
    if (s_yPhase != YPhase::HELD) return;
    float vx, vy;
    switch (gripLetGo(s_yG, now, vx, vy)) {
        case 0:   // a tap: he was never really picked up, and it made him jump
            if (s_yBefore == YPhase::AIR || s_yBefore == YPhase::FLOPPED || s_yBefore == YPhase::STOMP) { yetiLaunch(0, 0, now); s_yFlung = false; }
            else { s_yPhase = s_yBefore; s_yAt = now; s_yX = s_yF.x; }
            yetiPoke(now);
            break;
        case 1:
            yetiLaunch(vx, vy, now); s_yFlung = true;
            noteThrow(now); Sound::play(Sound::Id::THROW);
            Settings::markYetiFound(1);
            break;
        default:  // let go slowly, or held still: he drops from there
            yetiLaunch(0, 0, now); s_yFlung = false;
            break;
    }
}

// Everything off his feet: carried, flying, face-down, stomping off. Moves
// him, draws him, and says his piece. Hands him back to the walk-off once he
// is on his feet again.
static void yetiFlightTick(TFT_eSPI& t, uint32_t now, int screenW, int floorY) {
    const float leftX = 16.0f, rightX = (float)(screenW - 16), ceilY = 62.0f;
    const float floorF = (float)floorY;
    if (s_yPhase == YPhase::HELD) {
        flingHold(s_yF, s_yG, leftX, rightX, ceilY, floorF);
        if (now - s_yG.gT > 600 && s_yG.moved) { yetiLaunch(0, 0, now); s_yFlung = false; }
    } else if (s_yPhase == YPhase::AIR) {
        uint32_t dt = flingDt(s_yF, now);
        while (dt && s_yPhase == YPhase::AIR) {
            const float st = flingTake(dt);
            if (flingStep(s_yF, st, YETI_G, leftX, rightX, ceilY, floorF, 0.4f, 0.3f)) {
                if (s_yBounces == 0) {
                    s_yLandAt = now ? now : 1;
                    s_yDust = s_yFlung;
                    if (s_yFlung) { zLandedAt((int)s_yF.x); Sound::play(Sound::Id::LAND_YETI); }
                }
                // Heavy: one small bounce at most, and he does not slide.
                if (s_yF.vy > 0.35f && s_yBounces < 1) {
                    s_yF.vy = -s_yF.vy * 0.2f;
                    s_yF.vx *= 0.4f;
                    s_yBounces++;
                } else {
                    s_yF.vy = 0;
                    s_yAt = now;
                    if (s_yFlung) s_yPhase = YPhase::FLOPPED;
                    else { s_yX = s_yF.x; s_yPhase = YPhase::OUT; }   // set down: he walks off
                }
            }
        }
        if (s_yPhase == YPhase::AIR && now - s_yAt > 5000) { s_yF.y = floorF; s_yX = s_yF.x; s_yPhase = YPhase::OUT; s_yAt = now; }
    } else if (s_yPhase == YPhase::FLOPPED) {
        if (now - s_yAt >= YETI_FLOP_MS) { s_yPhase = YPhase::STOMP; s_yAt = now; }
    } else if (s_yPhase == YPhase::STOMP) {
        if (now - s_yAt >= YETI_STOMP_MS) { s_yX = s_yF.x; s_yPhase = YPhase::OUT; s_yAt = now; }
    }
    if (s_yPhase == YPhase::OUT) return;          // back on his feet; the walk-off draws him

    const int x  = (int)lroundf(s_yF.x);
    const int fy = (int)lroundf(s_yF.y);
    // The thud: a pixel's shudder for the first moment after he lands.
    const int shake = (s_yDust && s_yLandAt && now - s_yLandAt < 300) ? (int)(((now - s_yLandAt) / 40) % 2) : 0;
    const char* line = nullptr;
    int lineY = fy - Theme::YETI_H - 16;
    switch (s_yPhase) {
        case YPhase::HELD: Theme::drawYeti(t, x, fy, now, Theme::YetiPose::FLINCH); break;
        case YPhase::AIR:  Theme::drawYeti(t, x, fy, now, Theme::YetiPose::WALK);   break;
        case YPhase::FLOPPED: {
            Theme::drawYeti(t, x + shake, fy, now, Theme::YetiPose::NAP);
            const uint32_t g = now - s_yAt;
            if (g >= 100 && g < 1100) { line = "OOF."; lineY = fy - 32; }
            break;
        }
        case YPhase::STOMP: {
            // Two stomps, one foot then the other, fists up, mouth open.
            const uint32_t e = now - s_yAt, p = e % 320;
            const int k = (int)((e / 320) % 2);
            Theme::YetiRig r = yetiRig();
            static const int8_t FISTS[2][4] = { { -15, -32, -18, -42 }, { 15, -32, 18, -42 } };
            memcpy(r.arms, FISTS, sizeof FISTS);
            r.mouth = 6;
            const int8_t lift = p < 160 ? 3 : 0;
            r.lLift = k ? 0 : lift;
            r.rLift = k ? lift : 0;
            Theme::drawYetiRig(t, x, fy, now, r);
            if (p > 160 && p < 280) yetiPuff(t, x + (k ? 8 : -8), floorY - 1, (float)(p - 160) / 120.0f, 3);
            if (e > 200) line = "NOT TOY.";
            break;
        }
        default: break;
    }
    if (s_yDust && s_yLandAt && now - s_yLandAt < 500)
        yetiPuff(t, x, floorY - 1, (float)(now - s_yLandAt) / 500.0f, 6);
    if (line) {
        t.setTextSize(1);
        const int bw = t.textWidth(line) + 8;
        int by = lineY;
        if (by < 22) by = 22;
        bubble(t, x - bw / 2, by, screenW, line);
    }
}

// The console's YETI DISCO / YETI FLOSS: he comes on now and dances that one,
// or starts it on the spot if he is already standing there.
void yetiDance(int8_t which) {
    if (which < 0 || which > 1) return;
    if (s_yPhase == YPhase::TALK || s_yPhase == YPhase::IN || s_yPhase == YPhase::NAP) {
        s_yDance = (uint8_t)which; s_yDanceVisit = true;
        if (s_yPhase != YPhase::IN) { s_yPhase = YPhase::DANCE; s_yAt = millis(); s_yAnswered = false; }
        return;
    }
    s_yForce = which;
    s_yNextAt = 1;
}
