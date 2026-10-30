#include "ui_diagnostics.h"
#include "cyd_gps_uart.h"
#include "clock.h"
#include "theme.h"
#include "test_util.h"
#include "gnss.h"
#include "band_frame.h"
#include <map>
#include <string>
#include <cstdlib>

// Hardware/UI boundaries only. The renderer and font rasterizer are real.
static uint32_t rxBytes = 0;
static bool portReady = true, bufferReady = true;
namespace CydGpsUart {
uint32_t receivedBytes() { return rxBytes; }
int rxPin() { return 1; }
unsigned uartNumber() { return 2; }
uint32_t baud() { return 9600; }
bool rxBufferReady() { return bufferReady; }
bool receiving() { return portReady; }
}
namespace Theme {
uint16_t BG = 0, CYAN = TFT_CYAN, GREEN = TFT_GREEN, RED = TFT_RED,
         AMBER = TFT_ORANGE, VAPOR_PINK = TFT_MAGENTA, VAPOR_PURPLE = TFT_MAGENTA;
bool compact() { return false; }
void drawTitleBar(TFT_eSPI&, const char*) {}
ButtonBarGeom computeButtonBar(int, int h) { ButtonBarGeom g{}; g.h = 20; g.y = h - 26; return g; }
void drawButton(TFT_eSPI&, int, int, int, int, const char*, bool, uint8_t) {}
}
namespace Clock {
void formatUptime(char* out, size_t n) { snprintf(out, n, "2m"); }
void formatClock(char* out, size_t n) { snprintf(out, n, "--:--"); }
bool trusted() { return false; }
void formatEpochStamp(uint32_t, char* out, size_t n) { snprintf(out, n, "unknown"); }
}
BootHeap bootHeap() { return {}; }

class Display : public TFT_eSPI {
public:
    std::map<int, std::string> lines;
    std::map<int, int> right;
    Display(int w, int h) : TFT_eSPI(w, h) {}
    int16_t drawChar(uint16_t c, int32_t x, int32_t y, uint8_t font = 1) override {
        const int16_t advance = TFT_eSPI::drawChar(c, x, y, font);
        lines[y] += (char)c;
        right[y] = x + advance;
        return advance;
    }
    void resetCapture() { lines.clear(); right.clear(); }
};

static DetectionEngine engine;
static std::string renderPages(Display& t, const DiagnosticsInfo& info) {
    std::string text;
    bool widthOk = true, bodyOk = true;
    for (unsigned page = 0; page < 12; ++page) {
        t.resetCapture();
        uiDiagnosticsTick(t, 10000, engine, info);
        for (const auto& line : t.lines) {
            text += line.second + "\n";
            if (line.second.find("GPS") != std::string::npos ||
                line.second.find("GOOD/BAD") != std::string::npos ||
                line.second.find("LAT:") != std::string::npos ||
                line.second.find("LON:") != std::string::npos ||
                line.second.find("FIX:") != std::string::npos ||
                line.second.find("SOURCE:") != std::string::npos ||
                line.second.find("POSITION:") != std::string::npos ||
                line.second.find("WARDRIVE:") != std::string::npos ||
                line.second.find("WRITTEN:") != std::string::npos ||
                line.second.find("REPEATS:") != std::string::npos ||
                line.second.find("DROPPED:") != std::string::npos ||
                line.second.find("WIGLE:") != std::string::npos) {
                widthOk &= t.right[line.first] <= t.width() - 6;
                bodyOk &= line.first >= 16 && line.first + t.fontHeight() <= t.height() - 42;
            }
        }
        if (info.fresh && std::getenv("SQUACH_DIAGNOSTICS_PREVIEW")) {
            for (const auto& line : t.lines) if (line.second.find("GPS UART:") != std::string::npos) {
                char path[128];
                snprintf(path, sizeof path, "/tmp/elecrow-diagnostics-%dx%d.ppm", t.width(), t.height());
                FILE* file = fopen(path, "wb");
                if (file) {
                    fprintf(file, "P6\n%d %d\n255\n", t.width(), t.height());
                    for (int y = 0; y < t.height(); ++y) for (int x = 0; x < t.width(); ++x) {
                        const uint16_t pixel = t.readPixel(x, y);
                        const uint8_t rgb[] = {(uint8_t)((pixel >> 11) * 255 / 31),
                            (uint8_t)(((pixel >> 5) & 63) * 255 / 63), (uint8_t)((pixel & 31) * 255 / 31)};
                        fwrite(rgb, 1, 3, file);
                    }
                    fclose(file);
                }
            }
        }
        uiDiagnosticsNextPage();
    }
#if defined(CYD_GPS_ELECROW)
    ck("GPS and wardrive rows stay inside display width", widthOk);
    ck("GPS and wardrive rows stay above page cue and BACK", bodyOk);
#else
    (void)widthOk; (void)bodyOk;
#endif
    return text;
}

