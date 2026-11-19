#pragma once
#include <stdint.h>

namespace StopWatchInput {
constexpr uint32_t HOLD_MS = 500;
inline uint8_t nextFocus(uint8_t index,uint8_t count) { return count ? (index+1)%count : 0; }
enum Action : uint8_t { NONE = 0, BACK = 1, HOME = 2, NEXT = 4, SELECT = 8, WAKE = 16, MENU = 32, POWER_OFF = 64 };
struct Button {
    bool down = false, fired = false, consumed = false, armed = false;
    uint32_t since = 0;
    uint8_t tick(bool pressed, uint32_t now, bool suppress, uint8_t click, uint8_t hold) {
        if (!pressed) armed = true;
        if (pressed && !down) { since = now; fired = false; consumed = suppress || !armed; }
        if (suppress && (pressed || down)) consumed = true;
        uint8_t event = NONE;
        if (!consumed && !fired && (pressed || down) && now - since >= HOLD_MS) {
            fired = true; event = hold;
        }
        if (!pressed && down && !consumed && !fired && now - since >= 25) event = click;
        down = pressed;
        return event;
    }
    uint16_t progress(uint32_t now) const {
        if (!down || consumed) return 0;
        const uint32_t elapsed = now - since;
        return elapsed >= HOLD_MS ? 1000 : (uint16_t)(elapsed * 1000 / HOLD_MS);
    }
};
struct Controller {
    Button left, right;
    bool chord = false, ended = false, fired = false, consumed = false, shortChord = false;
    uint32_t chordSince = 0;
    void consume() {
        if(left.down) left.consumed=true;
        if(right.down) right.consumed=true;
        if(chord) consumed=true;
    }
    uint16_t progress(bool rightSide, uint32_t now) const {
        if(!chord) return rightSide ? right.progress(now) : left.progress(now);
        if(consumed || ended) return 0;
        const uint32_t elapsed=now-chordSince;
        return elapsed>=HOLD_MS ? 1000 : (uint16_t)(elapsed*1000/HOLD_MS);
    }
    uint8_t tick(bool l, bool r, uint32_t now, bool dimmed, bool blocked) {
        const bool wake = dimmed && ((l && !left.down) || (r && !right.down));
        const bool suppress = dimmed || blocked;
        if(!chord && l && r) {
            chord=true; ended=false; fired=false; shortChord=false; chordSince=now;
            // A wake-only or already-activated single press cannot become a chord.
            consumed=suppress || !left.armed || !right.armed ||
                (left.down && (left.consumed || left.fired)) ||
                (right.down && (right.consumed || right.fired));
        }
        if(chord) {
            if(suppress) consumed=true;
            uint8_t event=NONE;
            if(!ended) {
                const uint32_t elapsed=now-chordSince;
                if(!consumed && !fired && elapsed>=HOLD_MS) {fired=true;event=POWER_OFF;}
                if(!l || !r) {ended=true;shortChord=elapsed>=25;}
            }
            // Consume both constituent gestures until BOTH buttons are released.
            left.tick(l,now,true,BACK,HOME);right.tick(r,now,true,NEXT,SELECT);
            if(!l && !r) {
                if(!consumed && !fired && shortChord) event=MENU;
                chord=false;
            }
            return (wake ? WAKE : NONE) | event;
        }
        const uint8_t a = left.tick(l, now, suppress, BACK, HOME);
        const uint8_t b = right.tick(r, now, suppress, NEXT, SELECT);
        return (wake ? WAKE : NONE) | a | b;
    }
};

// Context includes the page and modal, not just AppState (Settings is nested).
class FocusMemory {
    struct Entry { uint16_t context = 0xffff; int id = -1; uint8_t index = 0; uint32_t age = 0; } entries[48];
    uint32_t clock = 0;
public:
    void save(uint16_t context, int id, uint8_t index) {
        Entry* slot = &entries[0];
        for (auto& e : entries) {
            if (e.context == context || e.context == 0xffff) { slot = &e; break; }
            if (e.age < slot->age) slot = &e;
        }
        slot->context = context; slot->id = id; slot->index = index; slot->age = ++clock;
    }
    uint8_t restore(uint16_t context, const int* ids, uint8_t count) const {
        if (!count) return 0;
        for (const auto& e : entries) if (e.context == context) {
            for (uint8_t i = 0; i < count; ++i) if (ids[i] == e.id) return i;
            return e.index < count ? e.index : 0;
        }
        return 0;
    }
};
}
