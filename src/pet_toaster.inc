// SquachWatch-CYD — T0@$TY, the brave little toaster. Included by pet.cpp
// inside namespace Pet, after C1iPPY, whose balloon and throw he borrows.
//
// A chrome two-slot toaster off the kitchen counter, 30 x 23 pixels, who
// stands on the pets' line beside Squachy and is scared of most things
// while insisting he is not. Every catch gets a slice of toast popped in
// that device's colour, which flies to Squachy, who eats it. He hops, he
// can be picked up and thrown (he is heavy: one thud, hardly a bounce,
// crumbs), and a hard landing leaves him on his back for a moment.
//
// Lines are three balloon lines of 24 characters at most, 72 in all.
static const char* const TOAST_TIPS[] = {
    "I'm on guard. You rest, Master.",
    "I'm not scared. Are you scared? I'm not.",
    "Toast is ready when danger is.",
    "I checked behind us. Twice.",
    "Nothing nearby. I'm still watching.",
    "I was left on a counter once. Never again.",
    "Crumbs are just tiny medals.",
    "If it beeps, I'll go first.",
    "Two slots. One for you, one for courage.",
    "I counted the cameras. There are some.",
};
static const uint8_t TOAST_TIPS_N = sizeof(TOAST_TIPS) / sizeof(TOAST_TIPS[0]);
static const char* const TOAST_NIGHT[] = {
    "It's dark. I'll glow a bit. For you.",
    "Night watch. I'm the night watch.",
};
static const char* const TOAST_SQUAD[] = {
    "A friend! I'll make them toast too.",
    "Visitor. Stand behind me. Both of you.",
};
static const char* const TOAST_POKE[] = {
    "Reporting for duty!",
    "I'm fine. That's just the heating element.",
    "Don't poke the toaster. The toaster is brave.",
};
// People will throw him. A lot. So there is a dozen of these.
static const char* const TOAST_THROWN[] = {
    "I meant to do that.",
    "Still brave.",
    "Ow. Bravely.",
    "Which way is the kitchen?",
    "That counts as flying. I flew.",
    "Note to self: toasters don't bounce.",
    "Master, I saw the whole screen from up there.",
    "My crumbs! Those were mine.",
    "Again! No. Yes. No.",
    "I kept my toast in. Mostly.",
    "Was that a test? Did I pass?",
    "Brave toasters land on their feet. Eventually.",
    "Is the floor meant to spin?",
};
static const uint8_t TOAST_THROWN_N = sizeof(TOAST_THROWN) / sizeof(TOAST_THROWN[0]);
static const char* const TOAST_FLIPPED[] = {
    "Little help?",
    "I'm fine down here. Very brave.",
    "The ceiling is lovely. Help.",
    "I can see my own cord from here.",
    "This is a tactical position.",
    "Right side up is overrated.",
};
static const uint8_t TOAST_FLIPPED_N = sizeof(TOAST_FLIPPED) / sizeof(TOAST_FLIPPED[0]);
// Thrown again within a minute of the last one, and he starts counting.
// %d is the number of throws in the run; one of these is used from the
// third throw on, every other time.
static const char* const TOAST_STREAK[] = {
    "That's %d. I'm counting.",
    "Throw %d. Still brave.",
    "%d times. Is this a sport now?",
    "%d throws. I live in the air now.",
    "%d. Okay. I'm actually impressed.",
    "Number %d. My lever came loose.",
};
static const uint8_t TOAST_STREAK_N = sizeof(TOAST_STREAK) / sizeof(TOAST_STREAK[0]);
static char    s_tStreakLine[40];
static uint8_t s_tStreak = 0;
static uint32_t s_tStreakAt = 0;
static const char* toastStreakLine(uint32_t now) {
    if (now - s_tStreakAt > 60000) s_tStreak = 0;
    s_tStreakAt = now;
    if (s_tStreak < 255) s_tStreak++;
    if (s_tStreak < 3 || random(0, 2)) return nullptr;
    snprintf(s_tStreakLine, sizeof(s_tStreakLine), TOAST_STREAK[random(0, TOAST_STREAK_N)], (int)s_tStreak);
    return s_tStreakLine;
}
// When it is Squachy who gets thrown past.
static const char* const TOAST_SQTHROWN[] = {
    "MASTER! I'm not scared! You fly! I'm fine!",
    "I'll catch you! I have no arms!",
    "Brave landing, Master. Very brave.",
};
// What Squachy says back, in his own bubble, after a tip has had its turn.
static const char* const TOAST_BACK[] = {
    "stop calling me master", "good toaster.", "you're doing great, buddy",
    "who's a brave appliance?",
};

