// SquachWatch-CYD — the offline half of node enrichment. See include/lora_ident.h.
//
// THE EXTRACTION RULES, and what each is worth. Every figure was measured over
// three live corpora -- 63,724 MeshCore advert names, 9,915 meshmap.net and
// 31,137 liamcottle Meshtastic long names, 104,776 in all.
//
//   1. Shape. An ITU-shaped token, word-bounded: a one- or two-character
//      prefix (letters, or a letter and a digit either way round), a
//      separating digit, one to four letters, and an optional -NN of the kind
//      APRS appends. Fires on 13.3 %, 13.0 % and 12.5 % of names in the three
//      corpora -- about one name in eight, strikingly consistent across two
//      networks and three independent collections. The word boundaries are
//      what keep it off hex strings and model numbers inside longer words.
//
//   2. Reject a four-character pure-hex token that renders the node's own
//      identifier. THE RULE THAT EARNS ITS KEEP: both mesh firmwares append
//      their own id to their default name, and pure-hex tokens are 17.9 % and
//      24.4 % of all tokens in the two Meshtastic corpora, of which 93.6 %
//      and 92.3 % are the node's own number; in MeshCore 2.2 % and 78.6 %.
//      So one rule removes 79 to 94 % of the false positives, and it costs
//      nothing because the row already holds both identifiers.
//      It is deliberately NOT a ban on four-character hex: EC3F is a
//      plausible Spanish callsign, and the residue that is hex but not
//      self-referential is only 1.2 to 1.9 % of tokens.
//
//   3. Reject a token whose prefix has no cty.dat allocation. Worth 1.1 % on
//      its own -- nearly every one- or two-character prefix belongs to
//      somebody, F8CA to France and 2A2C to 2A -- so this is NOT validation
//      and must not be sold as it. It is here because the table has to exist
//      anyway for the country, and because it does catch the real garbage:
//      1C8WR, the one genuine false positive in a 30-token live check.
//
//   4. More than one token means an owner/operator pair. Kept, both of them,
//      for the caller to show together or not at all.
//
//   5. Three- and four-character tokens pass but deserve less confidence than
//      five to seven. Real four-character club calls exist (HB9Y, W3YJ), so
//      Call::len is reported rather than the short ones dropped.
//
// What the rules do not do is verify anything. 26 of 30 extracted tokens
// resolved in a live callsign database, 3 were structurally valid UK
// Foundation calls simply absent from it, and 1 was garbage -- so the tokens
// are good, and they are still guesses about strangers.
#include "lora_ident.h"
#include "lora_dxcc_table.h"
#include <string.h>
#include <stdio.h>

