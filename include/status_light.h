// SquachWatch-CYD — the RGB LED on the back of the board, put to work.
//
// One light, several things that want it. The rules, highest first:
//
//   1. Off: LIGHT is OFF in settings, or a screen where the light has to
//      stay honest (PIN entry, the lock screen, the wipe). A duress restart
//      looks like any other restart, from the back too.
//   2. A firmware update: cyan blink while bytes arrive, solid green for the
//      seconds before a successful restart, solid red for a failure.
//   3. A detection alert: three fast flashes in the detection's own colour,
//      then steady for as long as the alert card is up, then a one-second
//      fade down. Same colour the card uses, from the theme.
//   4. An unread message: a soft double-blink in the mesh pink every three
//      seconds until the inbox is opened.
//   5. A squad visit: one short blip, then back to idle. Never repeats.
//   6. Idle: off, a slow breathe, or solid, in the idle colour.
//
// The LED sits on GPIO 4 / 16 / 17 on the 2.8" CYD, both the ST7789 and the
// ILI9341 kinds, and on the RL Phantom, where it is on the front. Common
// anode, so the pin goes LOW to light. The Freenove S3 2.8" has a WS2812 on
// GPIO 42 instead, driven with the same rules. The AWOK and the 3.5" are unverified
// and the driver compiles to nothing on them: driving PWM onto a pin that
// turns out to be something else is exactly how the Phantom's touch fault
// happened.
#pragma once
#include <stdint.h>

namespace StatusLight {

// What the main loop knows that the light needs. Filled in fresh every tick
// from the state machine's own variables, so the light never has to be told
// about a transition it might otherwise miss.
struct Context {
    bool     alert;         // the ALERT card is on screen
    uint16_t alertColor;    // Theme::colorFor(type), RGB565
    bool     unread;        // MeshTalk::inbox().unread
    bool     visiting;      // somebody's Squachy is on our screen
    uint8_t  update;        // 0 none, 1 receiving, 2 done, 3 failed
    bool     quiet;         // the lock screen, PIN entry: show nothing at all
    bool     screenDimmed;  // the power saver has dimmed the backlight
    bool     screenDark;    // ...all the way to off
};

// True on a board whose LED pins are known. Everything below is a no-op
// elsewhere, so callers need not check.
bool available();

// Once in setup(), after Settings. Claims LEDC channels 3, 4 and 5; the
// backlight owns 0 to 2.
void begin();

// The red, green, blue sweep during the splash. Half a second.
void boot(uint32_t now);

// The settings TEST row: alert, message, visit and idle back to back, about
// six seconds, so the settings can be seen without waiting for a camera.
void test(uint32_t now);

// Dark, now, and stays dark until the next tick() decides otherwise. For the
// moment before a wipe restarts the board.
void off();

// Every loop. Cheap: does its sums every 20 ms and nothing in between.
void tick(uint32_t now, const Context& c);

} // namespace StatusLight