#if defined(CYD_GPS_ELECROW)
// Real GNSS parser/source transitions feed the actual text renderer. Hardware
// UART health is the only replacement; no synthetic GPS state machine here.
static void navigation(const char* body, uint32_t atMs) {
    unsigned char sum = 0;
    for (const char* p = body; *p; ++p) sum ^= (unsigned char)*p;
    char sentence[160];
    snprintf(sentence, sizeof sentence, "$%s*%02X", body, sum);
    ck("navigation fixture parses", Gnss::sentence(sentence, atMs));
}
static void positionInfo(DiagnosticsInfo& info, uint32_t nowMs) {
    const auto s = Gnss::snapshot();
    const auto& f = s.position;
    const auto sky = Gnss::sky();
    info.gpsGood = Gnss::good(); info.gpsBad = Gnss::bad();
    info.view = sky.view; info.heard = sky.heard; info.used = f.used;
    info.lat7 = f.lat7; info.lon7 = f.lon7; info.accM = f.accM;
    info.fixAgeMs = f.atMs ? nowMs - f.atMs : UINT32_MAX;
    info.source = s.source;
    info.fresh = Gnss::fresh(nowMs); info.faked = Gnss::faked();
}
#endif

// Compare the actual report, including paged wardrive rows near the bottom,
// against the ordinary full-frame image at each orientation.
static void compareBandedDiagnostics() {
#if defined(CYD_GPS_ELECROW)
    for (const auto& size : {std::pair<int,int>{320,240}, {240,320}}) {
        const int w = size.first, h = size.second;
        TFT_eSPI panel(w, h);
        TFT_eSprite reference(&panel), band(&panel);
        reference.setColorDepth(8); reference.createSprite(w, h);
        band.setColorDepth(8); band.createSprite(w, FrameLayout::bufferHeight(w, h));
        DiagnosticsInfo info{};
        info.boardName = "cyd"; info.resetReason = "POWERON";
        info.gpsShown = true; info.fresh = true; info.fixAgeMs = 200;
        info.wdOn = true; info.wdState = 4; info.wdWritten = 12345;
        info.wdFile = "/wigle-20261029-120000.csv";
        bool equal = true;
        for (int page = 0; page < 12; ++page) {
            reference.fillScreen(Theme::BG);
            uiDiagnosticsTick(reference, 10000, engine, info);
            BandFrame::render(band, w, h, Theme::BG,
                [&](TFT_eSprite& t, bool) { uiDiagnosticsTick(t, 10000, engine, info); },
                [&](int y, int rows) {
                    for (int r = 0; r < rows; ++r) for (int x = 0; x < w; ++x)
                        panel.drawPixel(x, y + r, band.readPixel(x, y + r));
                });
            for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x)
                equal &= panel.readPixel(x, y) == reference.readPixel(x, y);
            uiDiagnosticsNextPage();
        }
        ck("all diagnostic pages match full-buffer pixels", equal);
    }
#endif
}

