// keymap_test -- the Cardputer Zero keyboard's characters (keymap.h): letters
// with and without SHIFT, the number row's symbols, punctuation, and the
// keys that must NOT type (navigation, modifiers, function keys).
#include "keymap.h"

#include <cstdio>

static int g_checks = 0, g_failures = 0;
#define CHECK(cond, what) do { g_checks++; if (cond) std::printf("ok  : %s\n", what); \
    else { g_failures++; std::printf("FAIL: %s\n", what); } } while (0)

int main() {
    using cm0::evdevChar;
    CHECK(evdevChar(KEY_A, false) == 'a' && evdevChar(KEY_A, true) == 'A', "A: a / A");
    CHECK(evdevChar(KEY_Q, false) == 'q' && evdevChar(KEY_M, false) == 'm', "Q and M: the row ends");
    CHECK(evdevChar(KEY_Z, true) == 'Z' && evdevChar(KEY_P, true) == 'P', "Z and P shifted");
    bool all = true;
    const uint16_t letters[] = { KEY_A, KEY_B, KEY_C, KEY_D, KEY_E, KEY_F, KEY_G, KEY_H, KEY_I, KEY_J,
                                 KEY_K, KEY_L, KEY_M, KEY_N, KEY_O, KEY_P, KEY_Q, KEY_R, KEY_S, KEY_T,
                                 KEY_U, KEY_V, KEY_W, KEY_X, KEY_Y, KEY_Z };
    for (int i = 0; i < 26; i++) all = all && evdevChar(letters[i], false) == 'a' + i;
    CHECK(all, "all 26 letters map to themselves");
    CHECK(evdevChar(KEY_1, false) == '1' && evdevChar(KEY_1, true) == '!', "1: 1 / !");
    CHECK(evdevChar(KEY_0, false) == '0' && evdevChar(KEY_0, true) == ')', "0: 0 / )");
    CHECK(evdevChar(KEY_2, true) == '@' && evdevChar(KEY_9, true) == '(', "2 / 9 shifted: @ (");
    CHECK(evdevChar(KEY_SPACE, false) == ' ' && evdevChar(KEY_SPACE, true) == ' ', "SPACE types a space");
    CHECK(evdevChar(KEY_COMMA, false) == ',' && evdevChar(KEY_DOT, true) == '>', "comma, dot");
    CHECK(evdevChar(KEY_SLASH, true) == '?' && evdevChar(KEY_SEMICOLON, true) == ':', "? and :");
    CHECK(evdevChar(KEY_APOSTROPHE, false) == '\'' && evdevChar(KEY_APOSTROPHE, true) == '"', "quote");
    CHECK(evdevChar(KEY_KP7, false) == '7' && evdevChar(KEY_KPASTERISK, false) == '*', "keypad codes");
    CHECK(evdevChar(KEY_ENTER, false) == 0 && evdevChar(KEY_BACKSPACE, false) == 0 &&
          evdevChar(KEY_TAB, false) == 0 && evdevChar(KEY_ESC, false) == 0, "ENTER/BACKSPACE/TAB/ESC do not type");
    CHECK(evdevChar(KEY_UP, false) == 0 && evdevChar(KEY_LEFT, true) == 0, "arrows do not type");
    CHECK(evdevChar(KEY_LEFTSHIFT, false) == 0 && evdevChar(KEY_LEFTCTRL, false) == 0 &&
          evdevChar(KEY_LEFTALT, false) == 0, "modifiers do not type");
    CHECK(evdevChar(KEY_F1, false) == 0 && evdevChar(KEY_VOLUMEUP, false) == 0, "function/media keys do not type");
    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