static const char* toastCatchLine(uint8_t type) {
    switch ((DetectionType)type) {
        case DetectionType::FLOCK:       return "A Flock camera! Stay behind me, Master.";
        case DetectionType::AXON:        return "A body camera. I'm not blinking first.";
        case DetectionType::META:        return "Camera glasses. I'm shiny. They can look at me.";
        case DetectionType::SKIMMER:     return "A card skimmer. Nobody touch anything.";
        case DetectionType::RAVEN:       return "A gunshot sensor. I'm quiet. I'm so quiet.";
        case DetectionType::AIRTAG:
        case DetectionType::SAMSUNG_TAG:
        case DetectionType::GOOGLE_TAG:
        case DetectionType::TILE:        return "A tracker. I'll guard your pockets.";
        case DetectionType::DRONE:       return "A drone! I'm not scared. Are you?";
        case DetectionType::ALPR:        return "A plate reader. I have no plates. Ha.";
        case DetectionType::CAMERA:      return "A camera. It can watch me instead.";
        case DetectionType::RING:        return "A doorbell camera. I'm not ringing it.";
        case DetectionType::DEAUTH:      return "Someone's kicking the WiFi! Hold my cord.";
        case DetectionType::EVILTWIN:    return "An evil twin network. I only have good twins.";
        case DetectionType::HACKER:      return "A hacker tool! I am mostly brave.";
        case DetectionType::IBEACON:     return "A shop beacon. I'm not for sale.";
        default:                         return nullptr;
    }
}
// Serious enough for burnt toast and a wisp of smoke.
static bool toastSerious(uint8_t type) {
    switch ((DetectionType)type) {
        case DetectionType::FLOCK: case DetectionType::AXON: case DetectionType::SKIMMER:
        case DetectionType::RAVEN: case DetectionType::ALPR: case DetectionType::DEAUTH:
        case DetectionType::EVILTWIN: case DetectionType::HACKER:
            return true;
        default:
            return false;
    }
}
// The browning dial, 0 light to 4 burnt, from the last catch.
static uint8_t toastBrowning(uint8_t type) {
    switch ((DetectionType)type) {
        case DetectionType::IBEACON:     return 0;
        case DetectionType::AIRTAG: case DetectionType::SAMSUNG_TAG:
        case DetectionType::GOOGLE_TAG: case DetectionType::TILE: case DetectionType::DRONE: return 1;
        case DetectionType::CAMERA: case DetectionType::RING: case DetectionType::META:    return 2;
        case DetectionType::SKIMMER: case DetectionType::ALPR: case DetectionType::RAVEN:  return 3;
        default:                         return 4;
    }
}

enum class TFace : uint8_t { IDLE, TALK, BRAVE, SCARED, POP, DIZZY };
// NONE on his spot; HELD under a finger; AIR flying; FLIPPED on his back
// after a hard landing; SIT a moment upright again before the hop home.
enum class TPhase : uint8_t { NONE, HELD, AIR, FLIPPED, SIT };

static const int      TOAST_W = 30, TOAST_H = 23;        // the box: body, base and feet
static const uint32_t TOAST_TALK_MS = 5500;
static const uint32_t TOAST_HOP_MS  = 650, TOAST_LAND_MS = 160;
static const uint32_t TOAST_FLIP_MS = 2600, TOAST_SIT_MS = 700;
static const float    TOAST_G = 0.0028f;

static const char* s_tLine    = nullptr;
static uint32_t    s_tSaidAt  = 0, s_tShownMs = 0, s_tLastNow = 0;
static uint32_t    s_tNextTip = 0, s_tHopAt = 0, s_tNextHop = 0, s_tPokeAt = 0, s_tScaredAt = 0;
static bool        s_tCaught = false, s_tAnswered = true, s_tSpotB = false;
static uint8_t     s_tTip = 0, s_tPoke = 0, s_tPending = 0, s_tDial = 2;
static int16_t     s_tX = -100, s_tY = 0, s_tFromX = 0;
// The catch in progress: when it began and what it was.
static uint32_t    s_tCatchAt = 0;
static uint8_t     s_tCatchType = 0;
static bool        s_tPopped = false;
// The slice in the air, on its way to Squachy's mouth.
static bool        s_tsLive = false, s_tsGolden = false, s_tsSerious = false;
static uint32_t    s_tsBorn = 0;
static int16_t     s_tsX0 = 0, s_tsY0 = 0;
static uint16_t    s_tsCol = 0;
static uint32_t    s_tSmokeAt = 0;
static int16_t     s_tSmokeX = 0, s_tSmokeY = 0;
// Thrown.
static TPhase      s_tPh = TPhase::NONE;
static float       s_tfX = 0, s_tfY = 0, s_tvX = 0, s_tvY = 0;
static Grip        s_tG;
static uint32_t    s_tThLast = 0, s_tThAt = 0, s_tSqAt = 0;
static float       s_tSqK = 0, s_tSq = 1.0f;
static bool        s_tFlung = false, s_tHard = false;
static uint8_t     s_tBounces = 0;
// Crumbs off a thud, and the loose slice that falls out with them.
static uint32_t    s_crumbAt = 0;
static float       s_crumbX[6], s_crumbY[6], s_crumbVX[6], s_crumbVY[6];
static bool        s_tlLive = false;
static float       s_tlX = 0, s_tlY = 0, s_tlVX = 0, s_tlVY = 0;
static uint32_t    s_tlBorn = 0;
// Where Squachy stands, remembered while he is carried or thrown.
static bool        s_taValid = false, s_tSqWasThrown = false;
static int         s_taCx = 0, s_taHalfW = 0, s_taBot = 0;

