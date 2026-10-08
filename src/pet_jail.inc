// SquachWatch-CYD — the cell block: the ball and chain. Included by pet.cpp
// inside namespace Pet, after C1iPPY and T0@$TY, whose throws it counts and
// whose balloon it borrows.
//
// Throw a pet ten times inside a minute and it is taken into custody. Sirens,
// a puff of smoke where the pet stood, and an iron ball drops in with a clank
// and is chained to Squachy's ankle for the ten minutes he serves (the
// sentence itself, the stripes and the yank on a throw live in squachy.cpp).
// The ball has a face, and opinions about people who throw pets.
//
// Lines are three balloon lines of 24 characters at most, 72 in all.
static const char* const BALL_LINES[] = {
    "You threw a pet. Now you've got me.",
    "I'm your pet now. Try throwing me.",
    "Ball and chain. Emphasis on chain.",
    "Nice stripes. They suit you.",
    "Ten minutes. I'll be counting. Out loud.",
    "Your pet's fine. It's in protective custody.",
    "Your pet filed a report. It was mostly crying.",
    "Ten throws a minute is a hobby, not an accident.",
    "Some people collect stamps. You collect charges.",
    "I've met rocks with better manners.",
    "Who throws a toaster? You. That's who.",
    "Fun fact: I don't bounce either.",
    "Rehabilitation starts now. Sit still.",
    "Solid iron. Unlike your alibi.",
    "I have one job. Weighing you down.",
};
static const uint8_t BALL_LINES_N = sizeof(BALL_LINES) / sizeof(BALL_LINES[0]);
static const char* const BALL_ARREST = "Ten throws in one minute? You're under arrest.";
static const char* const BALL_YANK[3] = { "Nope.", "Where do you think you're going?", "Sit." };
static const char* const BALL_POKE[3] = { "Don't touch me. You'll throw me.", "Hands where I can see them.", "Poking counts. I'm writing it down." };
static const char* const BALL_FREE = "Time served. Try petting them this time.";
// Out of custody, chosen as the pet. Same ball, different outlook.
static const char* const BALL_PET_LINES[] = {
    "You chose me. Over a toaster. Bold.",
    "The only pet you can't throw. Think about that.",
    "I don't fetch. I anchor.",
    "Time served. I stayed anyway.",
    "No food. No walks. No mercy.",
    "Parole officer. Unpaid.",
    "Heavy is a lifestyle.",
    "Try to run. I dare you.",
    "Not a ball and chain. A commitment.",
    "No stripes today. I miss the stripes.",
    "Throws this week: zero. Keep it up.",
    "He's rehabilitated. Mostly.",
};
static const uint8_t BALL_PET_LINES_N = sizeof(BALL_PET_LINES) / sizeof(BALL_PET_LINES[0]);
static const char* const BALL_PET_POKE[3] = { "Pet. Not poke. Pet.", "That's it. That's the whole trick.", "I'm iron. Did that feel good?" };
static const char* const BALL_PET_YANK[3] = { "Leash law.", "Short chain. Long memory.", "We go where I go." };
// Squachy, on his choice of companion.
static const char* const PET_SQ[4] = {
    "He followed me home.", "We're not friends. We're attached.",
    "Walkies are... short.", "He's my emotional support anchor.",
};
// Squachy, doing his time.
static const char* const JAIL_SQ[4] = {
    "I was framed. By a toaster.", "Three square meals. All toast.",
    "I want my lawyer. And my pet.", "it was ten throws. ten. tops.",
};

// ---- the throws ---------------------------------------------------------------
// The last ten flings of any pet. The tenth inside a minute of the first is
// the arrest.
static uint32_t s_throwAt[10] = {};
static uint8_t  s_throwN = 0;
static uint32_t s_jArrestAt = 0;          // the arrest's sirens and clank, 0 when not running
static uint32_t s_jFreeAt = 0;            // the walk out, 0 when not running
static const char* s_jLine = nullptr;
static uint32_t s_jSaidAt = 0, s_jNextAt = 0, s_jYankSeen = 0, s_jPokeAt = 0;
static uint8_t  s_jMood = 0;              // 0 talk, 1 laugh, 2 smug
static uint8_t  s_jIx = 0, s_jPoke = 0, s_jSq = 0;
static bool     s_jAnswered = true;
static bool     s_jWasPet = false;        // last frame he was out as the pet, not the jailer
int16_t         s_jX = -100, s_jY = 0, s_jR = 0;   // the ball this frame, for taps
static int      s_jaCx = 0, s_jaHalfW = 0, s_jaBot = 0;
static bool     s_jaValid = false;

static void ballSay(const char* line, uint32_t now, uint8_t mood) {
    s_jLine = line; s_jSaidAt = now ? now : 1; s_jMood = mood; s_jAnswered = false;
}