int main() {
#if !defined(CYD_GPS_ELECROW)
    suite("Ordinary GPS diagnostics preserve console and paging behavior");
    Display t(320, 240);
    DiagnosticsInfo info{};
    info.boardName = "cyd"; info.resetReason = "POWERON";
    info.gpsShown = true;
    const std::string text = renderPages(t, info);
    ck("ordinary display does not gain Elecrow paging", !uiDiagnosticsPaged());
    ck("ordinary console guidance remains available", text.find("over serial") != std::string::npos);
    ck("ordinary GPS status remains unchanged", text.find("no data from the module") != std::string::npos);
    ck("ordinary profile has no Elecrow configuration lines", text.find("GPS UART:") == std::string::npos);
#else
    suite("Elecrow diagnostics receiver states and both screen orientations");
    for (const auto& size : {std::pair<int,int>{320,240}, {240,320}}) {
        Display t(size.first, size.second);
        t.setTextFont(2); // The diagnostics renderer must establish readable text.
        DiagnosticsInfo info{};
        info.boardName = "cyd"; info.resetReason = "POWERON";
        info.gpsShown = true; info.fixAgeMs = UINT32_MAX;
        info.gpsGood = 0; info.gpsBad = 2;
        rxBytes = 0; portReady = true; bufferReady = true;
        std::string text = renderPages(t, info);
        ck("fixed receiver configuration is visible", text.find("2 RX1 9600 8N1") != std::string::npos);
        ck("no bytes has a distinct state", text.find("no serial bytes") != std::string::npos);
        ck("page navigation has a visible cue", text.find("tap body") != std::string::npos);
        ck("lower diagnostic sections remain reachable", text.find("CAL RANGE:") != std::string::npos && text.find("LOG:") != std::string::npos && text.find("FRAME:") != std::string::npos);
        ck("untrusted clock does not advertise disabled serial commands", text.find("time not set") != std::string::npos && text.find("over serial") == std::string::npos);
        portReady = false;
        ck("missing UART driver is visible", renderPages(t, info).find("UART unavailable") != std::string::npos);
        portReady = true; bufferReady = false;
        ck("failed buffer setup is visible", renderPages(t, info).find("UART unavailable") != std::string::npos);
        bufferReady = true; rxBytes = 900;
        ck("bytes without valid NMEA differs from silence", renderPages(t, info).find("bytes / no valid NMEA") != std::string::npos);
        info.gpsGood = 6;
        ck("receiving without fix is visible", renderPages(t, info).find("receiving / no fix") != std::string::npos);
        info.fixAgeMs = 200; info.fresh = true;
        info.lat7 = -900000000; info.lon7 = -1800000000; info.accM = 255;
        ck("fresh position is distinguished", renderPages(t, info).find("fresh fix") != std::string::npos);
        info.fresh = false; info.fixAgeMs = 6000;
        ck("stale position is distinguished", renderPages(t, info).find("stale fix") != std::string::npos);
        rxBytes = UINT32_MAX; info.gpsGood = UINT32_MAX; info.gpsBad = UINT32_MAX;
        text = renderPages(t, info);
        ck("full-width received byte counter remains visible", text.find("4294967295 bytes") != std::string::npos);
        ck("full-width sentence counters remain visible", text.find("4294967295 / 4294967295") != std::string::npos);

        suite("Elecrow wardrive state and full counter values fit the screen");
        info.wdOn = true;
        info.wdWritten = info.wdSkipped = info.wdDropped = UINT32_MAX;
        info.wdFile = "/wigle/20261028-1200-001.csv";
        const char* states[] = {"OFF", "NO CARD", "LOW MEMORY", "WAITING FOR FIX", "ON", "WRITE ERROR", "?"};
        for (unsigned state = 0; state < sizeof states / sizeof states[0]; ++state) {
            info.wdState = state;
            text = renderPages(t, info);
            ck("wardrive state remains visible", text.find(std::string("WARDRIVE:") + states[state]) != std::string::npos);
            ck("all written digits are visible", text.find("WRITTEN:4294967295") != std::string::npos);
            ck("all repeat digits are visible", text.find("REPEATS:4294967295") != std::string::npos);
            ck("all dropped digits are visible", text.find("DROPPED:4294967295") != std::string::npos);
            ck("session file remains visible", text.find(info.wdFile) != std::string::npos);
        }

        suite("Actual GNSS phone fallback cannot become a receiver fix on screen");
        Gnss::reset();
        navigation("GNGGA,120000.00,,,,,0,00,99.99,,,,,,", 1000);
        Gnss::phone(420000000, -750000000, 7, 1793188800u, 2000);
        positionInfo(info, 3000);
        text = renderPages(t, info);
        ck("fallback identifies the phone position", text.find("SOURCE:PHONE") != std::string::npos);
        ck("phone fix is explicitly excluded from wardrive", text.find("PHONE (wardrive excluded)") != std::string::npos);
        ck("phone freshness is separate from receiver status", text.find("POSITION:fresh fix") != std::string::npos);
        ck("phone freshness never advertises a receiver fix", text.find("GPS:fresh fix") == std::string::npos);
        ck("physical receiver still reports no receiver fix", text.find("GPS:receiving / no receiver fix") != std::string::npos);
        ck("phone coordinates are visible under their source", text.find("LAT:42.00000") != std::string::npos && text.find("LON:-75.00000") != std::string::npos);
        positionInfo(info, 62000);
        text = renderPages(t, info);
        ck("expired phone is labeled stale under PHONE", text.find("SOURCE:PHONE") != std::string::npos && text.find("POSITION:stale fix") != std::string::npos);

        navigation("GNGGA,120003.00,4300.0000,N,07400.0000,W,1,08,1.0,10.0,M,0.0,M,,", 63000);
        positionInfo(info, 64000);
        text = renderPages(t, info);
        ck("reacquired receiver replaces phone source and coordinates", text.find("SOURCE:RECEIVER") != std::string::npos && text.find("LAT:43.00000") != std::string::npos && text.find("LON:-74.00000") != std::string::npos);
        ck("fresh receiver advertises a receiver fix", text.find("GPS:fresh fix") != std::string::npos);
        positionInfo(info, 68000);
        text = renderPages(t, info);
        ck("receiver becomes stale at five seconds", text.find("GPS:stale fix") != std::string::npos && text.find("POSITION:stale fix") != std::string::npos);
        Gnss::fake(440000000, -730000000, 1793188800u, 70000);
        positionInfo(info, 71000);
        text = renderPages(t, info);
        ck("bench fix has its own excluded source", text.find("SOURCE:BENCH (wardrive excluded)") != std::string::npos);
        ck("bench fix never advertises a receiver fix", text.find("GPS:fresh fix") == std::string::npos && text.find("POSITION:fresh fix") != std::string::npos);
        Gnss::reset();
        positionInfo(info, 72000);
        text = renderPages(t, info);
        ck("reset reports a receiver with no position", text.find("SOURCE:RECEIVER") != std::string::npos && text.find("POSITION:no fix") != std::string::npos && text.find("LAT:") == std::string::npos);
    }
#endif
    compareBandedDiagnostics();
    return report();
}