static bool toastTalking() { return s_tLine && s_tShownMs < TOAST_TALK_MS; }
static void toastSay(const char* line, uint32_t now, bool caught) {
    s_tShownMs = 0;
    s_tLine = line; s_tSaidAt = now ? now : 1; s_tCaught = caught; s_tAnswered = caught;
}

// ---- the toaster -----------------------------------------------------------
// Everything is drawn in a 30 x 23 box at (x, y); his feet are on the box's
// bottom line. `sq` squashes him into the floor on a landing; `flip` draws
// him on his back (every row measured from the bottom instead of the top).
static void toastDraw(TFT_eSPI& t, int x, int y, uint32_t now, TFace face, float sq, float lever, bool flip, bool air) {
    const uint16_t OUT   = (uint16_t)0x10A3;
    const uint16_t BODY  = (uint16_t)0xBE5A, SHINE = (uint16_t)0xF7BF;
    const uint16_t SHADE = (uint16_t)0x9516, BASE  = (uint16_t)0x2966;
    const uint16_t TRACK = (uint16_t)0x8CB4, KNOB  = (uint16_t)0xD9A5;
    const uint16_t CREAM = (uint16_t)0xEF39, PRONG = (uint16_t)0xCE9B;
    const uint16_t INK   = (uint16_t)0x1904, CHEEK = (uint16_t)0xFCC7;
    const int w = TOAST_W, h = (int)lroundf(20.0f * sq);        // the body; 3 more for the feet
    const int H = h + 3;
    y += 20 - h;                                        // a squash sinks him into the floor, not off it
    // A rect by its offset from the top of the box, mirrored when flipped.
    auto R = [&](int ox, int oy, int rw, int rh, uint16_t c) {
        if (rw <= 0 || rh <= 0) return;
        t.fillRect(x + ox, flip ? y + H - oy - rh : y + oy, rw, rh, c);
    };
    const int bodyTop = 0;                              // the feet are the box's last three rows
    // shadow
    if (!air && !flip) t.fillRect(x, y + H, w, 2, (uint16_t)0x10A2);
    // cord to the right, red and cream, with a plug on the end
    {
        const int cy0 = H - 6;
        int lx = w, ly = cy0;
        for (int i = 0; i < 9; i++) {
            lx = w + i; ly = cy0 + (int)lroundf(sinf((float)i * 0.9f + (float)now / 260.0f) * 1.4f);
            R(lx, ly - 1, 1, 3, OUT);
            R(lx, ly, 1, 1, (i & 1) ? KNOB : CREAM);
        }
        R(lx, ly - 2, 5, 6, OUT);
        R(lx + 1, ly - 1, 3, 4, CREAM);
        R(lx + 4, ly - 1, 2, 1, PRONG); R(lx + 4, ly + 2, 2, 1, PRONG);
    }
    // lever on the left: a track and a red knob that rides down it
    {
        const int ly0 = bodyTop + 5;
        R(-5, ly0 - 2, 5, 13, OUT);
        R(-2, ly0, 1, 9, TRACK);
        R(-4, ly0 - 1 + (int)lroundf(lever * 7.0f), 3, 3, KNOB);
    }
    // feet
    R(4, H - 3, 5, 3, OUT); R(w - 9, H - 3, 5, 3, OUT);
    // body: round shoulders, square base, dark outline
    {
        const int top = flip ? y + H - bodyTop - h : y + bodyTop;
        t.fillRoundRect(x - 1, top - 1, w + 2, h + 2, 7, OUT);
        t.fillRoundRect(x, top, w, h, 6, BODY);
        if (flip) { t.fillRect(x - 1, top - 1, w + 2, 8, OUT); t.fillRect(x, top, w, 7, BODY); }
        else      { t.fillRect(x - 1, top + h - 7, w + 2, 8, OUT); t.fillRect(x, top + h - 7, w, 7, BODY); }
    }
    R(4, bodyTop + 2, w - 8, 2, SHINE);
    R(0, bodyTop + h - 5, w, 1, SHADE);
    R(0, bodyTop + h - 4, w, 4, BASE);
    // the slots
    R(5, bodyTop, 8, 2, BASE); R(w - 13, bodyTop, 8, 2, BASE);
    // a glint that crosses the chrome every few seconds
    {
        const uint32_t gp = now % 5200u;
        if (gp < 650) R(2 + (int)(gp * (uint32_t)(w - 6) / 650u), bodyTop + 5, 2, h - 11, SHINE);
    }
    if (lever > 0.5f) { R(3, bodyTop + 11, 3, 2, CHEEK); R(w - 6, bodyTop + 11, 3, 2, CHEEK); }
    // the browning dial, bottom right of the front: five pointer positions
    {
        const int dx = w - 9, dy = bodyTop + 12;
        R(dx, dy, 4, 4, TRACK);
        static const int8_t PX[5] = { 0, 0, 1, 3, 3 }, PY[5] = { 3, 1, 0, 1, 3 };
        R(dx + PX[s_tDial], dy + PY[s_tDial], 1, 1, BASE);
    }
    // the face
    {
        const int cx = w / 2, ey = bodyTop + 7;
        const bool blink = face == TFace::IDLE && (now % 4300u) < 140;
        for (int sd = -1; sd <= 1; sd += 2) {
            const int ex = cx + sd * 5;
            if (face == TFace::DIZZY) {
                if ((now / 180) & 1) { R(ex - 1, ey, 1, 1, INK); R(ex + 1, ey, 1, 1, INK); R(ex, ey + 1, 1, 1, INK); R(ex - 1, ey + 2, 1, 1, INK); R(ex + 1, ey + 2, 1, 1, INK); }
                else { R(ex, ey, 1, 3, INK); R(ex - 1, ey + 1, 3, 1, INK); }
            } else if (face == TFace::SCARED || face == TFace::POP) {
                R(ex - 2, ey - 1, 5, 5, INK); R(ex - 1, ey - 2, 3, 7, INK); R(ex - 3, ey, 7, 3, INK);
                R(ex - 1, ey - 1, 3, 5, Theme::WHITE); R(ex - 2, ey, 5, 3, Theme::WHITE);
                if (face == TFace::POP) R(ex - 1, ey, 2, 2, INK);
                else R(ex + (((now / 90) & 1) ? 0 : -1), ey + 1, 1, 1, INK);
            } else if (blink) R(ex - 1, ey + 2, 3, 1, INK);
            else { R(ex - 1, ey, 3, 4, INK); R(ex - 1, ey, 1, 1, Theme::WHITE); }
            // brows: inner end low for brave, inner end high for scared
            if (face == TFace::BRAVE)  for (int i = 0; i < 4; i++) R(ex + sd * (2 - i), ey - 4 + i * 2 / 3, 1, 1, INK);
            if (face == TFace::SCARED) for (int i = 0; i < 4; i++) R(ex + sd * (2 - i), ey - 4 - i * 2 / 3, 1, 1, INK);
        }
        const int my = ey + 6;
        switch (face) {
            case TFace::BRAVE:  R(cx - 2, my, 4, 1, INK); R(cx + 2, my - 1, 1, 1, INK); break;
            case TFace::TALK:   R(cx - 1, my - 1, 3, 2 + (int)((now / 160) & 1), INK); break;
            case TFace::POP:    R(cx - 1, my - 1, 3, 3, INK); R(cx, my, 1, 1, Theme::WHITE); break;
            case TFace::SCARED:
            case TFace::DIZZY:  R(cx - 3, my, 2, 1, INK); R(cx - 1, my - 1, 2, 1, INK); R(cx + 1, my, 2, 1, INK); break;
            default:            R(cx - 2, my, 4, 1, INK); R(cx - 3, my - 1, 1, 1, INK); R(cx + 2, my - 1, 1, 1, INK); break;
        }
        if (face == TFace::SCARED) R(cx + 10, ey - 3 + (int)((now / 130) % 4), 1, 2, (uint16_t)0x7EBF);
    }
}

