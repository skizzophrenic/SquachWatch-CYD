// SquachWatch-CM0 -- auto-dim while the app is in front (see dimmer.h).
#include "dimmer.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <string>
#include <unistd.h>

namespace {

int      s_fd = -1;
int      s_max = 0;
int      s_user = 0;          // the level to show when awake: the app's, else the launcher's
int      s_launcher = 0;      // the level at open: put back on exit
bool     s_appSet = false;    // the firmware has asked for a level (dimmerSetAppDuty)
bool     s_dimmed = false;
uint16_t s_swallow = 0;       // the key that woke the panel, until its key-up
bool     s_swallowing = false;
uint32_t s_lastKey = 0;

int readInt(int fd) {
    char b[16] = {0};
    const ssize_t n = pread(fd, b, sizeof b - 1, 0);
    return n > 0 ? atoi(b) : -1;
}

bool writeLevel(int level) {
    if (s_fd < 0) return false;
    char b[16];
    const int n = std::snprintf(b, sizeof b, "%d\n", level);
    return pwrite(s_fd, b, (size_t)n, 0) == n;
}

}   // namespace

namespace cm0 {

int dimLevelFor(int level, int max) {
    // 2% of the panel's range (half of catputer's 10/255: on the device the
    // user found 4% brighter than wanted), never above what the user already
    // has, and never 0.
    int d = max * 2 / 100;
    if (d < 1) d = 1;
    if (level > 0 && d > level) d = level;
    return d;
}

bool dimmerOpen(uint32_t nowMs) {
    std::string dir;
    if (DIR* d = opendir("/sys/class/backlight")) {
        while (dirent* e = readdir(d))
            if (e->d_name[0] != '.') { dir = std::string("/sys/class/backlight/") + e->d_name; break; }
        closedir(d);
    }
    if (dir.empty()) { std::fprintf(stderr, "[dim] no backlight: auto-dim off\n"); return false; }
    return dimmerOpenAt(dir.c_str(), nowMs);
}

bool dimmerOpenAt(const char* dirc, uint32_t nowMs) {
    const std::string dir(dirc);
    s_dimmed = false; s_swallowing = false;
    const int mfd = open((dir + "/max_brightness").c_str(), O_RDONLY | O_CLOEXEC);
    s_max = mfd >= 0 ? readInt(mfd) : -1;
    if (mfd >= 0) close(mfd);
    s_fd = open((dir + "/brightness").c_str(), O_RDWR | O_CLOEXEC);
    if (s_fd < 0 || s_max <= 0) {
        std::fprintf(stderr, "[dim] %s/brightness: %s: auto-dim off\n", dir.c_str(),
                     s_fd < 0 ? strerror(errno) : "no max_brightness");
        if (s_fd >= 0) { close(s_fd); s_fd = -1; }
        return false;
    }
    s_user = readInt(s_fd);
    s_launcher = s_user;
    s_appSet = false;
    s_lastKey = nowMs;
    std::fprintf(stderr, "[dim] %s: level %d of %d, dims to %d after %u s idle\n", dir.c_str(),
                 s_user, s_max, dimLevelFor(s_user, s_max), (unsigned)(DIM_AFTER_MS / 1000));
    return true;
}

void dimmerClose() {
    if (s_fd < 0) return;
    // The launcher's level, not the app's BRIGHTNESS: that setting is the
    // app's own, and the OS gets its screen back as it lent it.
    if (s_launcher > 0 && (s_dimmed || s_user != s_launcher)) writeLevel(s_launcher);
    close(s_fd);
    s_fd = -1;
    s_dimmed = false;
}

bool dimmerKey(uint16_t code, bool down, uint32_t nowMs) {
    s_lastKey = nowMs;
    if (s_swallowing && code == s_swallow) {
        if (!down) s_swallowing = false;   // the waking key's release: done
        return true;
    }
    if (s_dimmed && down) {
        if (s_user > 0) writeLevel(s_user);
        s_dimmed = false;
        s_swallow = code;
        s_swallowing = true;
        return true;
    }
    return false;
}

bool dimmerWake(uint32_t nowMs) {
    s_lastKey = nowMs;
    if (!s_dimmed) return false;
    if (s_user > 0) writeLevel(s_user);
    s_dimmed = false;
    return true;
}

void dimmerTick(uint32_t nowMs) {
    if (s_fd < 0 || s_dimmed) return;
    if ((int32_t)(nowMs - s_lastKey) < (int32_t)DIM_AFTER_MS) return;
    // Until the app has set its own level, whatever the level is now is the
    // user's (the launcher's brightness setting may have changed it since
    // we started).
    if (!s_appSet) {
        const int cur = readInt(s_fd);
        if (cur > 0) s_user = s_launcher = cur;   // and the one to put back
    }
    if (s_user <= 0) { s_lastKey = nowMs; return; }   // already off: leave it
    if (writeLevel(dimLevelFor(s_user, s_max))) s_dimmed = true;
    else s_lastKey = nowMs;                          // retry a minute later
}

int levelForDuty(int duty, int max) {
    if (max <= 0) return 0;
    if (duty < 0) duty = 0;
    if (duty > 255) duty = 255;
    int l = (duty * max + 127) / 255;
    if (l < 1) l = 1;   // never 0: that is "screen off" to the launcher
    return l;
}

void dimmerSetAppDuty(int duty) {
    if (s_fd < 0) return;
    const int l = levelForDuty(duty, s_max);
    if (s_appSet && l == s_user) return;
    s_appSet = true;
    s_user = l;
    if (!s_dimmed) writeLevel(s_user);   // dimmed: shown when it wakes
}

bool dimmed() { return s_dimmed; }

}   // namespace cm0
