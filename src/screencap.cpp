// SquachWatch-CYD — serial screen capture implementation
#include "screencap.h"
#include <Arduino.h>
#include <esp_log.h>

namespace ScreenCap {

// Payload formats, reported in the header's format byte so the PC side
// never has to guess what the pixels are.
static const uint8_t FMT_RGB565 = 0;   // 2 bytes per pixel
static const uint8_t FMT_RGB332 = 1;   // 1 byte per pixel, what an 8bpp sprite holds

static const char    REQUEST[]   = "SQCAP\n";
static const uint8_t REQUEST_LEN = sizeof(REQUEST) - 1;   // no terminator
static uint8_t       s_matched   = 0;

// A capture is streamed across many loop() iterations rather than
// written in one go: a single blocking Serial.write() of a whole frame
// stalls loop() for the better part of a second, and this is the shape
// the live PC mirror needs anyway.
static bool     s_sending = false;
static uint32_t s_offset  = 0;      // bytes of payload already written
static uint32_t s_len     = 0;      // total payload bytes
static uint32_t s_sum     = 0;      // running checksum over what's been sent
static const uint8_t* s_px = nullptr;

// Upper bound per loop() iteration even when the TX buffer claims more
// room, plus a minimum gap between chunks, so a capture can never
// dominate the frame budget. The rate this works out to is well clear of
// what the link handles comfortably -- pacing was never what fixed the
// original truncation (that was a buffer overrun, see beginFrame()), it
// just keeps loop() responsive.
static const size_t   MAX_PER_TICK = 1024;
static const uint32_t TICK_MIN_MS  = 1;
static uint32_t       s_lastTickAt = 0;

static void beginFrame(TFT_eSprite* fb, int w, int h) {
    const uint8_t* px = (const uint8_t*)fb->getPointer();
    if (!px) {
        // Sprite object exists but was never successfully allocated
        // (see main.cpp's frameBufferOk).
        Serial.println("[screencap] framebuffer not allocated");
        return;
    }

    // Ask the sprite what it actually is rather than assuming. The first
    // version of this hardcoded 16bpp on the reasoning that nothing here
    // calls setColorDepth() -- main.cpp does, twice, right where `frame`
    // is created (it runs 8bpp to halve the buffer's heap cost). Reading
    // w*h*2 from a w*h*1 allocation walked ~38KB past the end and
    // faulted the device mid-transfer, which looked convincingly like a
    // flaky USB-serial link for a while.
    uint8_t depth = (uint8_t)fb->getColorDepth();
    uint8_t bytesPerPx;
    uint8_t format;
    switch (depth) {
        case 16: bytesPerPx = 2; format = FMT_RGB565; break;
        case 8:  bytesPerPx = 1; format = FMT_RGB332; break;
        default:
            Serial.printf("[screencap] unsupported sprite depth %u bpp\n", (unsigned)depth);
            return;
    }
    uint32_t len = (uint32_t)w * (uint32_t)h * (uint32_t)bytesPerPx;

    uint8_t hdr[14];
    hdr[0]  = 'S'; hdr[1] = 'Q'; hdr[2] = 'F'; hdr[3] = 'B';
    hdr[4]  = 1;                                  // version
    hdr[5]  = format;
    hdr[6]  = (uint8_t)(w & 0xFF);
    hdr[7]  = (uint8_t)((w >> 8) & 0xFF);
    hdr[8]  = (uint8_t)(h & 0xFF);
    hdr[9]  = (uint8_t)((h >> 8) & 0xFF);
    hdr[10] = (uint8_t)(len & 0xFF);
    hdr[11] = (uint8_t)((len >> 8) & 0xFF);
    hdr[12] = (uint8_t)((len >> 16) & 0xFF);
    hdr[13] = (uint8_t)((len >> 24) & 0xFF);
    Serial.write(hdr, sizeof(hdr));   // 14 bytes always fits, no need to meter

    // Nothing else may write to this port until the frame is out. The
    // SD driver logs "Card Failed!" every few hundred ms whenever no
    // card is inserted, from its own task -- those lines were landing
    // in the middle of the pixel payload, shifting everything after them
    // and putting the checksum bytes somewhere the reader wasn't
    // looking. The payload is a length-delimited binary blob; anything
    // else on the wire during it is corruption by definition.
    esp_log_level_set("*", ESP_LOG_NONE);

    s_px      = px;
    s_len     = len;
    s_offset  = 0;
    s_sum     = 0;
    s_sending = true;
}

// Pushes whatever the TX buffer will take right now, up to MAX_PER_TICK.
// Never blocks: availableForWrite() is the amount that can be handed over
// without waiting for the UART to drain.
static void pumpFrame() {
    uint32_t nowMs = millis();
    if (nowMs - s_lastTickAt < TICK_MIN_MS) return;
    s_lastTickAt = nowMs;

    int space = Serial.availableForWrite();
    if (space <= 0) return;

    size_t budget = (size_t)space;
    if (budget > MAX_PER_TICK) budget = MAX_PER_TICK;

    uint32_t remaining = s_len - s_offset;
    size_t n = (remaining < budget) ? (size_t)remaining : budget;

    Serial.write(s_px + s_offset, n);
    for (size_t i = 0; i < n; i++) s_sum += s_px[s_offset + i];
    s_offset += n;

    if (s_offset >= s_len) {
        uint16_t ck = (uint16_t)(s_sum & 0xFFFF);
        uint8_t tail[2] = { (uint8_t)(ck & 0xFF), (uint8_t)((ck >> 8) & 0xFF) };
        Serial.write(tail, sizeof(tail));
        // Plain-text trailer after the binary payload: it makes "did the
        // device finish sending?" answerable from the PC side without
        // guessing, which is the difference between chasing a device-side
        // hang and chasing lost bytes in the USB-serial transport.
        s_sending = false;
        s_px      = nullptr;
        // Safe to let the rest of the system talk again now that the
        // binary payload is fully on the wire.
        esp_log_level_set("*", ESP_LOG_ERROR);
        Serial.printf("\n[screencap] sent %u bytes sum=%u\n", (unsigned)s_len, (unsigned)ck);
    }
}

bool busy() { return s_sending; }

void poll(TFT_eSprite* fb, int w, int h) {
    // Finish an in-flight capture before looking at new input -- the
    // frame being streamed is the one that was on screen when the
    // request arrived, and reading the live buffer as it keeps changing
    // underneath would tear the image.
    if (s_sending) {
        pumpFrame();
        return;
    }

    while (Serial.available()) {
        char c = (char)Serial.read();
        if (c == REQUEST[s_matched]) {
            if (++s_matched == REQUEST_LEN) {
                s_matched = 0;
                if (fb) beginFrame(fb, w, h);
                else    Serial.println("[screencap] no framebuffer on this board");
                return;
            }
        } else {
            // Restart the match -- but a mismatched byte can still be
            // the start of the next attempt, so "SSQCAP" has to match
            // rather than being swallowed by a naive reset to 0.
            s_matched = (c == REQUEST[0]) ? 1 : 0;
        }
    }
}

}  // namespace ScreenCap