namespace Lora {
namespace Ident {

// ---- Maidenhead --------------------------------------------------------------
bool grid(int32_t latE7, int32_t lonE7, char* out, size_t cap) {
    if (!out || cap < 7) return false;
    // Degrees x 1e7 from the south-west corner, so the arithmetic is unsigned
    // and nothing depends on how this compiler rounds a negative division.
    // Clamped one ten-millionth of a degree inside the far edge: 90 N would
    // otherwise index field 18, which is past R.
    int64_t la = (int64_t)latE7 + 900000000ll;
    int64_t lo = (int64_t)lonE7 + 1800000000ll;
    if (la < 0) la = 0; if (la > 1799999999ll) la = 1799999999ll;
    if (lo < 0) lo = 0; if (lo > 3599999999ll) lo = 3599999999ll;
    const uint32_t lat = (uint32_t)la, lon = (uint32_t)lo;
    out[0] = (char)('A' + lon / 200000000u);            // 20 degrees of longitude
    out[1] = (char)('A' + lat / 100000000u);            // 10 degrees of latitude
    out[2] = (char)('0' + (lon % 200000000u) / 20000000u);
    out[3] = (char)('0' + (lat % 100000000u) / 10000000u);
    // Subsquares are 24 to a square: 5 minutes of longitude, 2.5 of latitude.
    out[4] = (char)('a' + ((lon % 20000000u) * 24u) / 20000000u);
    out[5] = (char)('a' + ((lat % 10000000u) * 24u) / 10000000u);
    out[6] = '\0';
    return true;
}

// ---- DXCC --------------------------------------------------------------------
static const size_t DXCC_N = sizeof DXCC_PFX / sizeof DXCC_PFX[0];

static bool lookupExact(const char* key, Dxcc& out) {
    size_t lo = 0, hi = DXCC_N;
    while (lo < hi) {
        const size_t mid = (lo + hi) / 2;
        const int c = strcmp(DXCC_PFX[mid].p, key);
        if (c == 0) {
            const DxccEntity& e = DXCC_ENT[DXCC_PFX[mid].ent];
            out.entity = e.name;
            out.continent = DXCC_CONT[e.cont];
            return true;
        }
        if (c < 0) lo = mid + 1; else hi = mid;
    }
    return false;
}

bool dxcc(const char* call, Dxcc& out) {
    out.entity = out.continent = nullptr;
    if (!call || !call[0]) return false;
    char key[DXCC_MAX_PFX + 1];
    size_t n = 0;
    while (n < DXCC_MAX_PFX && call[n] && call[n] != '-') {
        const char c = call[n];
        key[n] = (c >= 'a' && c <= 'z') ? (char)(c - 32) : c;
        n++;
    }
    // Longest prefix first, so a row is reached only when every shorter
    // prefix of it misses. That is what makes BV9P Pratas and BV Taiwan.
    for (size_t l = n; l >= 1; l--) {
        key[l] = '\0';
        if (lookupExact(key, out)) return true;
    }
    return false;
}

// ---- extraction --------------------------------------------------------------
static inline bool isUp(char c)    { return c >= 'A' && c <= 'Z'; }
static inline bool isDig(char c)   { return c >= '0' && c <= '9'; }
static inline bool isAlnum(char c) { return isUp(c) || isDig(c); }
static inline char up(char c)      { return (c >= 'a' && c <= 'z') ? (char)(c - 32) : c; }

static inline bool hexVal(char c, uint8_t& v) {
    if (isDig(c)) { v = (uint8_t)(c - '0'); return true; }
    if (c >= 'A' && c <= 'F') { v = (uint8_t)(c - 'A' + 10); return true; }
    return false;
}

// Rule 2. True when tok is four hex digits whose value is this node's own id.
static bool isSelfHex(const char* tok, uint8_t len, int32_t self) {
    if (self < 0 || len != 4) return false;
    uint32_t v = 0;
    for (uint8_t i = 0; i < 4; i++) {
        uint8_t d;
        if (!hexVal(tok[i], d)) return false;
        v = (v << 4) | d;
    }
    return v == (uint32_t)self;
}

int32_t selfHex(const Nodes::Node& n) {
    switch (n.proto) {
        // The low 16 bits of the node number, which is what Node::id holds.
        case Proto::MESHTASTIC: return (int32_t)(uint16_t)(n.id & 0xFFFFu);
        // The first two bytes of the advert public key. Node::id is the first
        // eight bytes, most significant first (src/lora_nodes.cpp), so the
        // two that the firmware renders are the top two.
        case Proto::MESHCORE:   return (int32_t)(uint16_t)((n.id >> 48) & 0xFFFFu);
        default:                return -1;
    }
}

// Rule 1's prefix: how many characters of it s starts with, 0 for none.
//
// Two characters when that leaves a digit next, one otherwise, and never the
// other way round -- when both readings are structurally available the
// one-character one cannot complete, because it would need a letter where the
// two-character one found the separating digit. So this is not a shortcut past
// the regex's backtracking; it is the same answer.
static uint8_t prefixLen(const char* s) {
    const bool two = (isUp(s[0]) && isUp(s[1])) ||
                     (isDig(s[0]) && isUp(s[1])) ||
                     (isUp(s[0]) && isDig(s[1]));
    if (two && isDig(s[2])) return 2;
    if (isUp(s[0]) && isDig(s[1])) return 1;    // a lone digit is not a prefix
    return 0;
}

// One token starting at s[0], which the caller has already established is at a
// word boundary. Returns its length in characters (callsign plus any SSID), or
// 0 for no match. `callLen` comes back as the callsign's own length.
static uint8_t matchToken(const char* s, uint8_t& callLen) {
    uint8_t i = prefixLen(s);
    if (!i || !isDig(s[i])) return 0;
    i++;
    uint8_t letters = 0;
    while (letters < 4 && isUp(s[i + letters])) letters++;
    if (!letters) return 0;
    i = (uint8_t)(i + letters);
    // The negative lookahead. A fifth letter or a trailing digit means this is
    // not a callsign at all, and no shorter reading of it is either: every
    // backtrack ends against the same character.
    if (isAlnum(s[i])) return 0;
    callLen = i;
    // Rule 1's optional SSID, the -7 in DH5DAX-7.
    if (s[i] == '-' && isDig(s[i + 1])) {
        uint8_t j = (uint8_t)(i + 2);
        if (isDig(s[j])) j++;
        if (!isAlnum(s[j]) && s[j] != '-') i = j;
    }
    return i;
}

uint8_t calls(const char* name, int32_t self, Call* out, uint8_t cap) {
    if (!name || !out || !cap) return 0;
    // Upper-cased into a local buffer, zero-padded so matchToken can read a
    // few characters past any start position without a bounds test of its own.
    char u[72];
    size_t n = 0;
    while (name[n] && n < sizeof u - 8) { u[n] = up(name[n]); n++; }
    memset(u + n, 0, sizeof u - n);

    uint8_t found = 0;
    for (size_t i = 0; i < n; i++) {
        if (i && isAlnum(u[i - 1])) continue;       // the word boundary
        uint8_t callLen = 0;
        const uint8_t tokLen = matchToken(u + i, callLen);
        if (!tokLen) continue;
        const char* tok = u + i;
        i += tokLen - 1;                            // never look inside a match
        if (isSelfHex(tok, callLen, self)) continue;                 // rule 2
        Call c;
        memset(&c, 0, sizeof c);
        const uint8_t keep = tokLen < sizeof c.text ? tokLen : (uint8_t)(sizeof c.text - 1);
        memcpy(c.text, tok, keep);
        c.len = callLen;
        Dxcc d;
        if (!dxcc(c.text, d)) continue;             // rule 3: no allocation, no token
        c.entity = d.entity;
        c.continent = d.continent;
        // Counted even when there is no room to write it, because the count is
        // what tells a caller a name holds more than one callsign, and that is
        // the fact rule 4 turns on.
        if (found < cap) out[found] = c;
        found++;
    }
    return found;
}

void describe(const Nodes::Node& n, Ident& out) {
    memset(&out, 0, sizeof out);
    if (n.hasPos) grid(n.latE7, n.lonE7, out.grid, sizeof out.grid);
    switch (n.proto) {
        case Proto::APRS:
        case Proto::MESHCOM: {
            // The tag IS the callsign here: a station identification the
            // licence requires, not a guess. No extraction, no claim.
            out.claimed = false;
            Call& c = out.call[0];
            strncpy(c.text, n.tag, sizeof c.text - 1);
            uint8_t l = 0;
            while (c.text[l] && c.text[l] != '-') l++;
            c.len = l;
            Dxcc d;
            if (dxcc(c.text, d)) { c.entity = d.entity; c.continent = d.continent; }
            out.callCount = c.text[0] ? 1 : 0;
            break;
        }
        case Proto::MESHTASTIC:
        case Proto::MESHCORE:
            out.claimed = true;
            out.callCount = calls(n.name, selfHex(n), out.call, CALL_MAX);
            break;
        default:
            break;      // LoRaWAN names a thing, FANET an aircraft: no callsign
    }
}

}
}
