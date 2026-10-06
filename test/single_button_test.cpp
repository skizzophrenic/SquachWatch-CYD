#include "single_button.h"
#include <cassert>
#include <cstdio>
using Event = SingleButton::Event;
int main() {
    SingleButton b;
    // Contact bounce never creates a tap.
    assert(b.tick(true, 10) == Event::NONE);
    assert(b.tick(false, 20) == Event::NONE);
    assert(b.tick(false, 50) == Event::NONE);
    // Short press reports once, after release has settled.
    assert(b.tick(true, 100) == Event::NONE);
    assert(b.tick(true, 125) == Event::NONE);
    assert(b.tick(false, 200) == Event::NONE);
    assert(b.tick(false, 225) == Event::TAP);
    assert(b.tick(false, 250) == Event::NONE);
    // A long press reports once; its release must not also cycle pages.
    assert(b.tick(true, 300) == Event::NONE);
    assert(b.tick(true, 325) == Event::NONE);
    assert(b.tick(true, 1024) == Event::NONE);
    assert(b.tick(true, 1025) == Event::HOLD);
    assert(b.tick(true, 1500) == Event::NONE);
    assert(b.tick(false, 1600) == Event::NONE);
    assert(b.tick(false, 1625) == Event::NONE);
    // Unsigned elapsed time preserves gestures across millis() rollover.
    SingleButton wrap;
    assert(wrap.tick(true, UINT32_MAX - 100) == Event::NONE);
    assert(wrap.tick(true, UINT32_MAX - 75) == Event::NONE);
    assert(wrap.tick(true, 624) == Event::HOLD);
    assert(wrap.tick(false, 650) == Event::NONE);
    assert(wrap.tick(false, 675) == Event::NONE);
    std::puts("single_button: bounce, tap, hold, release and rollover passed");
}
