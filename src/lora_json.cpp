// SquachWatch-CYD — the JSON scanner. See include/lora_json.h for why the
// allow-list lives in one file instead of two.
#include "lora_json.h"
#include <stdio.h>
#include <string.h>

namespace Lora {
namespace Json {
namespace {

// The needle for a key, as it appears in a document: quoted. 24 bytes is the
// longest key any caller names plus its quotes and NUL -- "advert_pubkey" is
// thirteen, "aircraft_model" fourteen.
void needleFor(const char* key, char* out, size_t cap) { snprintf(out, cap, "\"%s\"", key); }

// The first occurrence of `needle` in [from,to), or nullptr. Plain bytes: a key
// name cannot contain an escape, so there is nothing here to be clever about.
const char* find(const char* from, const char* to, const char* needle, size_t nl) {
    for (const char* p = from; p + nl <= to; p++) if (memcmp(p, needle, nl) == 0) return p;
    return nullptr;
}

// Past a key's colon to the first byte of its value.
const char* toValue(const char* p, const char* to) {
    while (p < to && (*p == ' ' || *p == ':')) p++;
    return p;
}

// The byte after the bracket that closes the one at `p`, or nullptr when the
// document ends first. String-aware, so a brace inside a name does not count --
// an advert called "{}" is a name, not a nesting level.
const char* closeOf(const char* p, const char* to, char open, char close) {
    int depth = 0;
    bool inStr = false;
    for (; p < to; p++) {
        if (inStr) {
            if (*p == '\\') p++;            // the escaped byte, whatever it is
            else if (*p == '"') inStr = false;
            continue;
        }
        if (*p == '"') inStr = true;
        else if (*p == open) depth++;
        else if (*p == close && --depth == 0) return p + 1;
    }
    return nullptr;
}

bool bracketed(const char* from, const char* to, const char* key,
               char open, char close, const char*& begin, const char*& end) {
    char needle[24];
    needleFor(key, needle, sizeof needle);
    const char* p = find(from, to, needle, strlen(needle));
    if (!p) return false;
    p = toValue(p + strlen(needle), to);
    if (p >= to || *p != open) return false;
    const char* e = closeOf(p, to, open, close);
    if (!e) return false;
    begin = p;
    end = e;
    return true;
}

}  // namespace

bool str(const char* from, const char* to, const char* key, char* out, size_t cap) {
    if (!out || !cap) return false;
    out[0] = '\0';
    char needle[24];
    needleFor(key, needle, sizeof needle);
    const size_t nl = strlen(needle);
    const char* p = find(from, to, needle, nl);
    if (!p) return false;
    p = toValue(p + nl, to);
    if (p >= to || *p != '"') return false;      // null, a number or an object
    p++;
    size_t o = 0;
    while (p < to && *p != '"') {
        char c = *p++;
        if (c == '\\' && p < to) {
            const char e = *p++;
            c = (e == '"' || e == '\\' || e == '/') ? e : '?';
            if (e == 'u') for (int i = 0; i < 4 && p < to; i++) p++;
        }
        if ((unsigned char)c >= 0x80) c = '?';
        if (o + 1 < cap) out[o++] = c;
    }
    out[o] = '\0';
    return o != 0;
}

bool obj(const char* from, const char* to, const char* key, const char*& begin, const char*& end) {
    return bracketed(from, to, key, '{', '}', begin, end);
}

bool arr(const char* from, const char* to, const char* key, const char*& begin, const char*& end) {
    return bracketed(from, to, key, '[', ']', begin, end);
}

bool nextObj(const char*& p, const char* to, const char*& begin, const char*& end) {
    // Only top-level braces of the array body: a row's own nested object would
    // otherwise be handed back as a second row.
    bool inStr = false;
    for (; p < to; p++) {
        if (inStr) {
            if (*p == '\\') p++;
            else if (*p == '"') inStr = false;
            continue;
        }
        if (*p == '"') { inStr = true; continue; }
        if (*p != '{') continue;
        const char* e = closeOf(p, to, '{', '}');
        // An unterminated object is what a TRUNCATED body looks like -- the feed
        // is read into a fixed buffer and a long answer WILL be cut mid-row --
        // and it is also what the document's own outer brace looks like when the
        // walk starts at byte zero. Both want the same thing: step over the
        // brace and keep scanning, which descends into the unterminated
        // container and finds the complete objects inside it. Stopping here
        // instead would throw away every good row in a cut body, and would find
        // nothing at all in a body walked from its first byte.
        if (!e) continue;
        begin = p;
        end = e;
        p = e;
        return true;
    }
    return false;
}

bool has(const char* from, const char* to, const char* what) {
    return find(from, to, what, strlen(what)) != nullptr;
}

}
}
