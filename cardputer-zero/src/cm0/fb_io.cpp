// SquachWatch-CM0 -- the panel and the keyboard without a compositor
// (see fb_io.h).
#include "fb_io.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <string>
#include <sys/auxv.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

namespace {

int      s_fb = -1;
uint8_t* s_map = nullptr;
size_t   s_mapLen = 0;
uint32_t s_stride = 0, s_xres = 0, s_yres = 0;
std::vector<uint16_t> s_last;

int  s_kbd = -1;
bool s_grabbed = false;

bool secure() { return getauxval(AT_SECURE) != 0; }

bool openFb() {
    const int fd = open("/dev/fb0", O_RDWR | O_CLOEXEC);
    if (fd < 0) { std::fprintf(stderr, "[fb] /dev/fb0: %s\n", strerror(errno)); return false; }
    struct fb_var_screeninfo v;
    struct fb_fix_screeninfo f;
    if (ioctl(fd, FBIOGET_VSCREENINFO, &v) != 0 || ioctl(fd, FBIOGET_FSCREENINFO, &f) != 0) {
        std::fprintf(stderr, "[fb] screeninfo: %s\n", strerror(errno));
        close(fd); return false;
    }
    if (v.bits_per_pixel != 16 || v.red.length != 5 || v.green.length != 6 || v.blue.length != 5 ||
        v.red.offset != 11) {
        std::fprintf(stderr, "[fb] not RGB565 (bpp %u, r %u/%u)\n", v.bits_per_pixel, v.red.offset, v.red.length);
        close(fd); return false;
    }
    void* m = mmap(nullptr, f.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (m == MAP_FAILED) { std::fprintf(stderr, "[fb] mmap: %s\n", strerror(errno)); close(fd); return false; }
    s_fb = fd; s_map = (uint8_t*)m; s_mapLen = f.smem_len;
    s_stride = f.line_length; s_xres = v.xres; s_yres = v.yres;
    std::fprintf(stderr, "[fb] %s %ux%u, stride %u\n", f.id, s_xres, s_yres, s_stride);
    return true;
}

// The keyboard: SQUACH_KBD_DEV, else the event device whose name is the
// Cardputer Zero's key matrix controller.
bool openKbd() {
    std::string path;
    const char* env = secure() ? nullptr : getenv("SQUACH_KBD_DEV");
    if (env && *env) path = env;
    else if (DIR* d = opendir("/dev/input")) {
        while (dirent* e = readdir(d)) {
            if (strncmp(e->d_name, "event", 5) != 0) continue;
            const std::string p = std::string("/dev/input/") + e->d_name;
            const int fd = open(p.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
            if (fd < 0) continue;
            char name[64] = {0};
            ioctl(fd, EVIOCGNAME(sizeof name - 1), name);
            close(fd);
            if (!strcmp(name, "tca8418c")) { path = p; break; }
        }
        closedir(d);
    }
    if (path.empty()) { std::fprintf(stderr, "[kbd] no tca8418c keyboard found\n"); return false; }
    const int fd = open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) { std::fprintf(stderr, "[kbd] %s: %s\n", path.c_str(), strerror(errno)); return false; }
    s_kbd = fd;
    // Exclusive: our keys are not also typed into the launcher or labwc.
    // A failure here is not fatal (someone else holds the grab) -- logged.
    s_grabbed = ioctl(fd, EVIOCGRAB, (void*)1) == 0;
    std::fprintf(stderr, "[kbd] %s%s\n", path.c_str(), s_grabbed ? " (grabbed)" : " (NOT grabbed: shared)");
    return true;
}

}   // namespace

namespace cm0 {

bool fbIoOpen() {
    if (!openFb()) return false;
    if (!openKbd()) { fbIoClose(); return false; }
    return true;
}

void fbIoClose() {
    if (s_kbd >= 0) {
        if (s_grabbed) ioctl(s_kbd, EVIOCGRAB, (void*)0);
        close(s_kbd);
        s_kbd = -1; s_grabbed = false;
    }
    if (s_map) { munmap(s_map, s_mapLen); s_map = nullptr; }
    if (s_fb >= 0) { close(s_fb); s_fb = -1; }
    s_last.clear();
}

void fbPush(const std::vector<uint16_t>& px, int w, int h) {
    if (!s_map || w <= 0 || h <= 0 || px.size() < (size_t)w * h) return;
    if (s_last.size() == px.size() && !memcmp(s_last.data(), px.data(), px.size() * 2)) return;
    const uint32_t cw = (uint32_t)w < s_xres ? (uint32_t)w : s_xres;
    const uint32_t ch = (uint32_t)h < s_yres ? (uint32_t)h : s_yres;
    for (uint32_t y = 0; y < ch; y++) {
        const size_t off = (size_t)y * s_stride;
        if (off + cw * 2 > s_mapLen) break;
        memcpy(s_map + off, px.data() + (size_t)y * w, cw * 2);
    }
    s_last = px;
}

void fbClear() {
    if (s_map) memset(s_map, 0, s_mapLen);
    s_last.clear();
}

int kbdFd() { return s_kbd; }

void kbdRead(std::vector<KeyEvent>& out) {
    if (s_kbd < 0) return;
    struct input_event ev[32];
    for (;;) {
        const ssize_t n = read(s_kbd, ev, sizeof ev);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return;
        for (size_t i = 0; i < (size_t)n / sizeof ev[0]; i++)
            if (ev[i].type == EV_KEY)
                out.push_back({ ev[i].code, ev[i].value != 0, ev[i].value == 2 });
    }
}

}   // namespace cm0