void arrest(uint32_t now) {
    if (Squachy::jailed() || s_jArrestAt) return;
    Squachy::jailStart();
    s_jArrestAt = now ? now : 1;
    s_jFreeAt = 0;
    s_jLine = nullptr;
    s_jNextAt = now + 60000;
    s_jYankSeen = Squachy::yankedAt();
    s_throwN = 0;
}

static void noteThrow(uint32_t now) {
    if (Squachy::jailed()) return;
    if (s_throwN < 10) s_throwAt[s_throwN++] = now;
    else { for (uint8_t i = 1; i < 10; i++) s_throwAt[i - 1] = s_throwAt[i]; s_throwAt[9] = now; }
    if (s_throwN == 10 && now - s_throwAt[0] <= 60000) arrest(now);
}

static bool jailShowing(uint32_t now) {
    if (s_jFreeAt && now - s_jFreeAt > 4200) s_jFreeAt = 0;
    return Squachy::jailed() || s_jArrestAt || s_jFreeAt;
}

static bool ballHit(int x, int y) {
    if (s_jX < -50) return false;
    return x >= s_jX - s_jR - 4 && x <= s_jX + s_jR + 4 && y >= s_jY - 2 * s_jR - 4 && y <= s_jY + 2;
}
static void ballPoke(uint32_t now) {
    if (now - s_jPokeAt > 8000) s_jPoke = 0;
    s_jPokeAt = now;
    ballSay((Squachy::ballPetOn() ? BALL_PET_POKE : BALL_POKE)[s_jPoke++ % 3], now, 2);
    s_jAnswered = true;
}

// ---- the ball ---------------------------------------------------------------------
// Sitting on the floor at (cx, bottom), radius r. mood: 0 talk, 1 laugh,
// 2 smug, 3 idle, 4 eye roll, 5 squashed.
// Sitting on the floor at (cx, bottom), radius r. mood: 0 talk, 1 laugh,
// 2 smug, 3 idle. Whole-number maths and the library's own circles: the
// first version drew the same ball with square roots and sines a row at a
// time, and cost three times the flash.
__attribute__((noinline)) static void ballDraw(TFT_eSPI& t, int cx, int bottom, int r, uint32_t now, uint8_t mood) {
    const uint16_t BASE = (uint16_t)0x2966, SHADE = (uint16_t)0x10C3, HI = (uint16_t)0x8C94;
    const uint16_t INK = (uint16_t)0xEF7E, PUPIL = (uint16_t)0x0841;
    if (mood == 1) cx += ((now / 70) & 1) ? 1 : -1;
    const int cy = bottom - r, u = r / 5 > 1 ? r / 5 : 1;      // u: one "unit" of his face
    t.drawFastHLine(cx - r + 2, bottom + 1, r * 2 - 4, (uint16_t)0x10A2);
    t.fillCircle(cx, cy, r + 1, (uint16_t)0x0020);
    // the shadow side, then the lit side over it, nudged up and left
    t.fillCircle(cx, cy, r, SHADE);
    t.fillCircle(cx - u / 2, cy - u / 2, r - u, BASE);
    t.fillRect(cx - r / 2, cy - r * 3 / 5, u + 1, u, HI);
    // the ring the chain hangs from
    t.drawCircle(cx - r * 3 / 4, cy - r * 3 / 4, u, (uint16_t)0xAD97);
    // ---- his face ----
    const int ey = cy - r / 4, er = u + 1 > 1 ? u + 1 : 2;
    const bool blink = mood == 3 && (now % 3900) < 140;
    for (int e = -1; e <= 1; e += 2) {
        const int x = cx + e * 2 * er;
        if (blink) { t.drawFastHLine(x - er, ey, er * 2 + 1, INK); continue; }
        t.fillCircle(x, ey, er, INK);
        t.fillRect(x - er + 1, ey, er, er, PUPIL);                // looking at Squachy, on his left
        if (mood == 2) t.fillRect(x - er, ey - er, er * 2 + 1, er, BASE);   // smug: lids half down
        // brows: angry, slanting in, while he watches or talks
        const int by = ey - er - 2;
        if (mood == 0 || mood == 3) t.drawLine(x - er, by - (e < 0 ? 0 : 1), x + er, by - (e < 0 ? 1 : 0), INK);
        else t.drawFastHLine(x - er, by - (mood == 2 && e > 0 ? 1 : 0), er * 2 + 1, INK);
    }
    // the mouth
    const int my = cy + r * 2 / 5, mw = 2 * er + 1;
    if (mood == 1) {
        t.fillRect(cx - mw / 2, my - 1, mw, u + 1, INK);
        t.fillRect(cx - mw / 2 + 1, my, mw - 2, u, (uint16_t)0x7863);
    } else if (mood == 0 && ((now / 150) & 1) == 0) {
        t.fillRect(cx - mw / 2 + 1, my - 1, mw - 2, u, INK);
    } else {
        t.drawFastHLine(cx - mw / 2, my, mw, INK);
        t.drawPixel(cx + mw / 2, my - 1, INK);          // the smirk
    }
}

