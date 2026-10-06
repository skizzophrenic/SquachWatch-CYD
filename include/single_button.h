// Debounced short-release / long-hold gestures, independent of GPIO and time source.
#pragma once
#include <stdint.h>
class SingleButton {
public:
    enum class Event : uint8_t { NONE, TAP, HOLD };
    Event tick(bool down, uint32_t now) {
        if (down != _raw) { _raw = down; _edgeAt = now; }
        if (now - _edgeAt >= 25 && _down != _raw) {
            _down = _raw;
            if (_down) { _pressedAt = now; _held = false; }
            else if (!_held) return Event::TAP;
        }
        if (_down && !_held && now - _pressedAt >= 700) {
            _held = true;
            return Event::HOLD;
        }
        return Event::NONE;
    }
private:
    bool _raw = false, _down = false, _held = false;
    uint32_t _edgeAt = 0, _pressedAt = 0;
};
