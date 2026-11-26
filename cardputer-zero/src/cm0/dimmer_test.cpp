// dimmer_test -- auto-dim (dimmer.cpp) on a fake backlight directory: dims
// after the idle time, the waking key is swallowed through its release,
// other keys pass, exit restores the user's level, never 0.
#include "dimmer.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unistd.h>

static int g_checks = 0, g_failures = 0;
#define CHECK(cond, what) do { g_checks++; if (cond) std::printf("ok  : %s\n", what); \
    else { g_failures++; std::printf("FAIL: %s\n", what); } } while (0)

static std::string g_dir;
static void put(const char* f, const char* v) {
    FILE* o = fopen((g_dir + "/" + f).c_str(), "wb"); fputs(v, o); fclose(o);
}
static int level() {
    FILE* i = fopen((g_dir + "/brightness").c_str(), "rb");
    char b[16] = {0};
    if (i) { size_t n = fread(b, 1, sizeof b - 1, i); b[n] = 0; fclose(i); }
    return atoi(b);
}

int main() {
    CHECK(cm0::dimLevelFor(30, 100) == 2, "dim level: 2% of 100");
    CHECK(cm0::dimLevelFor(1, 100) == 1, "dim level: never above the user's level");
    CHECK(cm0::dimLevelFor(30, 10) == 1, "dim level: never 0 (small range)");
    CHECK(cm0::dimLevelFor(200, 255) == 5, "dim level: 255 range -> 5 (half catputer's 10)");

    char tmpl[] = "/tmp/dimmer_test.XXXXXX";
    if (!mkdtemp(tmpl)) { std::perror("mkdtemp"); return 1; }
    g_dir = tmpl;

    CHECK(!cm0::dimmerOpenAt(g_dir.c_str(), 0), "no brightness file: dimming off");

    put("max_brightness", "100\n");
    put("brightness", "30\n");
    const uint32_t t0 = 1000;
    CHECK(cm0::dimmerOpenAt(g_dir.c_str(), t0), "open on a backlight dir");

    cm0::dimmerTick(t0 + cm0::DIM_AFTER_MS - 1);
    CHECK(!cm0::dimmed() && level() == 30, "not dimmed before the idle time");
    CHECK(!cm0::dimmerKey(28, true, t0 + 30000), "a key while bright passes through");
    CHECK(!cm0::dimmerKey(28, false, t0 + 30100), "...and its release");
    cm0::dimmerTick(t0 + cm0::DIM_AFTER_MS + 5000);
    CHECK(!cm0::dimmed(), "a key restarts the idle time");
    const uint32_t t1 = t0 + 30100 + cm0::DIM_AFTER_MS;
    cm0::dimmerTick(t1);
    CHECK(cm0::dimmed() && level() == 2, "dimmed to 2 after 60 s without a key");

    // Wake with ESC (1): swallowed down, repeats and up; the next key passes.
    CHECK(cm0::dimmerKey(1, true, t1 + 10), "waking key down swallowed");
    CHECK(!cm0::dimmed() && level() == 30, "...and the user's level is back");
    CHECK(cm0::dimmerKey(1, true, t1 + 500), "...its auto-repeat swallowed");
    CHECK(cm0::dimmerKey(1, false, t1 + 900), "...its release swallowed");
    CHECK(!cm0::dimmerKey(1, true, t1 + 2000), "the NEXT ESC press goes to the app");
    CHECK(!cm0::dimmerKey(1, false, t1 + 2100), "...and its release");

    // The user changed brightness meanwhile (launcher settings): that level
    // is the one restored.
    put("brightness", "55\n");
    const uint32_t t2 = t1 + 2100 + cm0::DIM_AFTER_MS;
    cm0::dimmerTick(t2);
    CHECK(cm0::dimmed() && level() == 2, "dims again after another idle minute");
    cm0::dimmerClose();
    CHECK(level() == 55, "exit while dimmed restores the current user level (55)");

    // The app's own BRIGHTNESS (the firmware's 0..255 duty) is the awake
    // level; the dim still overrides it; exit gives the launcher its own.
    CHECK(cm0::levelForDuty(0, 100) == 1, "duty 0: never 0 (screen off to the launcher)");
    CHECK(cm0::levelForDuty(32, 100) == 13, "duty 32 (BRIGHTNESS minimum) of 100 -> 13");
    CHECK(cm0::levelForDuty(255, 100) == 100, "duty 255 -> full");
    put("brightness", "30\n");
    const uint32_t t3 = 500000;
    CHECK(cm0::dimmerOpenAt(g_dir.c_str(), t3), "reopen for the app level");
    cm0::dimmerSetAppDuty(255);
    CHECK(level() == 100, "BRIGHTNESS 255: the panel goes to full");
    cm0::dimmerSetAppDuty(128);
    CHECK(level() == 50, "BRIGHTNESS 128: half");
    cm0::dimmerTick(t3 + cm0::DIM_AFTER_MS + 1);
    CHECK(cm0::dimmed() && level() == 2, "the idle dim still wins over the app level");
    cm0::dimmerSetAppDuty(64);
    CHECK(level() == 2, "a new app level while dimmed waits");
    CHECK(cm0::dimmerKey(28, true, t3 + cm0::DIM_AFTER_MS + 100), "a key wakes it (swallowed)");
    CHECK(!cm0::dimmed() && level() == 25, "...to the app's latest level (64 -> 25)");
    cm0::dimmerKey(28, false, t3 + cm0::DIM_AFTER_MS + 200);
    cm0::dimmerSetAppDuty(16);
    CHECK(level() == 6, "POWER SAVER's dim duty (16) dims the real panel");
    cm0::dimmerClose();
    CHECK(level() == 30, "exit puts back the launcher's level, not the app's");

    // Screen already off (brightness 0) when the minute is up: left alone.
    put("brightness", "0\n");
    CHECK(cm0::dimmerOpenAt(g_dir.c_str(), 0), "reopen");
    cm0::dimmerTick(cm0::DIM_AFTER_MS + 1);
    CHECK(!cm0::dimmed() && level() == 0, "backlight already 0: not touched");
    cm0::dimmerClose();

    unlink((g_dir + "/brightness").c_str());
    unlink((g_dir + "/max_brightness").c_str());
    rmdir(g_dir.c_str());
    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
