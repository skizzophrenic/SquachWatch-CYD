#pragma once
#include <stdint.h>

// Matrix wiring/coordinates verified against M5Stack's MIT-licensed
// M5Cardputer IOMatrix reader (f139285). This decoder only exposes the keys
// used by this port; it is not a text-entry or USB keyboard implementation.
namespace CardputerKeys {
constexpr uint64_t keyMask(unsigned row, unsigned col) {
    return uint64_t(1) << (row * 14 + col);
}
constexpr uint64_t matrixBit(unsigned bank, unsigned input) {
    return keyMask(3 - (bank % 4), input * 2 + (bank < 4 ? 1 : 0));
}
enum class Action { NONE, HOME, LOG, DIAGNOSTICS, UP, DOWN, OPEN, BACK,
    PET, OUTFIT, BINGO, SHOW, SHADES, SETTINGS, DIARY, HELP, LEFT, RIGHT, IGNORE, SNOOZE };
inline Action decode(uint64_t pressed) {
    if (pressed & keyMask(0, 0)) return Action::BACK;       // grave / Fn-Esc
    if (pressed & keyMask(0, 13)) return Action::BACK;      // backspace
    if (pressed & keyMask(2, 13)) return Action::OPEN;      // enter
    if (pressed & keyMask(2, 11)) return Action::UP;        // ; / Fn-Up
    if (pressed & keyMask(3, 11)) return Action::DOWN;      // . / Fn-Down
    if (pressed & keyMask(2, 3)) return Action::HOME;       // s
    if (pressed & keyMask(2, 10)) return Action::LOG;       // l
    if (pressed & keyMask(2, 4)) return Action::DIAGNOSTICS; // d
    if (pressed & keyMask(1, 10)) return Action::PET;       // p
    if (pressed & keyMask(1, 9)) return Action::OUTFIT;     // o
    if (pressed & keyMask(3, 7)) return Action::BINGO;      // b
    if (pressed & keyMask(3, 13)) return Action::SHOW;      // space
    if (pressed & keyMask(3, 5)) return Action::SHADES;     // c
    if (pressed & keyMask(2, 9)) return Action::SETTINGS;   // k
    if (pressed & keyMask(2, 8)) return Action::DIARY;      // j
    if (pressed & keyMask(2, 7)) return Action::HELP;       // h
    if (pressed & keyMask(3, 10)) return Action::LEFT;     // , / Fn-Left
    if (pressed & keyMask(3, 12)) return Action::RIGHT;    // / / Fn-Right
    if (pressed & keyMask(1, 8)) return Action::IGNORE;    // i
    if (pressed & keyMask(3, 4)) return Action::SNOOZE;    // x
    return Action::NONE;
}

// Debounce the whole matrix for 20 ms, then emit only newly pressed keys.
// Holding a key must not repeatedly dismiss alerts or activate a menu.
class Debouncer {
public:
    Action update(uint64_t raw, uint32_t now) {
        if (raw != candidate) { candidate = raw; changed = now; }
        if (raw == stable || uint32_t(now - changed) < 20) return Action::NONE;
        const uint64_t pressed = raw & ~stable;
        stable = raw;
        return decode(pressed);
    }
private:
    uint64_t candidate = 0, stable = 0;
    uint32_t changed = 0;
};
}
