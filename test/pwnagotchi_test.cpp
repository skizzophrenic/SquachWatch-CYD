// The pwnagotchi beacon parser.
//
// A pwnagotchi announces itself to other pwnagotchis by putting a JSON
// blob inside a vendor information element in its own beacon frames. It is
// plain ASCII and it carries the unit's name, version, uptime, handshake
// count and whether deauth is switched on.
//
// The parser runs inside the promiscuous WiFi callback, on every beacon in
// the air, against a buffer an unknown stranger composed. So the cases
// worth writing down are not really "does it find the name" -- they are
// the refusals: a frame that is nearly right, a name that never closes its
// quote, a length that lies, bytes that are not text.
#include "signatures.h"
#include "test_util.h"
#include <cstring>
#include <cstdio>

// A beacon is a 24-byte header then 12 bytes of fixed parameters, so the
// information elements -- and anything a pwnagotchi added -- start at 36.
// Everything before that is filled with a recognisable pattern rather than
// left uninitialised.
static uint8_t f[600];

static uint32_t build(const char* body, size_t n) {
    memset(f, 0xAA, sizeof(f));
    if (36 + n > sizeof(f)) { fprintf(stderr, "test frame too big\n"); return 0; }
    memcpy(f + 36, body, n);
    return (uint32_t)(36 + n);
}

// Length from the literal, not from strlen. A real information element
// starts with a tag and a length byte, and a length byte of 0x00 is
// perfectly legal -- measuring these frames with strlen truncates every
// case with a binary prefix to nothing, which is a good way to write a
// test suite that passes while testing an empty buffer.
#define BEACON(lit) build(lit, sizeof(lit) - 1)

// Trimmed from a real grid advertisement: the vendor IE header, then the
// keys in the order they actually appear.
#define REAL \
    "\xde\x50\x00\x01" \
    "{\"epoch\":41,\"face\":\"(^-^)\",\"identity\":\"a1b2c3\"," \
    "\"name\":\"pwnimus\",\"policy\":{\"deauth\":true},\"pwnd_run\":2," \
    "\"pwnd_tot\":137,\"session_id\":\"de:ad:be:ef\",\"uptime\":90210," \
    "\"version\":\"1.5.5\"}"

int main() {
    char name[33];
    uint32_t len;

    suite("A real advertisement");

    len = BEACON(REAL);
    name[0] = '\0';
    ck("matches", pwnagotchiName(f, len, name, sizeof(name)));
    ck("and reads the name back", strcmp(name, "pwnimus") == 0);

    suite("Both keys are required");

    // The handshake counter is what makes this a pwnagotchi rather than
    // any other device that happens to put JSON in a beacon.
    len = BEACON("{\"name\":\"pwnimus\",\"uptime\":5}");
    name[0] = '\0';
    ck("a name without pwnd_tot does not match", !pwnagotchiName(f, len, name, sizeof(name)));
    ck("...and leaves the buffer alone", name[0] == '\0');

    len = BEACON("{\"pwnd_tot\":137,\"uptime\":5}");
    ck("pwnd_tot without a name does not match", !pwnagotchiName(f, len, name, sizeof(name)));

    // An ordinary AP beacon -- the overwhelming majority of what this runs
    // on. Note the leading 0x00: that is the SSID element's tag, and it is
    // why these frames cannot be measured with strlen.
    len = BEACON("\x00\x08" "HomeWiFi" "\x01\x04\x82\x84\x8b\x96");
    ck("an ordinary AP beacon does not match", !pwnagotchiName(f, len, name, sizeof(name)));

    suite("Layout is not assumed");

    len = BEACON("{ \"pwnd_tot\" : 3 , \"name\" : \"spaced\" }");
    name[0] = '\0';
    ck("whitespace around the colon is fine", pwnagotchiName(f, len, name, sizeof(name)));
    ck("name still read", strcmp(name, "spaced") == 0);

    // Key order varies between pwnagotchi versions.
    len = BEACON("{\"name\":\"first\",\"pwnd_tot\":9}");
    name[0] = '\0';
    ck("name before pwnd_tot is fine", pwnagotchiName(f, len, name, sizeof(name)));
    ck("name still read", strcmp(name, "first") == 0);

    suite("Malformed input, which is the point");

    len = BEACON("{\"pwnd_tot\":1,\"name\":\"unterminated");
    name[0] = '\0';
    ck("an unterminated name stays inside the buffer",
       !pwnagotchiName(f, len, name, sizeof(name)) || strlen(name) < sizeof(name));

    len = BEACON("{\"pwnd_tot\":1,\"name\":\"\"}");
    ck("an empty name is not a match", !pwnagotchiName(f, len, name, sizeof(name)));

    // A control character in a string the LOG renders would be somebody
    // else deciding what our screen does.
    len = BEACON("{\"pwnd_tot\":1,\"name\":\"ev\x01il\"}");
    ck("a control character rejects the name",
       !pwnagotchiName(f, len, name, sizeof(name)));

    // Longer than the field: truncate, never overflow.
    len = BEACON("{\"pwnd_tot\":1,\"name\":"
                 "\"012345678901234567890123456789012345678901234567890\"}");
    name[0] = '\0';
    ck("an over-long name still matches", pwnagotchiName(f, len, name, sizeof(name)));
    ck("...truncated to the buffer", strlen(name) == sizeof(name) - 1);

    suite("Lengths and pointers");

    len = BEACON(REAL);
    ck("a length shorter than the header is safe",
       !pwnagotchiName(f, 20, name, sizeof(name)));
    ck("a length of exactly the element offset is safe",
       !pwnagotchiName(f, 36, name, sizeof(name)));
    // sig_len comes from the driver and is not trusted as an upper bound;
    // the parser caps its own scan. Running clean under a sanitiser is the
    // real assertion here -- the return value is not the interesting part.
    (void)pwnagotchiName(f, 100000, name, sizeof(name));
    ck("an absurd length does not walk off the buffer", true);
    ck("a null frame is safe", !pwnagotchiName(nullptr, len, name, sizeof(name)));
    ck("a null output is safe", !pwnagotchiName(f, len, nullptr, sizeof(name)));
    ck("a one-byte output is safe", !pwnagotchiName(f, len, name, 1));

    return report();
}
