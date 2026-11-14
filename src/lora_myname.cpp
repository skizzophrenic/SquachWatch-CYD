// SquachWatch-CYD — the name this board goes by on LoRa. See include/lora_myname.h.
#include "lora_myname.h"
#include <string.h>

namespace Lora {
namespace MyName {

namespace {
bool blank(const char* s) {
    if (!s) return true;
    for (; *s; s++) if (*s != ' ') return false;
    return true;
}
}

const char* longName(const char* custom, const char* nick) {
    if (!blank(custom)) return custom;
    if (!blank(nick))   return nick;
    return "SQUACHY";
}

void shortName(const char* customShort, const char* longName, char* out, size_t cap) {
    if (!out || !cap) return;
    out[0] = '\0';
    if (cap < 2) return;
    const size_t lim = cap - 1 < SHORT_LEN ? cap - 1 : SHORT_LEN;
    const char* src = nullptr;
    if (!blank(customShort)) {
        src = customShort;
        while (*src == ' ') src++;
    } else if (!longName || !*longName || !strcmp(longName, "SQUACHY")) {
        src = "SQWT";
    } else {
        src = longName;
    }
    size_t n = 0;
    for (; n < lim && src[n]; n++) out[n] = src[n];
    // A short name cut through the middle of a word can end on a space.
    while (n && out[n - 1] == ' ') n--;
    out[n] = '\0';
}

size_t clean(char* s, size_t max) {
    if (!s) return 0;
    size_t w = 0;
    for (size_t r = 0; s[r]; r++) {
        const unsigned char c = (unsigned char)s[r];
        if (c < 32 || c > 126 || c == ':') continue;
        if (c == ' ' && w == 0) continue;           // leading spaces
        if (w < max) s[w++] = (char)c;
    }
    while (w && s[w - 1] == ' ') w--;               // trailing spaces
    s[w] = '\0';
    return w;
}

}
}