// A slice of toast, 8 x 9, or flat (seen edge-on) mid-spin.
static void toastSlice(TFT_eSPI& t, int x, int y, uint16_t faceCol, bool burnt, bool flat) {
    const uint16_t OUT = (uint16_t)0x10A3;
    const uint16_t CRUST = burnt ? (uint16_t)0x3922 : (uint16_t)0x8AA4;
    if (flat) {
        t.fillRect(x - 1, y + 2, 10, 4, OUT);
        t.fillRect(x, y + 3, 8, 2, CRUST);
        t.fillRect(x + 1, y + 3, 6, 1, faceCol);
        return;
    }
    t.fillRoundRect(x - 1, y - 1, 10, 11, 3, OUT);
    t.fillRoundRect(x, y, 8, 9, 2, CRUST);
    t.fillRect(x, y + 5, 8, 4, CRUST);
    t.fillRoundRect(x + 1, y + 1, 6, 7, 1, faceCol);
    t.fillRect(x + 1, y + 4, 6, 4, faceCol);
}
static uint16_t toastFaceCol(TFT_eSPI& t, uint8_t type, bool golden, bool burnt) {
    if (golden) return (uint16_t)0xFEA7;
    const uint16_t bread = burnt ? (uint16_t)0x7A85 : (uint16_t)0xEDCE;
    return Theme::blend(bread, Theme::colorFor((DetectionType)type), 150);
}

