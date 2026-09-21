#include "cardputer_keys.h"
#include <assert.h>
#include <stdio.h>
using namespace CardputerKeys;

int main() {
    // Every physical switch maps to exactly one of the 56 coordinates.
    uint64_t seen = 0;
    for (unsigned bank = 0; bank < 8; ++bank) {
        for (unsigned input = 0; input < 7; ++input) {
            const uint64_t b = matrixBit(bank, input);
            assert(!(seen & b));
            seen |= b;
        }
    }
    assert(seen == ((uint64_t(1) << 56) - 1));
    // Wiring examples independently taken from M5Stack's matrix reader:
    // bank 0 => bottom odd columns; bank 7 => top even columns.
    assert(matrixBit(0, 5) == keyMask(3, 11));
    assert(matrixBit(7, 0) == keyMask(0, 0));
    assert(decode(matrixBit(1, 6)) == Action::OPEN);
    assert(decode(matrixBit(5, 5)) == Action::LOG);
    assert(decode(keyMask(2, 0) | keyMask(2, 11)) == Action::UP); // Fn + up
    assert(decode(keyMask(2, 0)) == Action::NONE);
    assert(decode(keyMask(1, 10)) == Action::PET);
    assert(decode(keyMask(1, 9)) == Action::OUTFIT);
    assert(decode(keyMask(3, 7)) == Action::BINGO);
    assert(decode(keyMask(3, 13)) == Action::SHOW);
    assert(decode(keyMask(3, 5)) == Action::SHADES);

    Debouncer d;
    const uint64_t enter = keyMask(2, 13);
    assert(d.update(enter, 10) == Action::NONE);
    assert(d.update(0, 15) == Action::NONE); // contact bounce
    assert(d.update(enter, 18) == Action::NONE);
    assert(d.update(enter, 37) == Action::NONE);
    assert(d.update(enter, 38) == Action::OPEN);
    assert(d.update(enter, 900) == Action::NONE); // held key doesn't repeat
    assert(d.update(0, 901) == Action::NONE);
    assert(d.update(0, 921) == Action::NONE);
    assert(d.update(enter, 930) == Action::NONE);
    assert(d.update(enter, 950) == Action::OPEN);

    Debouncer wrap;
    assert(wrap.update(keyMask(2, 4), UINT32_MAX - 9) == Action::NONE);
    assert(wrap.update(keyMask(2, 4), 10) == Action::DIAGNOSTICS);
    puts("cardputer keyboard: mapping, bounce, hold, release, timer wrap passed");
}