// The chain, ankle to ring: two straight runs of links meeting low in the
// middle, so it lies along the floor -- or one straight run, pulled taut,
// while he is off the ground.
__attribute__((noinline)) static void chainDraw(TFT_eSPI& t, int ax, int ay, int bx, int by, int floorY, int link, bool taut) {
    const uint16_t LINK = (uint16_t)0x6B90, LINK_HI = (uint16_t)0xAD97, RIM = (uint16_t)0x0020;
    int mx = (ax + bx) / 2, my = (ay > by ? ay : by) + (taut ? 0 : link * 2);
    if (taut) my = (ay + by) / 2;
    if (my > floorY - 1) my = floorY - 1;
    int i = 0;
    for (int seg = 0; seg < 2; seg++) {
        const int x0 = seg ? mx : ax, y0 = seg ? my : ay, x1 = seg ? bx : mx, y1 = seg ? by : my;
        const int dx = x1 - x0, dy = y1 - y0;
        int n = (abs(dx) > abs(dy) ? abs(dx) : abs(dy)) / link;
        if (n < 1) n = 1;
        for (int s = 0; s < n; s++, i++) {
            const int x = x0 + dx * s / n, y = y0 + dy * s / n;
            const int lw = (i & 1) ? link - 1 : link, lh = (i & 1) ? link : link - 1;
            t.fillRect(x - lw / 2 - 1, y - lh / 2 - 1, lw + 2, lh + 2, RIM);
            t.fillRect(x - lw / 2, y - lh / 2, lw, lh, LINK);
            t.drawPixel(x - lw / 2, y - lh / 2, LINK_HI);
        }
    }
}