static void toasterNoteCatch(uint8_t type) { s_tPending = (uint8_t)(type + 1); }

static bool toasterHit(int x, int y) {
    if (s_tX < -50) return false;
    return x >= s_tX - 6 && x <= s_tX + TOAST_W + 14 && y >= s_tY - 4 && y <= s_tY + TOAST_H + 2;
}
static bool toasterCenter(int& x, int& y) {
    if (s_tX < -50) return false;
    x = s_tX + TOAST_W / 2; y = s_tY + TOAST_H / 2;
    return true;
}
static void toasterPoke(uint32_t now) {
    if (now - s_tPokeAt > 8000) s_tPoke = 0;
    s_tPokeAt = now;
    toastSay(TOAST_POKE[s_tPoke % 3], now, false);
    s_tAnswered = true;
    s_tPoke++;
}
static void toasterGrab(int x, int y, uint32_t now) {
    if (s_tX < -50) return;
    s_tPh = TPhase::HELD;
    s_tfX = (float)s_tX; s_tfY = (float)s_tY;
    gripStart(s_tG, x, y, s_tX, s_tY, now);
    s_tHopAt = 0;
    s_tCatchAt = 0;
}
static void toasterDrag(int x, int y, uint32_t now) {
    if (s_tPh != TPhase::HELD) return;
    gripMove(s_tG, x, y, now);
}
static void toastLaunch(float vx, float vy, uint32_t now) {
    if (vx > CLIP_MAX_V) vx = CLIP_MAX_V;
    if (vx < -CLIP_MAX_V) vx = -CLIP_MAX_V;
    if (vy > CLIP_MAX_V) vy = CLIP_MAX_V;
    if (vy < -CLIP_MAX_V) vy = -CLIP_MAX_V;
    s_tvX = vx; s_tvY = vy;
    s_tPh = TPhase::AIR;
    s_tThAt = s_tThLast = now;
    s_tBounces = 0;
    s_tSqAt = 0;
    s_tHard = false;
}
static void toasterRelease(uint32_t now) {
    if (s_tPh != TPhase::HELD) return;
    float vx, vy;
    switch (gripLetGo(s_tG, now, vx, vy)) {
        case 0:  s_tPh = TPhase::NONE; toasterPoke(now); break;
        case 1:  toastLaunch(vx, vy, now); s_tFlung = true; noteThrow(now); break;
        case 2:  toastLaunch(0, 0, now); s_tFlung = false; break;
        default: s_tPh = TPhase::NONE; break;
    }
}

static void toastCrumbs(int cx, int cy, uint32_t now) {
    s_crumbAt = now ? now : 1;
    for (uint8_t i = 0; i < 6; i++) {
        s_crumbX[i] = (float)(cx + (i - 3) * 4); s_crumbY[i] = (float)(cy - 2);
        s_crumbVX[i] = (float)(i - 3) * 0.02f + (float)random(-10, 11) / 1000.0f;
        s_crumbVY[i] = -0.12f - (float)random(0, 120) / 1000.0f;
    }
}

