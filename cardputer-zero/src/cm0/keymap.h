// SquachWatch-CM0 -- the Cardputer Zero keyboard's characters.
//
// The keyboard is a TCA8418 matrix the kernel's tca8418c driver reports as
// standard Linux key codes on a US layout, with a real LEFTSHIFT (and real
// arrow keys; Fn combinations arrive as the codes the driver makes of them).
// main_linux.cpp turns those codes into the characters the firmware's text
// screens take (the squad name, a WiFi password, the PIN) -- see typeChar()
// there. Header-only, for keymap_test.
#pragma once
#include <cstdint>
#include <linux/input-event-codes.h>

namespace cm0 {

// The character a key types, with or without SHIFT; 0 for keys that do not
// type (navigation, modifiers, function keys).
inline char evdevChar(uint16_t code, bool shift) {
    static const char* const LETTERS = "qwertyuiopasdfghjklzxcvbnm";
    static const uint16_t LETTER_CODES[26] = {
        KEY_Q, KEY_W, KEY_E, KEY_R, KEY_T, KEY_Y, KEY_U, KEY_I, KEY_O, KEY_P,
        KEY_A, KEY_S, KEY_D, KEY_F, KEY_G, KEY_H, KEY_J, KEY_K, KEY_L,
        KEY_Z, KEY_X, KEY_C, KEY_V, KEY_B, KEY_N, KEY_M,
    };
    for (int i = 0; i < 26; i++)
        if (code == LETTER_CODES[i]) return shift ? (char)(LETTERS[i] - 'a' + 'A') : LETTERS[i];
    struct Pair { uint16_t code; char plain, shifted; };
    static const Pair OTHERS[] = {
        { KEY_1, '1', '!' }, { KEY_2, '2', '@' }, { KEY_3, '3', '#' }, { KEY_4, '4', '$' },
        { KEY_5, '5', '%' }, { KEY_6, '6', '^' }, { KEY_7, '7', '&' }, { KEY_8, '8', '*' },
        { KEY_9, '9', '(' }, { KEY_0, '0', ')' },
        { KEY_MINUS, '-', '_' }, { KEY_EQUAL, '=', '+' },
        { KEY_LEFTBRACE, '[', '{' }, { KEY_RIGHTBRACE, ']', '}' }, { KEY_BACKSLASH, '\\', '|' },
        { KEY_SEMICOLON, ';', ':' }, { KEY_APOSTROPHE, '\'', '"' }, { KEY_GRAVE, '`', '~' },
        { KEY_COMMA, ',', '<' }, { KEY_DOT, '.', '>' }, { KEY_SLASH, '/', '?' },
        { KEY_SPACE, ' ', ' ' },
        { KEY_KPASTERISK, '*', '*' }, { KEY_KPMINUS, '-', '-' }, { KEY_KPPLUS, '+', '+' },
        { KEY_KPDOT, '.', '.' },
        { KEY_KP0, '0', '0' }, { KEY_KP1, '1', '1' }, { KEY_KP2, '2', '2' }, { KEY_KP3, '3', '3' },
        { KEY_KP4, '4', '4' }, { KEY_KP5, '5', '5' }, { KEY_KP6, '6', '6' }, { KEY_KP7, '7', '7' },
        { KEY_KP8, '8', '8' }, { KEY_KP9, '9', '9' },
    };
    for (const Pair& p : OTHERS)
        if (code == p.code) return shift ? p.shifted : p.plain;
    return 0;
}

}   // namespace cm0
