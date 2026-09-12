// SquachWatch-CYD — the crowd benchmark. See include/crowd_bench.h.
#include "crowd_bench.h"

#if CROWD_BENCH
#include "squachy.h"
#include "theme.h"
#include <Arduino.h>
#include <esp_heap_caps.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

namespace CrowdBench {
namespace {

enum Kind : uint8_t { ROW, TIER, FLOAT };
struct Scene { Kind kind; uint8_t n; uint8_t back; const char* label; };
// Rows stand on the ground the way a visit does, today's two included for
// comparison. A tier is a back row behind a front one. The floaters fill the
// whole screen in a loose grid and drift about their spots.
const Scene SCENES[] = {
    { ROW,   1, 0, "row 1"   }, { ROW,   2, 0, "row 2"   }, { ROW,   3, 0, "row 3"   },
    { ROW,   4, 0, "row 4"   }, { ROW,   6, 0, "row 6"   }, { ROW,   8, 0, "row 8"   },
    { TIER,  3, 5, "tier 3+5" },
    { FLOAT, 8, 0, "float 8" }, { FLOAT, 12, 0, "float 12" }, { FLOAT, 16, 0, "float 16" },
    { FLOAT, 20, 0, "float 20" }, { FLOAT, 24, 0, "float 24" }, { FLOAT, 30, 0, "float 30" },
};
const uint8_t SCENE_N = sizeof SCENES / sizeof SCENES[0];
const uint32_t SCENE_MS   = 10000;   // of frames actually drawn
const uint32_t WARM_MS    = 1000;    // not counted: the first second of each
const uint32_t RESULTS_MS = 60000;   // the table stays up this long

struct Stats {
    uint32_t frames, maxFrame, minHeap, minBlock;
    uint64_t sumFrame, sumDraw, sumPush, sumBg;
    float    scale;
};
Stats    s_st[SCENE_N];
bool     s_on = true;                 // runs once after every boot
bool     s_results = false, s_hold = false, s_drew = false, s_announced = false;
uint8_t  s_scene = 0;
uint32_t s_sceneMs = 0, s_lastAt = 0, s_resultsAt = 0;
uint32_t s_drawUs = 0;
uint32_t s_frameEma = 0, s_drawEma = 0;

const char* const LINES[] = {
    "Did you see that?", "Nice night.", "Anyone else here?", "Big crowd!",
    "Who brought snacks?", "Squach squad!",
};

uint32_t ema(uint32_t a, uint32_t s) { return a ? a + ((int32_t)s - (int32_t)a) / 8 : s; }

void resetStats() {
    memset(s_st, 0, sizeof s_st);
    for (uint8_t i = 0; i < SCENE_N; i++) { s_st[i].minHeap = 0xFFFFFFFFu; s_st[i].minBlock = 0xFFFFFFFFu; }
}

void printScene(uint8_t i) {
    const Stats& s = s_st[i];
    if (!s.frames) { Serial.printf("[crowd] %-9s no frames counted\n", SCENES[i].label); return; }
    const float fr = (float)s.sumFrame / s.frames / 1000.0f;
    Serial.printf("[crowd] %-9s x%.2f  %lu frames  avg %.1f ms (%.1f fps)  worst %.1f ms  "
                  "crowd %.2f ms  bg %.1f  push %.1f  heap %lu / block %lu\n",
                  SCENES[i].label, s.scale, (unsigned long)s.frames, fr, 1000.0f / fr,
                  s.maxFrame / 1000.0f, (float)s.sumDraw / s.frames / 1000.0f,
                  (float)s.sumBg / s.frames / 1000.0f, (float)s.sumPush / s.frames / 1000.0f,
                  (unsigned long)s.minHeap, (unsigned long)s.minBlock);
}

void begin() {
    resetStats();
    s_on = true; s_results = false; s_hold = false;
    s_scene = 0; s_sceneMs = 0; s_lastAt = 0;
    s_announced = false;
}

void nextScene(uint32_t now) {
    printScene(s_scene);
    s_sceneMs = 0;
    if (s_scene + 1 >= SCENE_N) {
        s_results = true;
        s_resultsAt = now;
        Serial.println("[crowd] done -- the table is on screen for a minute; CROWD runs it again");
        return;
    }
    s_scene++;
}

// One of them, dressed and posed by its slot so a crowd is not twelve
// copies of the same Squachy. Their clocks are offset for the same reason:
// blinking in unison reads as a screensaver.
void one(TFT_eSPI& t, uint8_t i, int cx, int baseY, float s, uint32_t now, const char* line) {
    Squachy::setOutfitPreview((int8_t)((i * 5 + 1) % Squachy::outfitCount()));
    Squachy::setShadesPreview((int8_t)(i % 4));
    const Squachy::VisitPose pose = (i % 5 == 4) ? Squachy::VisitPose::DANCE : Squachy::VisitPose::NONE;
    Squachy::drawWaving(t, cx, baseY, now + i * 137u, s, line, line != nullptr, 0,
                        i % 3 == 0, 20, i % 7 == 3, false, line != nullptr, pose);
    Squachy::setShadesPreview(-1);
    Squachy::setOutfitPreview(-1);
}

// The largest scale that fits `n` in a row of width `w`, never bigger than
// they stand on a visit today (sMax).
float rowScale(int w, int n, float sMax) {
    const float s = (float)(w - 8) / ((float)n * 54.0f);
    return s < sMax ? s : sMax;
}

float drawScene(TFT_eSPI& t, const Scene& sc, uint32_t now, int top, int floorY) {
    const int w = t.width();
    // Today's visit scale is about 1.9 in the 174 rows a landscape CLEAR
    // gives him; the same ratio keeps every layout honest in portrait.
    const float sMax = (float)(floorY - top) / 91.0f;
    const uint8_t total = sc.n + sc.back;
    // Two of them talking, taking turns every three seconds.
    const uint32_t turn = now / 3000;
    const uint8_t ta = (uint8_t)(turn % total), tb = (uint8_t)((ta + total / 2) % total);
    auto lineFor = [&](uint8_t i) -> const char* {
        if (i == ta) return LINES[turn % 6];
        if (total > 1 && i == tb) return LINES[(turn + 3) % 6];
        return nullptr;
    };

    if (sc.kind == ROW) {
        const float s = rowScale(w, sc.n, sMax);
        for (uint8_t i = 0; i < sc.n; i++)
            one(t, i, 4 + (int)(((float)i + 0.5f) * (float)(w - 8) / sc.n), floorY, s, now, lineFor(i));
        return s;
    }
    if (sc.kind == TIER) {
        const float sf = rowScale(w, sc.n, sMax * 0.8f);
        float sb = sf * 0.72f;
        const float sbFit = rowScale(w, sc.back, sb);
        if (sbFit < sb) sb = sbFit;
        const int backY = floorY - (int)(20.0f * sf);
        for (uint8_t i = 0; i < sc.back; i++)                     // behind first
            one(t, (uint8_t)(sc.n + i), 4 + (int)(((float)i + 0.5f) * (float)(w - 8) / sc.back),
                backY, sb, now, lineFor((uint8_t)(sc.n + i)));
        for (uint8_t i = 0; i < sc.n; i++)
            one(t, i, 4 + (int)(((float)i + 0.5f) * (float)(w - 8) / sc.n), floorY, sf, now, lineFor(i));
        return sf;
    }
    // FLOAT: the grid that gives them the biggest size, then a slow drift
    // about each spot. A Squachy is about 54 units wide with his arms and 72
    // tall with his crest.
    const int aw = w - 8, ah = floorY - top;
    int bestCols = 1;
    float best = 0.0f;
    for (int c = 1; c <= sc.n; c++) {
        const int r = (sc.n + c - 1) / c;
        float s = (float)aw / c / 54.0f;
        const float sh = (float)ah / r / 72.0f;
        if (sh < s) s = sh;
        if (s > best) { best = s; bestCols = c; }
    }
    const float s = best < sMax ? best : sMax;
    const int cols = bestCols, rows = (sc.n + cols - 1) / cols;
    const float cw = (float)aw / cols, ch = (float)ah / rows;
    for (uint8_t i = 0; i < sc.n; i++) {
        const int c = i % cols, r = i / cols;
        const float dx = sinf((float)now / 1900.0f + i * 1.7f) * cw * 0.15f;
        const float dy = sinf((float)now / 2600.0f + i * 0.9f) * ch * 0.10f;
        const int cx = 4 + (int)(((float)c + 0.5f) * cw + dx);
        const int baseY = top + (int)(((float)r + 1.0f) * ch - 3.0f * s + dy);
        one(t, i, cx, baseY, s, now, lineFor(i));
    }
    return s;
}

void overlay(TFT_eSPI& t, uint32_t now) {
    const Scene& sc = SCENES[s_scene];
    char a[48], b[56];
    const uint32_t left = s_hold ? 0 : (SCENE_MS > s_sceneMs ? (SCENE_MS - s_sceneMs) / 1000 : 0);
    snprintf(a, sizeof a, "CROWD %u/%u %s x%.2f %s", (unsigned)(s_scene + 1), (unsigned)SCENE_N,
             sc.label, s_st[s_scene].scale, s_hold ? "HELD" : "");
    if (!s_hold) snprintf(a + strlen(a), sizeof a - strlen(a), "%lus", (unsigned long)left);
    const uint32_t f = s_frameEma ? s_frameEma : 1;
    snprintf(b, sizeof b, "%lu.%lums %lufps crowd %lu.%lu bg %lu.%lu",
             (unsigned long)(f / 1000), (unsigned long)(f % 1000 / 100), (unsigned long)(1000000UL / f),
             (unsigned long)(s_drawEma / 1000), (unsigned long)(s_drawEma % 1000 / 100),
             (unsigned long)(Theme::backgroundUs() / 1000), (unsigned long)(Theme::backgroundUs() % 1000 / 100));
    t.setTextSize(1);
    t.setTextWrap(false);
    t.fillRect(2, 18, t.textWidth(b) > t.textWidth(a) ? t.textWidth(b) + 6 : t.textWidth(a) + 6, 21, Theme::BG);
    t.setTextColor(Theme::VAPOR_YELLOW, Theme::BG);
    t.setCursor(5, 20);
    t.print(a);
    t.setTextColor(Theme::CYAN, Theme::BG);
    t.setCursor(5, 30);
    t.print(b);
    (void)now;
}

void drawResults(TFT_eSPI& t) {
    const int w = t.width(), h = t.height();
    t.fillRect(2, 2, w - 4, h - 4, Theme::BG);
    t.drawRect(2, 2, w - 4, h - 4, Theme::VAPOR_PINK);
    t.setTextSize(1);
    t.setTextWrap(false);
    t.setTextColor(Theme::VAPOR_PINK, Theme::BG);
    t.setCursor(8, 7);
    t.print("CROWD BENCH  (tap to close)");
    t.setTextColor(Theme::W95_LIGHT, Theme::BG);
    t.setCursor(8, 21);
    t.print("scene     size fps worst  frame crowd");
    uint32_t heap = 0xFFFFFFFFu, block = 0xFFFFFFFFu;
    for (uint8_t i = 0; i < SCENE_N; i++) {
        const Stats& s = s_st[i];
        char row[56];
        if (s.frames) {
            const uint32_t fr = (uint32_t)(s.sumFrame / s.frames);
            snprintf(row, sizeof row, "%-9s %4.2f %3lu %3lu %6.1f %5.1f", SCENES[i].label, s.scale,
                     (unsigned long)(1000000UL / (fr ? fr : 1)),
                     (unsigned long)(1000000UL / (s.maxFrame ? s.maxFrame : 1)),
                     fr / 1000.0f, (float)s.sumDraw / s.frames / 1000.0f);
            if (s.minHeap < heap) heap = s.minHeap;
            if (s.minBlock < block) block = s.minBlock;
        } else {
            snprintf(row, sizeof row, "%-9s  --", SCENES[i].label);
        }
        t.setTextColor(i % 2 ? Theme::WHITE : Theme::CYAN, Theme::BG);
        t.setCursor(8, 33 + i * 11);
        t.print(row);
    }
    char foot[48];
    snprintf(foot, sizeof foot, "lowest heap %lu KB, block %lu KB",
             (unsigned long)(heap == 0xFFFFFFFFu ? 0 : heap / 1024),
             (unsigned long)(block == 0xFFFFFFFFu ? 0 : block / 1024));
    t.setTextColor(Theme::VAPOR_YELLOW, Theme::BG);
    t.setCursor(8, 33 + SCENE_N * 11 + 4);
    t.print(foot);
}

}  // namespace

bool active() { return s_on; }

void draw(TFT_eSPI& t, uint32_t now, int top, int floorY) {
    if (!s_announced) {
        s_announced = true;
        // The run that starts itself at boot needs its counters cleared too.
        // Without this the heap minimums stay at their zero-initialised value
        // and every line reports "heap 0 / block 0".
        resetStats();
        Serial.printf("[crowd] benchmark: %u scenes, %lu s each; tap to skip, CROWD OFF to stop\n",
                      (unsigned)SCENE_N, (unsigned long)(SCENE_MS / 1000));
    }
    if (s_results) return;                  // the table is drawn over the top
    const uint32_t t0 = micros();
    s_st[s_scene].scale = drawScene(t, SCENES[s_scene], now, top, floorY);
    s_drawUs = micros() - t0;
    s_drawEma = ema(s_drawEma, s_drawUs);
    s_drew = true;

    // Time is counted in frames drawn, so an alert or a trip to another
    // screen does not eat into a scene.
    if (s_lastAt) {
        uint32_t d = now - s_lastAt;
        if (d > 200) d = 200;
        s_sceneMs += d;
    }
    s_lastAt = now;
    if (s_sceneMs >= SCENE_MS) {
        if (!s_hold) {
            nextScene(now);
        } else {
            // Held: a line every ten seconds, each from fresh numbers.
            printScene(s_scene);
            s_sceneMs = 0;
            memset(&s_st[s_scene], 0, sizeof s_st[s_scene]);
            s_st[s_scene].minHeap = s_st[s_scene].minBlock = 0xFFFFFFFFu;
            s_st[s_scene].scale = 0;
        }
    }
}

void drawOver(TFT_eSPI& t, uint32_t now) {
    if (!s_on) return;
    if (s_results) {
        drawResults(t);
        if (now - s_resultsAt > RESULTS_MS) s_on = false;
        return;
    }
    overlay(t, now);
}

void noteFrame(uint32_t frameUs, uint32_t pushUs) {
    if (!s_on || !s_drew) { s_lastAt = 0; return; }
    s_drew = false;
    s_frameEma = ema(s_frameEma, frameUs);
    if (s_results || s_sceneMs < WARM_MS) return;
    Stats& s = s_st[s_scene];
    s.frames++;
    s.sumFrame += frameUs;
    s.sumDraw  += s_drawUs;
    s.sumPush  += pushUs;
    s.sumBg    += Theme::backgroundUs();
    if (frameUs > s.maxFrame) s.maxFrame = frameUs;
    const uint32_t heap = ESP.getFreeHeap();
    const uint32_t block = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    if (heap < s.minHeap) s.minHeap = heap;
    if (block < s.minBlock) s.minBlock = block;
}

void tap() {
    if (s_results) { s_on = false; return; }
    nextScene(millis());
}

void command(const char* args) {
    while (*args == ' ') args++;
    if (!*args) { begin(); Serial.println("[crowd] started"); return; }
    if (!strncasecmp(args, "OFF", 3)) { s_on = false; Serial.println("[crowd] off"); return; }
    const int n = atoi(args);
    if (n >= 1 && n <= SCENE_N) {
        if (!s_on) resetStats();
        s_on = true; s_results = false; s_hold = true;
        s_scene = (uint8_t)(n - 1);
        s_sceneMs = 0;
        memset(&s_st[s_scene], 0, sizeof s_st[s_scene]);
        s_st[s_scene].minHeap = s_st[s_scene].minBlock = 0xFFFFFFFFu;
        Serial.printf("[crowd] holding %s; CROWD to run them all, CROWD OFF to stop\n", SCENES[s_scene].label);
        return;
    }
    Serial.printf("[crowd] CROWD runs all %u scenes, CROWD N holds one, CROWD OFF stops\n", (unsigned)SCENE_N);
}

}  // namespace CrowdBench
#endif  // CROWD_BENCH