static void toasterTick(TFT_eSPI& t, uint32_t now, int screenW, int floorY) {
    int cx, halfW, top, bot;
    if (!Squachy::lastFootprint(cx, halfW, top, bot)) { s_tX = -100; return; }
    (void)top;
    bot = floorY;
    const bool sqAway = Squachy::isHeld() || Squachy::thrown();
    if (sqAway) {
        if (!s_taValid) { s_tX = -100; return; }
        cx = s_taCx; halfW = s_taHalfW; bot = s_taBot;
    } else {
        s_taCx = cx; s_taHalfW = halfW; s_taBot = bot; s_taValid = true;
    }
    // Squachy flying past is the scariest thing he knows.
    const bool sqThrown = Squachy::thrown();
    if (sqThrown && !s_tSqWasThrown && s_tPh == TPhase::NONE) {
        s_tScaredAt = now ? now : 1;
        if (!toastTalking()) { toastSay(TOAST_SQTHROWN[random(0, 3)], now, false); s_tAnswered = true; }
    }
    s_tSqWasThrown = sqThrown;
    uint32_t seenMs = (s_tLastNow && now - s_tLastNow < 100000u) ? now - s_tLastNow : 0;
    if (seenMs > 100) seenMs = 100;
    s_tLastNow = now;
    const int screenH = t.height();
    // Two spots beside him (lever room on the left, cord room on the right),
    // the right-hand side when it has room.
    int a = cx + halfW + 7, b = a + 30;
    if (b + TOAST_W + 16 > screenW - 2) b = a;
    if (a + TOAST_W + 16 > screenW - 2) { a = cx - halfW - TOAST_W - 16; b = a - 30; if (b < 6) b = a; }

    if (const char* intro = Squachy::takeToasterIntro()) {
        toastSay(intro, now, false);
        s_tAnswered = true;
        Theme::showToast("T0@$TY", "your new pet. Settings > Pet", Theme::AMBER, 3500);
        s_tNextTip = now + 60000;
    }
    if (!s_tNextTip) s_tNextTip = now + 8000;
    if (!s_tNextHop) s_tNextHop = now + 30000 + (uint32_t)random(0, 20000);
    const bool hopping = s_tHopAt && now - s_tHopAt < TOAST_HOP_MS;
    const bool busy = s_tPh != TPhase::NONE || hopping || (s_tCatchAt && now - s_tCatchAt < 3400);
    // A catch: the lever goes down, he braces, and the slice pops.
    if (s_tPending && !busy) {
        s_tCatchType = (uint8_t)(s_tPending - 1);
        s_tPending = 0;
        s_tCatchAt = now ? now : 1;
        s_tPopped = false;
        s_tDial = toastBrowning(s_tCatchType);
    }
    if ((int32_t)(now - s_tNextTip) >= 0 && !toastTalking() && !busy) {
        const char* l = TOAST_TIPS[s_tTip++ % TOAST_TIPS_N];
        if (Squachy::visiting() && random(0, 2) == 0) l = TOAST_SQUAD[random(0, 2)];
        else if (Clock::night() && random(0, 3) == 0) l = TOAST_NIGHT[random(0, 2)];
        toastSay(l, now, false);
        s_tNextTip = now + 60000 + (uint32_t)random(0, 30000);
    }
    const bool talking = toastTalking();
    if (s_tLine && !talking && !s_tAnswered) {
        s_tAnswered = true;
        if (random(0, 2) == 0) Squachy::visitSay(TOAST_BACK[random(0, 4)]);
    }
    if (!talking && !busy && (int32_t)(now - s_tNextHop) >= 0 && a != b) {
        s_tFromX = s_tSpotB ? (int16_t)b : (int16_t)a;
        s_tSpotB = !s_tSpotB;
        s_tHopAt = now;
        s_tNextHop = now + 30000 + (uint32_t)random(0, 20000);
    }
    const int to = s_tSpotB ? b : a;

    // ---- the slice in the air, on its way to Squachy ----
    auto drawSlice = [&]() {
        if (!s_tsLive) return;
        const uint32_t age = now - s_tsBorn;
        int mx, my;
        if (!Squachy::mouthPoint(mx, my) || Squachy::isHeld() || Squachy::thrown()) { mx = cx; my = bot - 60; }
        if (age >= 900) { s_tsLive = false; Squachy::eatToast(s_tsGolden); return; }
        const float k = (float)age / 900.0f;
        const int sx = s_tsX0 + (int)lroundf((float)(mx - s_tsX0) * k);
        const int sy = s_tsY0 + (int)lroundf((float)(my - s_tsY0) * k - sinf(k * 3.14159265f) * 34.0f);
        toastSlice(t, sx - 4, sy - 5, s_tsCol, s_tsSerious, ((age / 120) & 1) != 0);
        if (s_tsGolden) for (uint8_t q = 0; q < 3; q++) t.drawPixel(sx - 6 - q * 3, sy + (int)q - 1, (uint16_t)0xFF51);
    };
    auto drawDebris = [&]() {
        if (s_crumbAt && now - s_crumbAt < 650) {
            const uint16_t bread = (uint16_t)0xEDCE;
            for (uint8_t i = 0; i < 6; i++) {
                s_crumbVY[i] += 0.0011f * (float)seenMs;
                s_crumbX[i] += s_crumbVX[i] * (float)seenMs; s_crumbY[i] += s_crumbVY[i] * (float)seenMs;
                t.drawPixel((int)s_crumbX[i], (int)s_crumbY[i], bread);
            }
        } else s_crumbAt = 0;
        if (s_tlLive) {
            if (now - s_tlBorn > 1000) s_tlLive = false;
            else {
                s_tlVY += 0.0011f * (float)seenMs;
                s_tlX += s_tlVX * (float)seenMs; s_tlY += s_tlVY * (float)seenMs;
                toastSlice(t, (int)s_tlX - 4, (int)s_tlY - 5, (uint16_t)0xEDCE, false, (((now - s_tlBorn) / 120) & 1) != 0);
            }
        }
        if (s_tSmokeAt && now - s_tSmokeAt < 1400) {
            const uint16_t grey = Theme::blend(Theme::BG, (uint16_t)0xAD56, 150);
            for (uint8_t q = 0; q < 3; q++) {
                const int rise = (int)(((now - s_tSmokeAt) / 70 + q * 4) % 14);
                t.drawPixel(s_tSmokeX + q * 3 + (int)lroundf(sinf((float)now / 200.0f + (float)q)), s_tSmokeY - 2 - rise, grey);
            }
        } else s_tSmokeAt = 0;
    };

    // ---- picked up, thrown, on his back, or getting over it ----
    if (s_tPh != TPhase::NONE) {
        const float floorBox = (float)(bot - TOAST_H);
        const float leftX = 6.0f, rightX = (float)(screenW - TOAST_W - 16), ceilY = 16.0f;
        TFace tf = TFace::SCARED;
        bool flip = false;
        s_tSq = 1.0f;
        if (s_tPh == TPhase::HELD) {
            s_tfX = (float)(s_tG.fingerX - s_tG.offX);
            s_tfY = (float)(s_tG.fingerY - s_tG.offY);
            if (s_tfX < leftX) s_tfX = leftX;
            if (s_tfX > rightX) s_tfX = rightX;
            if (s_tfY < ceilY) s_tfY = ceilY;
            if (s_tfY > floorBox) s_tfY = floorBox;
            if (!s_tG.moved) tf = TFace::IDLE;
            if (now - s_tG.gT > 600 && s_tG.moved) toastLaunch(0, 0, now);
        } else if (s_tPh == TPhase::AIR) {
            uint32_t dt = now - s_tThLast;
            if (dt > 60) dt = 60;
            s_tThLast = now;
            while (dt && s_tPh == TPhase::AIR) {
                const float st = (float)(dt > 8 ? 8 : dt);
                dt -= (dt > 8 ? 8 : dt);
                s_tvY += TOAST_G * st;
                s_tfX += s_tvX * st;
                s_tfY += s_tvY * st;
                if (s_tfX < leftX && s_tvX < 0)  { s_tfX = leftX;  s_tvX = -s_tvX * 0.5f; }
                if (s_tfX > rightX && s_tvX > 0) { s_tfX = rightX; s_tvX = -s_tvX * 0.5f; }
                if (s_tfY < ceilY && s_tvY < 0)  { s_tfY = ceilY;  s_tvY = -s_tvY * 0.3f; }
                if (s_tfY >= floorBox && s_tvY > 0) {
                    // He is a toaster: a thud, crumbs, and at most one small hop.
                    s_tfY = floorBox;
                    s_tSqAt = now;
                    s_tSqK = s_tvY > 0.9f ? 1.0f : s_tvY / 0.9f;
                    if (s_tBounces == 0) {
                        toastCrumbs((int)s_tfX + TOAST_W / 2, (int)s_tfY + TOAST_H, now);
                        if (s_tFlung) Squachy::petLandedAt((int)s_tfX + TOAST_W / 2);
                        if (s_tFlung) {
                            s_tlLive = true; s_tlBorn = now;
                            s_tlX = s_tfX + 8; s_tlY = s_tfY; s_tlVX = -0.03f; s_tlVY = -0.4f;
                        }
                        if (s_tvY > 0.75f) s_tHard = true;
                    }
                    if (s_tvY > 0.35f && s_tBounces < 1) {
                        s_tvY = -s_tvY * 0.25f;
                        s_tvX *= 0.5f;
                        s_tBounces++;
                    } else {
                        s_tvY = 0;
                        s_tThAt = now;
                        const char* streak = s_tFlung ? toastStreakLine(now) : nullptr;
                        if (s_tHard && s_tFlung) {
                            s_tPh = TPhase::FLIPPED;
                            toastSay(streak ? streak : TOAST_FLIPPED[random(0, TOAST_FLIPPED_N)], now, false);
                        } else {
                            s_tPh = TPhase::SIT;
                            if (s_tFlung) toastSay(streak ? streak : TOAST_THROWN[random(0, TOAST_THROWN_N)], now, false);
                        }
                        s_tAnswered = true;
                        s_tFlung = false;
                    }
                }
            }
            if (s_tPh == TPhase::AIR && now - s_tThAt > 5000) { s_tfY = floorBox; s_tPh = TPhase::SIT; s_tThAt = now; }
        } else if (s_tPh == TPhase::FLIPPED) {
            tf = TFace::DIZZY;
            flip = true;
            if (now - s_tThAt > TOAST_FLIP_MS) { s_tPh = TPhase::SIT; s_tThAt = now; }
        } else if (s_tPh == TPhase::SIT) {
            tf = TFace::DIZZY;
            if (now - s_tThAt > TOAST_SIT_MS) {
                s_tPh = TPhase::NONE;
                s_tFromX = (int16_t)lroundf(s_tfX);
                s_tHopAt = now;
            }
        }
        if (s_tPh != TPhase::NONE) {
            if (s_tSqAt && now - s_tSqAt < TOAST_LAND_MS + 60)
                s_tSq = 1.0f - 0.22f * s_tSqK * (1.0f - (float)(now - s_tSqAt) / (float)(TOAST_LAND_MS + 60));
            s_tX = (int16_t)lroundf(s_tfX);
            s_tY = (int16_t)lroundf(s_tfY);
            // On his back he rocks, the first second or so.
            const int rock = (flip && now - s_tThAt < 1200) ? (int)((now / 110) & 1) : 0;
            drawSlice();
            toastDraw(t, s_tX + rock, s_tY, now, tf, s_tSq, 0.0f, flip, s_tPh == TPhase::AIR || s_tPh == TPhase::HELD);
            drawDebris();
            if (toastTalking() && (s_tPh == TPhase::SIT || s_tPh == TPhase::FLIPPED)) {
                petBalloon(t, s_tLine, s_tX, s_tY, TOAST_W, screenW, screenH);
                s_tShownMs += seenMs;
            }
            return;
        }
    }

    // ---- on his spot ----
    float x = (float)to, hop = 0.0f, lever = 0.0f;
    s_tSq = 1.0f;
    TFace face = TFace::IDLE;
    const uint32_t since = now - s_tHopAt;
    if (s_tHopAt && since < TOAST_HOP_MS) {
        const float k = (float)since / (float)TOAST_HOP_MS;
        x = (float)s_tFromX + (float)(to - s_tFromX) * k;
        hop = -sinf(k * 3.14159265f) * 16.0f;
        s_tSq = 1.0f + 0.06f * sinf(k * 3.14159265f);
        face = TFace::POP;
    } else {
        if (s_tHopAt && since < TOAST_HOP_MS + TOAST_LAND_MS) s_tSq = 0.84f + 0.16f * (float)(since - TOAST_HOP_MS) / (float)TOAST_LAND_MS;
        if (talking) face = TFace::TALK;
    }
    if (s_tCatchAt) {
        const uint32_t age = now - s_tCatchAt;
        if (age < 450)      { face = TFace::POP; lever = (float)age / 450.0f; }
        else if (age < 800) { face = TFace::POP; lever = 1.0f; }
        else {
            if (!s_tPopped) {
                s_tPopped = true;
                s_tsLive = true; s_tsBorn = now;
                s_tsGolden  = random(0, 50) == 0;
                s_tsSerious = toastSerious(s_tCatchType);
                s_tsCol = toastFaceCol(t, s_tCatchType, s_tsGolden, s_tsSerious);
                s_tsX0 = (int16_t)(lroundf(x) + 9); s_tsY0 = (int16_t)(bot - TOAST_H - 4);
                if (s_tsSerious) { s_tSmokeAt = now ? now : 1; s_tSmokeX = s_tsX0 - 2; s_tSmokeY = s_tsY0; }
                if (const char* l = toastCatchLine(s_tCatchType)) { toastSay(l, now, true); s_tNextTip = now + 60000; }
            }
            if (age < 3400) face = TFace::BRAVE;
            else if (talking) face = TFace::TALK;
            if (age > 60000) s_tCatchAt = 0;
        }
    }
    int shake = 0;
    if (s_tScaredAt && now - s_tScaredAt < 2500) { face = TFace::SCARED; shake = (int)((now / 60) & 1); }
    else s_tScaredAt = 0;
    s_tX = (int16_t)lroundf(x);
    s_tY = (int16_t)(bot - TOAST_H + (int)lroundf(hop));
    drawSlice();
    toastDraw(t, s_tX + shake, s_tY, now, face, s_tSq, lever, false, hop < -0.5f);
    drawDebris();
    if (talking && !(s_tHopAt && since < TOAST_HOP_MS)) {
        petBalloon(t, s_tLine, s_tX, s_tY, TOAST_W, screenW, screenH);
        s_tShownMs += seenMs;
    }
}

static void toasterReset() {
    s_tX = -100;
    s_tHopAt = 0;
    s_tPh = TPhase::NONE;
    s_tsLive = false;
    s_tlLive = false;
    s_crumbAt = 0;
    s_tSmokeAt = 0;
    s_tCatchAt = 0;
}
