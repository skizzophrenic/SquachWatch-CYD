// SquachWatch-CYD -- Tab5 bench build (TAB5_RADIOTEST): the C6 link alone.
//
// Proves the radio before the app leans on it: the panel comes up (its
// expander bus is where the C6's power switch lives), the C6 is powered and
// the SDIO link brought up, a hello goes over and the answer is printed, then
// the C6 hops channels 1, 6 and 11 at 250 ms with sniffing on and every frame
// it forwards is one line on the console (type, subtype, RSSI, channel,
// length, source MAC -- never a network name) with the C6's own counters once
// a second, for 30 seconds, then sniffing goes off and the totals per channel
// are printed. Not part of the product build ([env:tab5] filters it out);
// [env:tab5-radiotest] builds this file with tab5_panel.cpp and tab5_radio.cpp.
#if defined(TAB5_RADIOTEST)
#include <Arduino.h>
#include <M5GFX.h>
#include <esp_heap_caps.h>
#include "tab5_panel.h"
#include "tab5_radio.h"

static const uint32_t SNIFF_MS = 30000;
static bool     s_linkUp = false;
static bool     s_done   = false;
static uint32_t s_startAt = 0, s_lastStatsAt = 0, s_lastLineAt = 0;
static uint32_t s_perChan[15], s_perType[4], s_total;
static uint32_t s_lastReceived = 0;

static const char* kindName(uint8_t type, uint8_t sub) {
    if (type == 2) return "data";
    if (type != 0) return "?";
    switch (sub) {
        case 8:  return "beacon";
        case 4:  return "probe-req";
        case 12: return "deauth";
        case 10: return "disassoc";
        default: return "mgmt";
    }
}

static void say(const char* line) {
    Serial.println(line);
    auto& g = Tab5Panel::gfx();
    g.println(line);
}

void setup() {
    Serial.begin(115200);
    Serial.setTxTimeoutMs(0);   // native USB: never wait for a host
    delay(300);
    Serial.println("[radiotest] Tab5 C6 link test");
    Serial.printf("[radiotest] heap %lu free, %lu largest\n", (unsigned long)ESP.getFreeHeap(),
                  (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    if (!Tab5Panel::begin()) {
        Serial.println("[radiotest] no panel: stopping (the C6's power switch is on the panel's expander bus)");
        return;
    }
    Tab5Panel::backlight(80);
    auto& g = Tab5Panel::gfx();
    g.fillScreen(TFT_BLACK);
    g.setTextColor(TFT_GREEN, TFT_BLACK);
    g.setTextSize(3);
    g.setCursor(20, 20);
    say("TAB5 RADIO TEST: C6 over SDIO");

    s_linkUp = Tab5Radio::begin();
    if (!s_linkUp) { say("link: DOWN (see console)"); return; }
    char line[96];
    snprintf(line, sizeof line, "link: UP, hello \"%s\"", Tab5Radio::tag());
    say(line);

    static const uint8_t chans[3] = { 1, 6, 11 };
    Tab5Radio::hop(chans, 3, 250);
    delay(50);
    Tab5Radio::sniff(true);
    s_startAt = millis();
    say("sniffing 1/6/11 at 250 ms for 30 s");
}

void loop() {
    if (!s_linkUp || s_done) { delay(100); return; }
    const uint32_t now = millis();
    Tab5Radio::Frame f;
    uint16_t burst = 0;
    while (Tab5Radio::next(f)) {
        burst++;
        s_total++;
        if (f.channel < 15) s_perChan[f.channel]++;
        if (f.type < 4) s_perType[f.type]++;
        const uint8_t* m = f.data + 10;   // addr2: the transmitter
        if (f.dataLen >= 16) {
            Serial.printf("[frame] %-9s sub %2u  %4d dBm  ch %2u  len %3u/%3u  src %02X:%02X:%02X:%02X:%02X:%02X\n",
                          kindName(f.type, f.subtype), (unsigned)f.subtype, (int)f.rssi, (unsigned)f.channel,
                          (unsigned)f.dataLen, (unsigned)f.sigLen, m[0], m[1], m[2], m[3], m[4], m[5]);
        } else {
            Serial.printf("[frame] %-9s sub %2u  %4d dBm  ch %2u  len %3u/%3u  (short)\n",
                          kindName(f.type, f.subtype), (unsigned)f.subtype, (int)f.rssi, (unsigned)f.channel,
                          (unsigned)f.dataLen, (unsigned)f.sigLen);
        }
        if (burst >= 64) break;   // let the stats line through on a busy channel
    }
    if (now - s_lastStatsAt >= 1000) {
        s_lastStatsAt = now;
        const Tab5Radio::Stats st = Tab5Radio::stats();
        Serial.printf("[stats] t=%2lus c6: seen %lu fwd %lu drop %lu ch %u (%s)  host: rx %lu (+%lu/s) qfull %lu pend %u acks %lu last %04x:%ld  heap %lu\n",
                      (unsigned long)((now - s_startAt) / 1000), (unsigned long)st.seen, (unsigned long)st.forwarded,
                      (unsigned long)st.dropped, (unsigned)st.channel, st.statsAt ? "live" : "none yet",
                      (unsigned long)st.received, (unsigned long)(st.received - s_lastReceived), (unsigned long)st.queueFull,
                      (unsigned)Tab5Radio::pending(), (unsigned long)st.acks, (unsigned)st.lastAckCmd, (long)st.lastAckStatus,
                      (unsigned long)ESP.getFreeHeap());
        s_lastReceived = st.received;
        if (now - s_lastLineAt >= 5000) {
            s_lastLineAt = now;
            char line[96];
            snprintf(line, sizeof line, "t=%lus frames %lu  ch1 %lu ch6 %lu ch11 %lu", (unsigned long)((now - s_startAt) / 1000),
                     (unsigned long)s_total, (unsigned long)s_perChan[1], (unsigned long)s_perChan[6], (unsigned long)s_perChan[11]);
            say(line);
        }
    }
    if (now - s_startAt >= SNIFF_MS) {
        Tab5Radio::sniff(false);
        delay(200);
        const Tab5Radio::Stats st = Tab5Radio::stats();
        Serial.printf("[done] %lu frames in %lu s: ch1 %lu, ch6 %lu, ch11 %lu, other %lu; mgmt %lu, data %lu; c6 seen %lu fwd %lu drop %lu; host qfull %lu; last ack %04x:%ld\n",
                      (unsigned long)s_total, (unsigned long)(SNIFF_MS / 1000), (unsigned long)s_perChan[1], (unsigned long)s_perChan[6],
                      (unsigned long)s_perChan[11], (unsigned long)(s_total - s_perChan[1] - s_perChan[6] - s_perChan[11]),
                      (unsigned long)s_perType[0], (unsigned long)s_perType[2], (unsigned long)st.seen, (unsigned long)st.forwarded,
                      (unsigned long)st.dropped, (unsigned long)st.queueFull, (unsigned)st.lastAckCmd, (long)st.lastAckStatus);
        say("done: sniff off");
        s_done = true;
    }
    delay(5);
}
#endif  // TAB5_RADIOTEST