__attribute__((noinline)) static void jailTick(TFT_eSPI& t, uint32_t now, int screenW) {
    int cx, halfW, top, bot;
    if (!Squachy::lastFootprint(cx, halfW, top, bot)) { s_jX = -100; return; }
    (void)top;
    // The ball stays where he stands; a carried or thrown Squachy pulls the
    // chain, not the ball.
    const bool away = Squachy::isHeld() || Squachy::thrown();
    if (away) {
        if (!s_jaValid) { s_jX = -100; return; }
        cx = s_jaCx; halfW = s_jaHalfW; bot = s_jaBot;
    } else { s_jaCx = cx; s_jaHalfW = halfW; s_jaBot = bot; s_jaValid = true; }
    const int H = t.height();
    // Out as the pet, not the jailer: his first remark comes soon after he
    // is chosen, and the jail's own lines never do.
    const bool pet = Squachy::ballPetOn();
    if (pet != s_jWasPet) { s_jWasPet = pet; s_jNextAt = now + 6000; s_jLine = nullptr; }
    // Sized from him: halfW is 24 units of his scale, and the ball is a
    // 13-unit radius at the main screen's 2.7.
    int r = halfW / 5;
    if (r < 6) r = 6;
    int ballX = cx + halfW * 76 / 65;
    if (ballX > screenW - r - 4) ballX = screenW - r - 4;
    const int floorY = bot + 1;

    // Walked out: the ball says its piece, then goes up in smoke.
    if (Squachy::takeJailFreed()) { s_jFreeAt = now ? now : 1; s_jArrestAt = 0; ballSay(BALL_FREE, now, 2); s_jAnswered = true; }

    int drop = 0, puff = -1;                     // puff: 0..7 while the smoke is up
    bool ballOn = true, clank = false, siren = false;
    if (s_jArrestAt) {
        const int a = (int)(now - s_jArrestAt);
        siren = a < 1800;
        if (a < 1000) ballOn = false;
        if (a >= 500 && a < 1300) puff = (a - 500) / 100;
        if (a >= 1000 && a < 1500) { const int v = 500 - (a - 1000); drop = -(90 * H / 240) * v / 500 * v / 500; }
        if (a >= 1450 && a < 1900) clank = true;
        if (a >= 1500 && !s_jLine) { ballSay(BALL_ARREST, now, 2); s_jAnswered = true; }
        if (a > 1900) s_jArrestAt = 0;
    }
    if (s_jFreeAt) {
        const int a = (int)(now - s_jFreeAt);
        if (a > 3400) { ballOn = false; if (a < 4200) puff = (a - 3400) / 100; }
    }
    // The chain stopped a throw: he enjoyed that.
    const uint32_t ya = Squachy::yankedAt();
    if (ya && ya != s_jYankSeen) { s_jYankSeen = ya; ballSay((pet ? BALL_PET_YANK : BALL_YANK)[random(0, 3)], now, 1); s_jAnswered = true; }
    // Something to say about once a minute, and Squachy answers now and then.
    const bool talking = s_jLine && now - s_jSaidAt < 5500;
    if ((Squachy::jailed() || pet) && !s_jArrestAt && !talking && (int32_t)(now - s_jNextAt) >= 0) {
        if (pet) ballSay(BALL_PET_LINES[s_jIx++ % BALL_PET_LINES_N], now, random(0, 4) == 0 ? 1 : 0);
        else     ballSay(BALL_LINES[s_jIx++ % BALL_LINES_N], now, random(0, 3) == 0 ? 1 : 0);
        s_jNextAt = now + 45000 + (uint32_t)random(0, 30000);
    }
    if (s_jLine && !talking && !s_jAnswered) {
        s_jAnswered = true;
        if (random(0, 3) == 0) Squachy::visitSay((pet ? PET_SQ : JAIL_SQ)[s_jSq++ % 4]);
    }

    if (ballOn) {
        const int bb = floorY + drop;
        int ax, ay;
        if (drop == 0 && Squachy::chainPoint(ax, ay)) {
            const int link = r / 4 > 2 ? r / 4 : 2;
            chainDraw(t, ax, ay, ballX - r * 3 / 4, bb - r - r * 3 / 4, floorY, link, away);
        }
        uint8_t mood = 3;
        if (talking) mood = (s_jMood == 1 && now - s_jSaidAt < 1600) ? 1 : (s_jMood == 2 && now - s_jSaidAt > 2000 ? 2 : 0);
        ballDraw(t, ballX, bb, r, now, mood);
        s_jX = (int16_t)ballX; s_jY = (int16_t)bb; s_jR = (int16_t)r;
        if (clank) {
            t.setTextSize(1);
            t.fillRect(ballX + r - 2, bb - 2 * r - 10, 34, 10, (uint16_t)0xFF09);
            t.setTextColor(Theme::BLACK);
            t.setCursor(ballX + r, bb - 2 * r - 9);
            t.print("CLANK");
        }
        // On a short screen the balloon goes beside him, so hang it from
        // higher up or it sits on the buttons.
        if (talking && drop == 0) petBalloon(t, s_jLine, ballX - r, bb - 2 * r - (H < 200 ? 3 * r : 0), 2 * r, screenW, H);
        // Time left, over the ball, whenever the balloon is not there: the
        // sentence was only ever readable from Settings before.
        else if (drop == 0 && !clank && Squachy::jailed()) {
            const uint32_t left = (Squachy::jailLeftMs() + 999) / 1000;
            char tm[8];
            snprintf(tm, sizeof tm, "%u:%02u", (unsigned)(left / 60), (unsigned)(left % 60));
            t.setTextSize(1);
            const int tw = t.textWidth(tm), tx = ballX - tw / 2, ty = bb - 2 * r - 12;
            t.fillRect(tx - 3, ty - 2, tw + 6, 11, Theme::BLACK);
            t.drawRect(tx - 3, ty - 2, tw + 6, 11, (uint16_t)0x7C11);
            t.setTextColor(Theme::AMBER, Theme::BLACK);
            t.setCursor(tx, ty);
            t.print(tm);
        }
    } else s_jX = -100;
    if (puff >= 0) {
        // Eight puffs round where the ball sits, drifting out.
        static const int8_t PX[8] = { 10, 7, 0, -7, -10, -7, 0, 7 }, PY[8] = { 0, 4, 6, 4, 0, -4, -6, -4 };
        const uint16_t smoke = (uint16_t)0xDEDB;
        const int d = 2 + puff, pr = r / 4 > 1 ? r / 4 : 1;
        if (puff < 7) for (int i = 0; i < 8; i++)
            t.fillCircle(ballX + PX[i] * d * r / 52, floorY - r + PY[i] * d * r / 52, pr, smoke);
    }
    if (siren) {
        const bool red = ((now / 180) & 1) == 0;
        const uint16_t R_ = (uint16_t)0xF904, B_ = (uint16_t)0x229F;
        t.fillRect(0, 0, screenW, 4, red ? R_ : B_);
        t.fillRect(0, H - 4, screenW, 4, red ? B_ : R_);
        t.setTextSize(H >= 200 ? 3 : 2);
        const int tw = t.textWidth("BUSTED!"), ty = H * 2 / 5;
        t.setTextColor(Theme::BLACK);
        t.setCursor((screenW - tw) / 2 + 2, ty + 2); t.print("BUSTED!");
        t.setTextColor((uint16_t)0xFF09);
        t.setCursor((screenW - tw) / 2, ty); t.print("BUSTED!");
        t.setTextSize(1);
    }
}

static void jailReset() { s_jX = -100; s_jaValid = false; s_jWasPet = false; }
