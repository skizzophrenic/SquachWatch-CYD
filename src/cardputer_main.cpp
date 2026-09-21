// Detector-first original Cardputer port. See docs/CARDPUTER.md.
// Guarded because the CYD environments compile every source by default.
#if defined(CARDPUTER)
#include <Arduino.h>
#include <TFT_eSPI.h>
#include <esp_heap_caps.h>
#include <esp_wifi.h>
#include <NimBLEDevice.h>
#include <SD.h>
#include "cardputer_keys.h"
#include "detection.h"
#include "settings.h"
#include "clock.h"
#include "squachy.h"
#include "bingo.h"
#include "ignore_list.h"
#include "cardputer_board.h"
#include "cardputer_runtime.h"

namespace {
TFT_eSPI display;
TFT_eSprite canvas(&display);
DetectionEngine engine;
CardputerKeys::Debouncer keys;
constexpr uint8_t selectors[] = {8, 9, 11};
constexpr uint8_t inputs[] = {13, 15, 3, 4, 5, 6, 7};
constexpr uint16_t BG = 0x0801, CYAN = 0x07FF, PINK = 0xF81F;
enum class Page { HOME, LOG, DETAIL, DIAGNOSTICS, WARDROBE, BINGO, SETTINGS, FILTERS, IGNORED, DIARY, HELP };
Page page = Page::HOME;
uint8_t selected = 0;
Detection detail = {};
Detection alert = {};
uint32_t alertAt = 0;
Cardputer::Sightings sightings;
bool haveAlert = false, buffered = false;
uint32_t wifiRate = 0, bleRate = 0, previousWifi = 0, previousBle = 0;
uint32_t lastSample = 0, lastPaint = 0, lastSerial = 0;
uint32_t bootReadyMs = 0;
bool dirty = true;
int dumpRow = -1;
char command[32] = {};
uint8_t commandLen = 0;
bool commandOverflow = false;
char reward[40] = {};
uint32_t rewardAt = 0;
int8_t rewardOutfit = -1;
uint8_t menuRow = 0;
constexpr uint8_t MENU_COUNT = 9;
void handleKey(CardputerKeys::Action a);

void reactTo(const Detection& d) {
    Squachy::trigger(Squachy::Event::DETECTION, d.type, engine.lifetimeTotal(), d.hits, d.rssi, d.conf);
}
void dismissAlert() {
    // Start the reaction when Squachy is visible again; otherwise its timer
    // expires behind the eight-second match card and the player never sees it.
    reactTo(alert);
    haveAlert = false;
    dirty = true;
}
int screenRow = -1;
uint32_t screenAt = 0;

void clearFrame() {
    // TFT_eSPI::fillScreen is non-virtual and uses the base panel's native
    // 135x240 dimensions even through a sprite reference. Clear the actual
    // 240x135 sprite, or everything to the right of x=134 leaves trails.
    if (buffered) canvas.fillSprite(BG);
    else display.fillScreen(BG);
}

// USB bench commands. Drain one row per loop so a full detection log
// never blocks scanning or overfills the USB transmit buffer.
void pollConsole() {
    for (unsigned n = 0; n < 32 && Serial.available(); ++n) {
        const char c = Serial.read();
        if (c == '\r') continue;
        if (c == '\n') {
            command[commandLen] = 0;
            if (!commandOverflow && !strcmp(command, "LOG")) {
                dumpRow = 0;
                Serial.printf("[log] rows=%u\n", engine.logCount());
            } else if (!commandOverflow && !strcmp(command, "STATUS")) {
                lastSerial = millis() - 10000;
                Serial.printf("[game] pets=%lu outfit=%s unlocked=%u bingo=%u lines=%u\n",
                    (unsigned long)Squachy::petCount(), Squachy::outfitName(),
                    Squachy::unlockedOutfitCount(), Bingo::markedCount(), Bingo::linesCalled());
                Serial.printf("[ui] page=%u row=%u battery_mv=%u dimmed=%u brightness=%u saver=%u timeout=%u mode=%s clock=%u\n",
                    (unsigned)page, menuRow, CardputerBoard::batteryMillivolts(), CardputerBoard::dimmed(),
                    Settings::brightness(), Settings::powerSaver(), Settings::screenTimeoutSec(),
                    CardputerBoard::scanModeName(), Clock::trusted());
            } else if (!commandOverflow && !strncmp(command, "TIME ", 5)) {
                uint32_t epoch;
                const bool ok = Cardputer::parseEpoch(command + 5, epoch) && Clock::setEpoch(epoch);
                Serial.printf("[clock] %s\n", ok ? "set" : "invalid epoch"); dirty = true;
            } else if (!commandOverflow && !strncmp(command, "KEY ", 4) && strlen(command) == 5) {
                // Same actions as the keyboard, useful for reproducible UI
                // checks. No synthetic detections or reward unlock command.
                const char* chars = "sld;.e`pob ckjh,/ix";
                const CardputerKeys::Action actions[] = {
                    CardputerKeys::Action::HOME, CardputerKeys::Action::LOG, CardputerKeys::Action::DIAGNOSTICS,
                    CardputerKeys::Action::UP, CardputerKeys::Action::DOWN, CardputerKeys::Action::OPEN,
                    CardputerKeys::Action::BACK, CardputerKeys::Action::PET, CardputerKeys::Action::OUTFIT,
                    CardputerKeys::Action::BINGO, CardputerKeys::Action::SHOW, CardputerKeys::Action::SHADES,
                    CardputerKeys::Action::SETTINGS, CardputerKeys::Action::DIARY,
                    CardputerKeys::Action::HELP, CardputerKeys::Action::LEFT, CardputerKeys::Action::RIGHT,
                    CardputerKeys::Action::IGNORE, CardputerKeys::Action::SNOOZE};
                const char* match = strchr(chars, command[4]);
                if (match) handleKey(actions[match - chars]);
            } else if (!commandOverflow && !strcmp(command, "FRAMECHECK") && buffered) {
                // Exercise the same clear as every animation frame against
                // the real TFT library and all pixels, including the far edge.
                screenRow = -1;
                canvas.fillSprite(TFT_CYAN);
                clearFrame();
                const uint16_t expected = canvas.readPixel(0, 0);
                unsigned stale = 0;
                for (int y = 0; y < 135; ++y)
                    for (int x = 0; x < 240; ++x)
                        if (canvas.readPixel(x, y) != expected) ++stale;
                Serial.printf("[framecheck] %s pixels=32400 stale=%u\n",
                    expected != TFT_CYAN && !stale ? "PASS" : "FAIL", stale);
                dirty = true;
            } else if (!commandOverflow && !strcmp(command, "SCREEN") && buffered) {
                screenRow = 0; screenAt = millis();
            } else if (!commandOverflow && !strcmp(command, "SD")) {
                char path[32];
                snprintf(path, sizeof path, "/squachwatch-%lu.log", (unsigned long)(millis() / 86400000UL));
                File f = engine.sd().ready() ? SD.open(path, FILE_READ) : File();
                if (!f) Serial.println("[sdcheck] no current log file");
                else {
                    const size_t size = f.size();
                    char tail[193] = {};
                    f.seek(size > 192 ? size - 192 : 0);
                    const size_t read = f.readBytes(tail, sizeof tail - 1);
                    tail[read] = 0;
                    f.close();
                    Serial.printf("[sdcheck] %s bytes=%u tail:\n%s\n", path, (unsigned)size, tail);
                }
            } else Serial.println("[console] STATUS LOG SD SCREEN FRAMECHECK TIME <epoch> KEY <key>");
            commandLen = 0;
            commandOverflow = false;
        } else if (commandLen < sizeof command - 1) command[commandLen++] = c;
        else commandOverflow = true;
    }
    // Read back a stable frame one row at a time; radio/keyboard work continues.
    // A disconnected reader must never leave animation frozen indefinitely.
    if (screenRow >= 0 && millis() - screenAt > 5000) screenRow = -1;
    if (screenRow >= 0 && Serial.availableForWrite() >= 1000) {
        char row[985];
        int len = snprintf(row, sizeof row, "[screen] %d ", screenRow);
        const char* hex = "0123456789abcdef";
        for (int x = 0; x < 240; ++x) {
            const uint16_t pixel = canvas.readPixel(x, screenRow);
            for (int shift = 12; shift >= 0; shift -= 4) row[len++] = hex[(pixel >> shift) & 15];
        }
        row[len++] = '\n'; Serial.write((const uint8_t*)row, len);
        if (++screenRow == 135) screenRow = -1;
    }
    if (dumpRow >= 0 && Serial.availableForWrite() >= 256) {
        const Detection* d = engine.logAt(dumpRow);
        if (!d) { Serial.println("[log] end"); dumpRow = -1; }
        else {
            Serial.printf("[log] %d,%s,%02X:%02X:%02X:%02X:%02X:%02X,%d,%u,%s\n", dumpRow,
                detectionTypeName(d->type), d->mac[0], d->mac[1], d->mac[2], d->mac[3], d->mac[4], d->mac[5],
                d->rssi, d->hits, d->active ? "here" : "gone");
            ++dumpRow;
        }
    }
}

uint64_t readKeys() {
    uint64_t mask = 0;
    for (unsigned bank = 0; bank < 8; ++bank) {
        for (unsigned b = 0; b < 3; ++b) digitalWrite(selectors[b], (bank >> b) & 1);
        delayMicroseconds(5); // let the decoder and pullups settle
        for (unsigned input = 0; input < 7; ++input)
            if (!digitalRead(inputs[input])) mask |= CardputerKeys::matrixBit(bank, input);
    }
    return mask;
}

const char* confidence(Confidence c) {
    return c == Confidence::HIGH_CONF ? "HIGH" : c == Confidence::MED_CONF ? "MED" : "LOW";
}

void activateSetting(int direction) {
    switch (menuRow) {
    case 0: Settings::adjustBrightness(direction * 16); break;
    case 1: Settings::togglePowerSaver(); break;
    case 2: Settings::cycleScreenTimeout(); break;
    case 3: CardputerBoard::cycleScanMode(); break;
    case 4: Settings::cycleMinConfidence(); break;
    case 5: page = Page::FILTERS; selected = 0; break;
    case 6: page = Page::IGNORED; selected = 0; break;
    case 7: Settings::cycleTimeZone(); break;
    case 8: page = Page::HELP; break;
    }
}

void handleKey(CardputerKeys::Action a) {
    using A = CardputerKeys::Action;
    if (a == A::NONE) return;
    if (CardputerBoard::input(millis())) { dirty = true; return; }
    Serial.printf("[key] action=%u page=%u\n", (unsigned)a, (unsigned)page);
    dirty = true;
    if (haveAlert) {
        if (a == A::IGNORE) IgnoreList::add(alert.mac, alert.type);
        else if (a == A::SNOOZE) IgnoreList::snooze(alert.mac);
        else if (a == A::OPEN) { detail = alert; page = Page::DETAIL; }
        dismissAlert();
        return;
    }
    switch (a) {
    case A::HOME: page = Page::HOME; break;
    case A::SETTINGS: page = Page::SETTINGS; break;
    case A::DIARY: page = Page::DIARY; break;
    case A::HELP: page = Page::HELP; break;
    case A::IGNORE:
        if (page == Page::DETAIL) {
            if (IgnoreList::contains(detail.mac)) IgnoreList::remove(detail.mac);
            else IgnoreList::add(detail.mac, detail.type);
        }
        break;
    case A::SNOOZE:
        if (page == Page::DETAIL) IgnoreList::snooze(detail.mac);
        break;
    case A::LEFT: case A::RIGHT:
        if (page == Page::SETTINGS) activateSetting(a == A::LEFT ? -1 : 1);
        else if (page == Page::WARDROBE) {
            if (a == A::LEFT) Squachy::cyclePrevOutfit(); else Squachy::cycleOutfit();
        }
        break;
    case A::PET:
        page = Page::HOME;
        Squachy::stopShowOff();
        Squachy::trigger(Squachy::Event::PETTED);
        break;
    case A::OUTFIT: page = Page::WARDROBE; break;
    case A::BINGO: page = Page::BINGO; break;
    case A::SHADES: Squachy::cycleShadesColor(); page = Page::WARDROBE; break;
    case A::SHOW:
        page = Page::HOME;
        while (Squachy::onboardingActive()) Squachy::onboardingTapAdvance(0, 0);
        if (Squachy::showOffActive()) Squachy::stopShowOff();
        else Squachy::startShowOff();
        break;
    case A::LOG: page = Page::LOG; selected = 0; break;
    case A::DIAGNOSTICS: page = Page::DIAGNOSTICS; break;
    case A::BACK:
        page = page == Page::DETAIL ? Page::LOG :
            (page == Page::FILTERS || page == Page::IGNORED) ? Page::SETTINGS : Page::HOME;
        break;
    case A::UP:
        if (page == Page::WARDROBE) Squachy::cyclePrevOutfit();
        if (page == Page::LOG && selected) --selected;
        if ((page == Page::FILTERS || page == Page::IGNORED) && selected) --selected;
        if (page == Page::SETTINGS && menuRow) --menuRow;
        break;
    case A::DOWN:
        if (page == Page::WARDROBE) Squachy::cycleOutfit();
        if (page == Page::LOG && selected + 1 < engine.logCount()) ++selected;
        if (page == Page::FILTERS && selected + 2 < (uint8_t)DetectionType::COUNT) ++selected;
        if (page == Page::IGNORED && selected + 1 < IgnoreList::count()) ++selected;
        if (page == Page::SETTINGS && menuRow + 1 < MENU_COUNT) ++menuRow;
        break;
    case A::OPEN:
        if (page == Page::SETTINGS) activateSetting(1);
        else if (page == Page::FILTERS) Settings::toggleType((DetectionType)(selected + 1));
        else if (page == Page::IGNORED) {
            if (const uint8_t* mac = IgnoreList::macAt(selected)) IgnoreList::remove(mac);
            if (selected >= IgnoreList::count()) selected = IgnoreList::count() ? IgnoreList::count() - 1 : 0;
        }
        else if (page == Page::BINGO && Bingo::markedCount() == Bingo::CELLS) Bingo::newCard();
        else if (page == Page::WARDROBE) Squachy::cycleOutfit();
        else if (page == Page::HOME && Squachy::onboardingActive()) Squachy::onboardingTapAdvance(0, 0);
        else if (page == Page::HOME) { page = Page::LOG; selected = 0; }
        else if (page == Page::LOG) {
            const Detection* d = engine.logAt(selected);
            if (d) { detail = *d; page = Page::DETAIL; }
        }
        break;
    default: break;
    }
}

void render() {
    TFT_eSPI& t = buffered ? static_cast<TFT_eSPI&>(canvas) : display;
    clearFrame();
    t.setTextFont(1);
    t.setTextSize(1);
    t.setTextWrap(false);
    t.setTextColor(CYAN, BG);
    t.setCursor(4, 4);
    const uint16_t battery = CardputerBoard::batteryMillivolts();
    if (battery) t.printf("SW %u.%02uV", battery / 1000, (battery % 1000) / 10);
    else t.print("SW BAT:--");
    t.setCursor(94, 4);
    t.printf("W:%lu/s B:%lu/s", (unsigned long)wifiRate, (unsigned long)bleRate);
    t.drawFastHLine(0, 16, 240, PINK);
    t.setTextColor(TFT_WHITE, BG);
    t.setCursor(4, 23);
    if (haveAlert || page == Page::DETAIL) {
        const Detection& d = haveAlert ? alert : detail;
        t.setTextColor(haveAlert ? PINK : CYAN, BG);
        t.printf("%s: %s", haveAlert ? "MATCH" : "DETAIL", detectionTypeName(d.type));
        t.setTextColor(TFT_WHITE, BG);
        t.setCursor(4, 38); t.printf("%.37s", vendorText(d));
        t.setCursor(4, 51); t.printf("%.19s", d.name);
        t.setCursor(4, 64);
        t.printf("%02X:%02X:%02X:%02X:%02X:%02X", d.mac[0], d.mac[1], d.mac[2], d.mac[3], d.mac[4], d.mac[5]);
        t.setCursor(4, 77); t.printf("%d dBm  %s  CH:%u", d.rssi, d.channel ? "WiFi" : "BLE", d.channel);
        t.setCursor(4, 90); t.printf("Confidence: %s  Hits:%u", confidence(d.conf), d.hits);
        t.setCursor(4, 103);
        t.print(IgnoreList::contains(d.mac) ? "IGNORED: I restores alerts" :
                IgnoreList::snoozed(d.mac) ? "SNOOZED until restart" : "Signature match, not proof");
    } else if (page == Page::HOME) {
        const bool celebrating = reward[0] && millis() - rewardAt < 8000;
        Squachy::holdBubble(celebrating);
        if (celebrating && rewardOutfit >= 0) Squachy::setOutfitPreview(rewardOutfit);
        Squachy::tick(t, 120, 19, 84, millis(), true, 0.35f);
        Squachy::setOutfitPreview(-1);
        Squachy::holdBubble(false);
        t.setTextFont(1); t.setTextSize(1); t.setTextColor(CYAN, BG);
        if (celebrating) {
            t.fillRoundRect(2, 19, 236, 23, 3, 0x2104);
            t.setTextColor(PINK, 0x2104); t.setCursor(8, 27); t.printf("%.37s", reward);
            t.setTextColor(CYAN, BG);
        }
        t.fillRect(0, 107, 240, 11, BG);
        t.setCursor(4, 109);
        t.printf("Seen:%lu Pets:%lu Bingo:%u/16", (unsigned long)engine.lifetimeTotal(),
                      (unsigned long)Squachy::petCount(), Bingo::markedCount());
    } else if (page == Page::WARDROBE) {
        Squachy::drawWaving(t, 48, 103, millis(), 0.8f);
        t.setTextFont(1); t.setTextSize(1); t.setTextColor(CYAN, BG);
        t.setCursor(96, 23); t.print("YOUR WARDROBE");
        t.setCursor(96, 39); t.printf("%.23s", Squachy::outfitName());
        t.setTextColor(TFT_WHITE, BG);
        t.setCursor(96, 55); t.printf("Unlocked: %u/%u", Squachy::unlockedOutfitCount(), Squachy::outfitCount());
        t.setCursor(96, 69); t.printf("Shades: %.15s", Squachy::shadesColorName());
        t.setCursor(96, 83); t.printf("Pets: %lu", (unsigned long)Squachy::petCount());
        uint8_t next; uint32_t target;
        t.setCursor(96, 96);
        if (Squachy::nextOutfit(next, target)) t.printf("Next: %.16s", Squachy::outfitNameAt(next));
        else t.print("Count rewards done!");
        t.setCursor(4, 109);
        if (Squachy::nextOutfit(next, target)) t.printf("Next outfit: %lu/%lu detections", (unsigned long)engine.lifetimeTotal(), (unsigned long)target);
        else t.print("Special outfits need background games");
    } else if (page == Page::BINGO) {
        t.printf("BINGO %u/16  Lines:%u  All:%u", Bingo::markedCount(), Bingo::linesCalled(), Bingo::linesEver());
        for (uint8_t i = 0; i < Bingo::CELLS; ++i) {
            const int x = (i % 4) * 60, y = 37 + (i / 4) * 17;
            const uint16_t color = Bingo::marked(i) ? CYAN : 0x528A;
            t.drawRect(x + 1, y, 58, 16, color);
            t.setTextColor(Bingo::marked(i) ? BG : TFT_WHITE, Bingo::marked(i) ? CYAN : BG);
            if (Bingo::marked(i)) t.fillRect(x + 2, y + 1, 56, 14, CYAN);
            t.setCursor(x + 4, y + 4); t.printf("%.8s", detectionTypeName(Bingo::typeAt(i)));
        }
        t.setTextColor(CYAN, BG); t.setCursor(4, 109);
        t.print(Bingo::markedCount() == Bingo::CELLS ? "Full card! Enter deals your next card" : "Real matches fill squares. Get a line!");
    } else if (page == Page::DIARY) {
        t.setTextColor(CYAN, BG); t.printf("DIARY / %s", Squachy::growthStageName());
        t.setTextColor(TFT_WHITE, BG);
        t.setCursor(4, 39); t.printf("Lifetime sightings: %lu", (unsigned long)engine.lifetimeTotal());
        const uint32_t growth = Squachy::nextGrowthTotal();
        t.setCursor(4, 53);
        if (growth) t.printf("Next growth: %lu / %lu", (unsigned long)engine.lifetimeTotal(), (unsigned long)growth);
        else t.print("Legend reached!");
        const uint32_t shades = Squachy::nextShadesPetCount();
        t.setCursor(4, 67);
        if (shades) t.printf("Next shades: %lu / %lu pets", (unsigned long)Squachy::petCount(), (unsigned long)shades);
        else t.printf("All shades! Pets: %lu", (unsigned long)Squachy::petCount());
        t.setCursor(4, 81); t.printf("Bingo: %u lines / %u full cards", Bingo::linesEver(), Bingo::cardsFilled());
        t.setCursor(4, 95); t.printf("Boots together: %lu", (unsigned long)Squachy::bootCount());
        t.setCursor(4, 109); t.print("O wardrobe  P pet  B bingo");
    } else if (page == Page::HELP) {
        const char* lines[] = {"S home L log D stats J diary", "P pet O outfits C shades B bingo",
            "Space show-off  K settings  H help", ";/. move  ,/ adjust  Enter choose",
            "Detail: I ignore/unignore X snooze", "W/B = WiFi/BLE packets per second",
            "Battery volts; USB affects reading"};
        for (unsigned i = 0; i < 7; ++i) { t.setCursor(4, 23 + i * 13); t.print(lines[i]); }
    } else if (page == Page::SETTINGS || page == Page::FILTERS || page == Page::IGNORED) {
        const uint8_t count = page == Page::SETTINGS ? MENU_COUNT : page == Page::FILTERS ? (uint8_t)DetectionType::COUNT - 1 : IgnoreList::count();
        const uint8_t cursor = page == Page::SETTINGS ? menuRow : selected;
        const unsigned first = (cursor / 5) * 5;
        if (!count) t.print("No ignored devices.");
        for (unsigned i = first; i < count && i < first + 5; ++i) {
            const int y = 23 + (i - first) * 17;
            const uint16_t bg = i == cursor ? 0x2104 : BG;
            t.fillRect(0, y - 2, 240, 15, bg);
            t.setTextColor(i == cursor ? CYAN : TFT_WHITE, bg); t.setCursor(4, y);
            if (page == Page::SETTINGS) {
                switch (i) {
                case 0: t.printf("Brightness       %u/255", Settings::brightness()); break;
                case 1: t.printf("Power saving     %s", Settings::powerSaver() ? "ON" : "OFF"); break;
                case 2: t.printf("Dim after        %us (0=never)", Settings::screenTimeoutSecRaw()); break;
                case 3: t.printf("BLE scan         %s", CardputerBoard::scanModeName()); break;
                case 4: t.printf("Alert confidence %s", Settings::minConfidenceLabel()); break;
                case 5: t.printf("Detection types  %u enabled >", Settings::enabledTypeCount()); break;
                case 6: t.printf("Ignored devices  %u >", IgnoreList::count()); break;
                case 7: t.printf("Zone: %.30s", Settings::timeZoneName()); break;
                case 8: t.print("Keyboard help >"); break;
                }
            } else if (page == Page::FILTERS) {
                const auto type = (DetectionType)(i + 1);
                t.printf("[%c] %.32s", Settings::typeEnabled(type) ? 'x' : ' ', detectionTypeName(type));
            } else {
                const uint8_t* mac = IgnoreList::macAt(i);
                if (mac) t.printf("%-11.11s %02X%02X%02X%02X%02X%02X", detectionTypeName(IgnoreList::typeAt(i)),
                    mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
            }
        }
        t.setTextColor(CYAN, BG); t.setCursor(4, 109);
        t.print(page == Page::IGNORED ? "Enter restores alerts for this device" :
                page == Page::FILTERS ? "Disabled types are not logged/counted" : "Saved immediately; scanning continues");
    } else if (page == Page::LOG) {
        const uint8_t count = engine.logCount();
        if (selected >= count) selected = count ? count - 1 : 0;
        if (!count) t.print("No signature matches yet.");
        const unsigned first = (selected / 5) * 5;
        for (unsigned i = first; i < count && i < first + 5; ++i) {
            const Detection* d = engine.logAt(i);
            if (!d) continue;
            const int y = 23 + (i - first) * 17;
            if (i == selected) t.fillRect(0, y - 2, 240, 15, 0x2104);
            t.setTextColor(i == selected ? CYAN : TFT_WHITE, i == selected ? 0x2104 : BG);
            t.setCursor(4, y);
            t.printf("%c %-11.11s %4d %s", i == selected ? '>' : ' ', detectionTypeName(d->type), d->rssi, confidence(d->conf));
        }
    } else {
        t.printf("Heap %lu  Largest %lu", (unsigned long)ESP.getFreeHeap(),
                 (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
        t.setCursor(4, 38); t.printf("Min heap %lu", (unsigned long)ESP.getMinFreeHeap());
        t.setCursor(4, 53); t.printf("WiFi frames %lu (%lu/s)", (unsigned long)wifiFramesSeen(), (unsigned long)wifiRate);
        t.setCursor(4, 68); t.printf("BLE adverts %lu (%lu/s)", (unsigned long)advertsSeen(), (unsigned long)bleRate);
        t.setCursor(4, 83); t.printf("BLE dropped %lu", (unsigned long)advertsDropped());
        t.setCursor(4, 98); t.printf("Up %lus SD:%s BLE:%s", (unsigned long)(millis() / 1000), engine.sd().ready() ? "OK" : "OFF", CardputerBoard::scanModeName());
        char clock[24]; Clock::formatClock(clock, sizeof clock);
        t.setCursor(4, 110); t.printf("Clock: %s%s", clock, Clock::guessed() ? " ~" : "");
    }
    t.setTextColor(CYAN, BG);
    t.drawFastHLine(0, 119, 240, PINK);
    t.setCursor(4, 124);
    t.print(haveAlert ? "I ignore X snooze Enter view Esc close" : page == Page::DETAIL ? "I ignore X snooze Esc back" :
        page == Page::LOG ? ";/. move  Enter view  Esc back" :
        page == Page::HOME ? "P pet O outfit B bingo K menu H help" :
        page == Page::WARDROBE ? ";/. outfit  C shades  S home" :
        page == Page::SETTINGS || page == Page::FILTERS || page == Page::IGNORED ? ";/. move ,/ adjust Enter choose Esc" :
        "S home  L log  D stats  Esc back");
    if (buffered) canvas.pushSprite(0, 0);
}
}

void setup() {
    Serial.setTxBufferSize(2048);
    Serial.begin(115200); // native USB CDC; never wait for a host
    Serial.setTxTimeoutMs(0); // diagnostics must not stall without a reader
    for (uint8_t pin : selectors) { pinMode(pin, OUTPUT); digitalWrite(pin, LOW); }
    for (uint8_t pin : inputs) pinMode(pin, INPUT_PULLUP);
    display.init();
    display.setRotation(1);
    display.fillScreen(BG);
    display.setTextColor(CYAN, BG);
    display.setCursor(4, 20);
    display.print("SquachWatch Cardputer: starting");
    // Allocate before the radio stacks fragment the heap.
    canvas.setColorDepth(8);
    buffered = canvas.createSprite(240, 135) != nullptr;
    Settings::load();
    Clock::begin();
    IgnoreList::begin();
    // Passive is the initial default; the keyboard menu can opt into active
    // scan responses or the adaptive policy. No network joins, mesh or OTA.
    CardputerBoard::begin();
    engine.init();
    Bingo::begin(engine);
    Squachy::trigger(Squachy::Event::BOOTED, DetectionType::UNKNOWN, engine.lifetimeTotal());
    bootReadyMs = millis();
    Serial.printf("[cardputer] %s; framebuffer=%s\n", FIRMWARE_VERSION, buffered ? "OK" : "direct fallback");
}

void loop() {
    const uint32_t now = millis();
    // Preserve the selected device as new detections enter the front of log.
    uint8_t selectedMac[6];
    DetectionType selectedType = DetectionType::UNKNOWN;
    bool preserve = false;
    if (page == Page::LOG) {
        if (const Detection* d = engine.logAt(selected)) {
            memcpy(selectedMac, d->mac, 6); selectedType = d->type; preserve = true;
        }
    }
    engine.loop();
    Clock::tick(now);
    CardputerBoard::tick(now);
    Bingo::tick(now);
    DetectionType bingoType;
    const Bingo::Event bingoEvent = Bingo::takeEvent(bingoType);
    if (bingoEvent == Bingo::Event::MARKED || bingoEvent == Bingo::Event::LINE || bingoEvent == Bingo::Event::FULL) {
        snprintf(reward, sizeof reward, "%s", bingoEvent == Bingo::Event::FULL ? "BINGO! Full card!" :
            bingoEvent == Bingo::Event::LINE ? "BINGO! Four in a row!" : "New bingo square!");
        rewardOutfit = -1;
        rewardAt = now; dirty = true;
    }
    uint8_t unlocked;
    if (page == Page::HOME && !haveAlert && (!reward[0] || now - rewardAt >= 8000) && Squachy::consumeOutfitUnlock(unlocked)) {
        snprintf(reward, sizeof reward, "Unlocked: %.28s", Squachy::outfitNameAt(unlocked));
        rewardOutfit = unlocked;
        rewardAt = now; dirty = true;
    }
    pollConsole();
    if (preserve) {
        for (unsigned i = 0; i < engine.logCount(); ++i) {
            const Detection* d = engine.logAt(i);
            if (d && d->type == selectedType && memcmp(d->mac, selectedMac, 6) == 0) { selected = i; break; }
        }
    }
    handleKey(keys.update(readKeys(), now));
    const Detection* latest = engine.latest();
    if (sightings.take(latest)) {
        // Browsing and diagnostics remain usable in busy environments.
        if (page == Page::HOME && !haveAlert && Cardputer::mayAlert(*latest, Settings::minConfidence(), IgnoreList::silenced(latest->mac)) &&
            engine.alertGate(latest->mac, Settings::autoQuietAfter(), false) != DetectionEngine::AlertGate::HOLD) {
            alert = *latest; alertAt = now; haveAlert = true; dirty = true;
            CardputerBoard::alert(now);
        } else reactTo(*latest);
    }
    if (haveAlert && now - alertAt >= 8000) dismissAlert();
    if (now - lastSample >= 1000) {
        const uint32_t elapsed = now - lastSample;
        const uint32_t wifi = wifiFramesSeen(), ble = advertsSeen();
        wifiRate = Cardputer::rate(wifi, previousWifi, elapsed);
        bleRate = Cardputer::rate(ble, previousBle, elapsed);
        previousWifi = wifi; previousBle = ble; lastSample = now;
        dirty = true;
    }
    if (now - lastSerial >= 10000) {
        lastSerial = now;
        bool promiscuous = false;
        uint8_t channel = 0;
        wifi_second_chan_t secondary;
        esp_wifi_get_promiscuous(&promiscuous);
        esp_wifi_get_channel(&channel, &secondary);
        Serial.printf("[stats] uptime=%lu ready_ms=%lu wifi_s=%lu ble_s=%lu heap=%lu largest=%lu dropped=%lu sd=%u wifi_total=%lu ble_total=%lu promisc=%u channel=%u scanning=%u records=%u\n",
            (unsigned long)(now / 1000), (unsigned long)bootReadyMs, (unsigned long)wifiRate, (unsigned long)bleRate,
            (unsigned long)ESP.getFreeHeap(), (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
            (unsigned long)advertsDropped(), engine.sd().ready(),
            (unsigned long)wifiFramesSeen(), (unsigned long)advertsSeen(), promiscuous, channel,
            NimBLEDevice::getScan()->isScanning(), engine.logCount());
    }
    const uint32_t frameMs = CardputerBoard::dimmed() ? 1000 : 100;
    if (screenRow < 0 && (dirty || page == Page::HOME || page == Page::WARDROBE) && now - lastPaint >= frameMs) { render(); lastPaint = now; dirty = false; }
    delay(2);
}
#endif
