// SquachWatch-CYD — a JSON scanner, not a JSON library.
//
// src/ota_wifi.cpp:238 established the pattern and the reason: a scanner reads
// the handful of keys it wants and cannot be surprised by the rest of the
// document. For the online lookups that is not tidiness, it is the whole
// defence -- a key this does not name is a key that never reaches RAM this side
// of the socket buffer (include/lora_enrich.h rule 4: an ALLOW-list, never a
// deny-list).
//
// WHY IT IS ITS OWN FILE. It was born inside src/lora_enrich.cpp, private to
// the two response parsers there. The MeshCore adverts feed
// (include/lora_feed.h) needs the same reads over a different body, and a
// second copy of a scanner that implements a privacy rule is a scanner where a
// fix to one copy silently does not reach the other: the escape handling in
// str() below exists because an unescaped \" ends a value early and scrambles
// every field after it, and that bug would have to be found twice. One
// implementation, one host test.
//
// Every function takes [from,to) rather than a NUL-terminated string, because
// the callers search INSIDE one object of a larger body -- identically named
// keys live in the siblings, and hamrig's envelope carries a `source` both
// inside the callsign object and outside it.
#pragma once
#include <stddef.h>

namespace Lora {
namespace Json {

// The value of "key" as a string, searched only between from and to. False
// when the key is absent, when its value is not a string (null, a number, an
// object) or when the string is empty -- the callers treat all of those the
// same way, which is to leave their field blank.
//
// Escapes are READ rather than copied: \" would otherwise end the value early.
// The fonts are ASCII, so anything this cannot spell becomes a question mark --
// measured on this feed, one advert name in eighty carries an emoji.
bool str(const char* from, const char* to, const char* key, char* out, size_t cap);

// The object that "key" names, as [begin,end) covering both braces. Brace
// counting, string-aware.
bool obj(const char* from, const char* to, const char* key, const char*& begin, const char*& end);

// The array that "key" names, as [begin,end) covering both brackets.
bool arr(const char* from, const char* to, const char* key, const char*& begin, const char*& end);

// The next object at or after *p and before to, as [begin,end); p is advanced
// past it so a loop walks an array of objects. Nested objects are skipped as
// part of their parent, not returned separately. False when there is no
// further object, which is how the loop ends.
bool nextObj(const char*& p, const char* to, const char*& begin, const char*& end);

// Whether the literal bytes appear anywhere in [from,to). For the sentinel
// bodies that are a miss rather than an answer -- hamrig's "NOT_FOUND",
// OGN's "devices":[] -- which have to be recognised BEFORE any field is read.
bool has(const char* from, const char* to, const char* what);

}
}
