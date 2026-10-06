// A deliberately small interface, sharing the production radio engine.
#include "mini_scanner.h"
#if defined(SQW_MINI)
#include "settings.h"
#include "theme.h"
#include "status_light.h"
#include "clock.h"
#include "ignore_list.h"
#include "bingo.h"
#include "dex.h"
#include "regulars.h"
#include "privacy.h"
#include "meshtalk.h"
#include "single_button.h"
#include "battery_status.h"
#include <esp_heap_caps.h>

namespace MiniScanner {
namespace {
static_assert(TFT_WIDTH == 128 && TFT_HEIGHT == 128, "Mini scanner requires a 128x128 panel");
TFT_eSprite* frame = nullptr;
uint8_t page = 0, logRow = 0, denseStart = 0;
constexpr uint8_t denseRows = 11;
SingleButton button;
bool radioInit = false, psramOk = false;
uint32_t drawnAt = 0, reportedAt = 0;
uint32_t seenAt = 0, alertAt = 0;
uint8_t seenMac[6] = {};
Detection alert = {};
bool haveAlert = false;

void line(TFT_eSPI& d, int y, const char* text, uint16_t color = Theme::WHITE) {
    // Built-in 6x8 font: at most 20 characters fit inside the 128px border.
    char clipped[21];
    snprintf(clipped, sizeof clipped, "%.20s", text);
    d.setTextColor(color, Theme::BG);
    d.drawString(clipped, 4, y, 1);
}

void draw(DetectionEngine& e, uint32_t now) {
    TFT_eSPI& d = *frame;
    d.fillScreen(Theme::BG);
    d.drawRect(0, 0, 128, 128, Theme::CYAN);
    line(d, 5, "SQUACHWATCH", Theme::CYAN);
    auto battery = BatteryStatus::reading();
    uint16_t batteryColor = battery.level == BatteryStatus::Level::LOW_BATTERY ? Theme::RED : Theme::GREEN;
    d.drawRect(105, 5, 17, 9, batteryColor);
    d.fillRect(122, 7, 2, 5, batteryColor);
    unsigned bars = battery.level == BatteryStatus::Level::FULL ? 3 :
                    battery.level == BatteryStatus::Level::MID ? 2 :
                    battery.level == BatteryStatus::Level::LOW_BATTERY ? 1 : 0;
    for (unsigned i = 0; i < bars; ++i) d.fillRect(107 + i * 5, 7, 3, 5, batteryColor);
    if (battery.level == BatteryStatus::Level::CHARGING) {
        d.drawLine(115, 6, 110, 10, Theme::AMBER);
        d.drawLine(110, 10, 116, 10, Theme::AMBER);
        d.drawLine(116, 10, 112, 13, Theme::AMBER);
    } else if (battery.level == BatteryStatus::Level::UNKNOWN) {
        d.setTextColor(Theme::AMBER, Theme::BG); d.drawString("?", 111, 6, 1);
    }
    d.drawFastHLine(4, 18, 120, Theme::PURPLE);
    char b[32];
    const bool flashing = page != 2 && haveAlert && now - alertAt < 5000;
    if (flashing) {
        line(d, 25, "DETECTION", Theme::colorFor(alert.type));
        line(d, 41, detectionTypeName(alert.type), Theme::colorFor(alert.type));
        line(d, 57, vendorText(alert));
        snprintf(b, sizeof b, "%d dBm  %s", alert.rssi, alert.channel ? "WiFi" : "BLE");
        line(d, 73, b);
        line(d, 93, "Signature match", Theme::AMBER);
    } else if (page == 0) {
        line(d, 25, radioInit ? "SCANNING WiFi + BLE" : "RADIO INIT FAILED", Theme::GREEN);
        unsigned live = 0;
        for (unsigned i = 1; i < (unsigned)DetectionType::COUNT; ++i)
            live += e.countByType((DetectionType)i);
        snprintf(b, sizeof b, "Live %u  Log %u", live, e.logCount()); line(d, 43, b);
        snprintf(b, sizeof b, "Lifetime %lu", (unsigned long)e.lifetimeTotal()); line(d, 59, b);
        const Detection* latest = e.latest();
        line(d, 77, latest ? detectionTypeName(latest->type) : "No detections yet", Theme::AMBER);
        snprintf(b, sizeof b, "Up %lu s", (unsigned long)(now / 1000)); line(d, 95, b);
    } else if (page == 1) {
        if (logRow >= e.logCount()) logRow = 0;
        const Detection* row = e.logAt(logRow);
        snprintf(b, sizeof b, "LOG %u/%u", row ? logRow + 1 : 0, e.logCount()); line(d, 25, b);
        if (row) {
            line(d, 41, detectionTypeName(row->type), Theme::colorFor(row->type));
            line(d, 57, vendorText(*row));
            char name[24]; line(d, 73, Privacy::name(row->name, name, sizeof name));
            snprintf(b, sizeof b, "%d dBm %s ch%u", row->rssi, row->channel ? "WiFi" : "BLE", row->channel);
            line(d, 89, b);
        } else line(d, 49, "No detections yet");
    } else if (page == 2) {
        if (denseStart >= e.logCount()) denseStart = 0;
        snprintf(b, sizeof b, "LOG %u-%u/%u", e.logCount() ? denseStart + 1 : 0,
                 unsigned(denseStart + denseRows < e.logCount() ? denseStart + denseRows : e.logCount()), e.logCount());
        line(d, 19, b, Theme::CYAN);
        // Eleven rows, with no padding: 9-character identity, 6-character
        // last-seen stamp and 4-character RSSI fill the 20-column font.
        for (uint8_t i = 0; i < denseRows; ++i) {
            const Detection* row = e.logAt(denseStart + i);
            if (!row) break;
            char name[24], stamp[16];
            const char* label = Privacy::name(row->name, name, sizeof name);
            if (!label[0]) label = vendorText(*row);
            if (!label[0]) label = detectionTypeName(row->type);
            Clock::formatStamp(row->lastSeen, stamp, sizeof stamp);
            // Clock's uptime stamp grows past six columns after 1000 min.
            // Keep a complete time, rather than cutting off its seconds.
            if (!Clock::trusted() && row->lastSeen / 60000u >= 1000) {
                uint32_t minutes = row->lastSeen / 60000u;
                if (minutes < 6000)
                    snprintf(stamp, sizeof stamp, "%luh%02lum",
                             (unsigned long)(minutes / 60), (unsigned long)(minutes % 60));
                else snprintf(stamp, sizeof stamp, "%lud%02luh",
                              (unsigned long)(minutes / 1440), (unsigned long)(minutes / 60 % 24));
            }
            snprintf(b, sizeof b, "%-9.9s %6.6s%4d", label, stamp, row->rssi);
            line(d, 27 + i * 8, b, Theme::colorFor(row->type));
        }
        if (!e.logCount()) line(d, 43, "No detections yet");
    } else {
        line(d, 25, "RADIO / MEMORY");
        snprintf(b, sizeof b, "WiFi frames %lu", (unsigned long)wifiFramesSeen()); line(d, 41, b);
        snprintf(b, sizeof b, "BLE adverts/s %lu", (unsigned long)advertRate()); line(d, 57, b);
        snprintf(b, sizeof b, "Heap %lu KB", (unsigned long)(ESP.getFreeHeap() / 1024)); line(d, 73, b);
        snprintf(b, sizeof b, "PSRAM %lu KB", (unsigned long)(ESP.getPsramSize() / 1024)); line(d, 89, b);
        // Red, green, blue bars and the border expose color/offset mistakes.
        d.fillRect(4, 101, 38, 6, TFT_RED);
        d.fillRect(44, 101, 38, 6, TFT_GREEN);
        d.fillRect(84, 101, 38, 6, TFT_BLUE);
    }
    line(d, 115, page == 1 ? "Tap:page Hold:row" : page == 2 ? "Tap:page Hold:more" : "Tap:page Hold:home", Theme::CYAN);
    frame->pushSprite(0, 0);
}

void console(TFT_eSPI& d) {
    // Bounded and nonblocking, even if a host never sends a newline.
    static char command[16]; static uint8_t used = 0; static bool overflow = false;
    for (uint8_t n = 0; n < 32 && Serial.available(); ++n) {
        char c = Serial.read();
        if (c == '\r') continue;
        if (c == '\n') {
            command[used] = 0;
            if (!overflow && strcmp(command, "LOG") == 0) logDump();
            else if (!overflow && strcmp(command, "RADIO") == 0) radioReport(false);
            else if (!overflow && strcmp(command, "BATTERY") == 0) {
                auto v = BatteryStatus::reading();
                Serial.printf("[battery] level=%u voltage=%u mV USB ADC=%u mV pulses=%lu CHRG=%d\n",
                              unsigned(v.level), v.millivolts, v.usbMillivolts,
                              (unsigned long)v.pulses, digitalRead(SQW_CHARGING_PIN));
            }
            else if (!overflow && strcmp(command, "LED") == 0) StatusLight::test(millis());
            else if (!overflow && strcmp(command, "INVERT") == 0)
                { Settings::toggleInvert(); d.invertDisplay(bool(SQW_PANEL_INVERTED) != Settings::inverted()); }
            else Serial.println("Commands: LOG RADIO LED INVERT BATTERY");
            used = 0; overflow = false;
        } else if (used < sizeof command - 1) command[used++] = c;
        else overflow = true;
    }
}
}

void begin(TFT_eSPI& d, DetectionEngine& e) {
    Serial.begin(SERIAL_BAUD);
    Serial.setTxTimeoutMs(0);
    delay(200);
    Serial.printf("\nSquachWatch %s\n%s\n", FIRMWARE_VERSION, USER_SETUP_INFO);
    Settings::load();
    Clock::begin();
    BatteryStatus::begin();
    IgnoreList::begin();
    pinMode(SQW_BUTTON_PIN, INPUT_PULLUP);
    // Leave audio, power control and USB pins alone; battery inputs are read-only.
    pinMode(SQW_BACKLIGHT_PIN, OUTPUT);
    digitalWrite(SQW_BACKLIGHT_PIN, LOW);
    d.init();
    d.setRotation(SQW_PANEL_ROTATION);
    d.invertDisplay(bool(SQW_PANEL_INVERTED) != Settings::inverted());
    d.fillScreen(Theme::BG);
    ledcSetup(0, 5000, 8);
    ledcAttachPin(SQW_BACKLIGHT_PIN, 0);
    ledcWrite(0, 64); // conservative brightness during radio start
    StatusLight::begin();
    StatusLight::boot(millis());
    // Verify an actual external RAM allocation, rather than only a build flag.
    uint8_t* probe = (uint8_t*)heap_caps_malloc(1024, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    psramOk = probe != nullptr;
    if (probe) {
        for (unsigned i = 0; i < 1024; ++i) probe[i] = uint8_t(i ^ 0xA5);
        for (unsigned i = 0; i < 1024; ++i) if (probe[i] != uint8_t(i ^ 0xA5)) psramOk = false;
        free(probe);
    }
    Serial.printf("[mini] PSRAM %lu bytes, allocation/readback %s\n",
                  (unsigned long)ESP.getPsramSize(), psramOk ? "PASS" : "FAIL");
    if (!psramOk) line(d, 55, "PSRAM check FAILED", Theme::RED);
    if (!psramOk) {
        Serial.println("[mini] Radios not started: check OPI PSRAM and board revision.");
        return;
    }
    static TFT_eSprite canvas(&d);
    canvas.setColorDepth(16);
    if (!canvas.createSprite(128, 128)) {
        psramOk = false;
        line(d, 55, "Frame alloc FAILED", Theme::RED);
        Serial.println("[mini] Frame allocation failed; radios not started.");
        return;
    }
    frame = &canvas;
    Serial.println("[mini] 128x128 RGB565 framebuffer allocated");
    radioInit = e.init();
    Bingo::begin(e); Dex::begin(); Regulars::begin();
    Mesh::begin(); MeshTalk::begin();
    Serial.println("[mini] Button active-low; commands: LOG RADIO LED INVERT BATTERY");
    draw(e, millis());
}

void tick(TFT_eSPI& d, DetectionEngine& e) {
    uint32_t now = millis();
    if (!psramOk) { delay(20); return; }
    if (radioInit) e.loop();
    Clock::tick(now);
    BatteryStatus::tick(now);
    Mesh::tick(now); MeshTalk::tick(now);
    Bingo::tick(now); Dex::tick(now); Regulars::tick(now);
    console(d);
    const SingleButton::Event gesture = button.tick(digitalRead(SQW_BUTTON_PIN) == LOW, now);
    if (gesture == SingleButton::Event::TAP) {
        page = (page + 1) % 4; haveAlert = false; drawnAt = 0;
        Serial.printf("[mini] button tap, page %u\n", page);
    } else if (gesture == SingleButton::Event::HOLD) {
        haveAlert = false;
        if (page == 1 && e.logCount()) logRow = (logRow + 1) % e.logCount();
        else if (page == 2 && e.logCount()) {
            denseStart = denseStart + denseRows < e.logCount() ? denseStart + denseRows : 0;
        } else page = 0;
        drawnAt = 0;
        Serial.printf("[mini] button hold, page %u row %u\n", page, logRow);
    }
    const Detection* latest = e.latest();
    if (latest && (latest->firstSeen != seenAt || memcmp(latest->mac, seenMac, 6))) {
        seenAt = latest->firstSeen; memcpy(seenMac, latest->mac, 6);
        Serial.printf("[mini] %s %02x:%02x:%02x:%02x:%02x:%02x %d dBm %s %s\n",
                      detectionTypeName(latest->type), latest->mac[0], latest->mac[1], latest->mac[2],
                      latest->mac[3], latest->mac[4], latest->mac[5], latest->rssi,
                      latest->channel ? "WiFi" : "BLE", vendorText(*latest));
        if (latest->conf >= Settings::minConfidence() && !IgnoreList::silenced(latest->mac) &&
            !(latest->channel == 0 && e.spam().active((uint8_t)latest->type, now) &&
              !e.spam().takeAnnounce((uint8_t)latest->type)) &&
            e.alertGate(latest->mac, Settings::autoQuietAfter(), false) != DetectionEngine::AlertGate::HOLD) {
            alert = *latest; haveAlert = true; alertAt = now; drawnAt = 0;
        }
    }
    StatusLight::Context context = {};
    context.alert = haveAlert && now - alertAt < 5000;
    context.alertColor = Theme::colorFor(alert.type);
    StatusLight::tick(now, context);
    if (!drawnAt || now - drawnAt >= 250) { draw(e, now); drawnAt = now; }
    if (now - reportedAt >= 10000) { radioReport(false); reportedAt = now; }
    delay(5);
}
}
#endif
