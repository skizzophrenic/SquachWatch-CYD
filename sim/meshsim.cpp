// SquachWatch-Sim — the virtual SquachMesh peer. See meshsim.h.
#include "meshsim.h"

#if SQUACH_MESH
#include <Arduino.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>   // strcasecmp
#include "detection.h"
#include "squachmesh.h"
#include "squachy.h"
#include "settings.h"
#include "meshmsg.h"
#include "meshtalk.h"
#include "meshcrypto.h"

namespace {

// Locally administered addresses, so neither can be mistaken for a real one
// in a log. Ours goes into the nonce of every frame we send, exactly as the
// device's public address does.
const uint8_t OWN_MAC[6]  = { 0x5A, 0x57, 0x00, 0x00, 0x00, 0x01 };
const uint8_t PEER_MAC[6] = { 0x5A, 0x57, 0x00, 0x00, 0x00, 0x02 };

// Mirrors of the firmware's own numbers: a real board adverts every 1500ms
// (Mesh ADV_MS) and keeps a message in its scan response for thirty seconds
// (MeshTalk SEND_MS). Different numbers here would make the emulator's visits
// feel unlike a board's.
const uint32_t ADV_MS   = 1500;
const uint32_t SEND_MS  = 30000;
// Somebody reading a bubble and picking an answer. Long enough that the
// reply visibly follows rather than collides.
const uint32_t REPLY_MS = 3000;

// A phrase for SETUP, from the real word list.
const char* const SETUP_PHRASE = "GIBSON MOTHMAN PHREAK NESSIE ZEROCOOL";

// What it answers, by the canned line it heard. Something a person might
// actually send back, not a random draw -- the point is to watch a
// conversation, and a random one reads as a bug.
const uint8_t REPLY[] = {
    20, 0, 19, 22, 10, 13, 19, 8, 18, 16, 20, 10,
    19, 6, 0, 10, 20, 18, 18, 20, 19, 6, 7, 10,
};

// Four shades travel in two bits; names as squachy.cpp's SHADE_NAMES, which
// is not exported. The picker only needs them as labels.
const char* const SHADES[] = { "CYAN", "PINK", "GREEN", "PURPLE" };

// onAir, not "advertising": inside namespace Mesh that name is the function.
bool present = false, shares = true, autoReply = true, onAir = false;
bool macSet  = false, advDue = true;
// Defaults to the VOID EYE costume in pink: the most obviously not-you
// Squachy there is, so nobody wonders which one is the visitor.
SquachMesh::Peer look = { 4, 12, 1, false, "" };

uint32_t lastAdv = 0, ctr = 0, sendUntil = 0, heardGen = 0, replyAt = 0;
int      replyLine = -1;
uint8_t  frame[MeshMsg::CANNED_FRAME_LEN];
size_t   frameLen = 0;
char     said[40] = "", heard[40] = "";

uint8_t nickCount() {
    // nicknameAt() wraps rather than failing, so the table's length is where
    // it first comes back round to the start.
    uint8_t n = 1;
    while (n < 16 && strcmp(Squachy::nicknameAt(n), Squachy::nicknameAt(0)) != 0) n++;
    return n;
}

const char* peerName() {
    return (look.custom && look.name[0]) ? look.name : Squachy::nicknameAt(look.nick);
}

bool live(uint32_t now) { return frameLen && (int32_t)(sendUntil - now) > 0; }

// JSON string contents: the only two characters that could break it.
void jsonCopy(char* out, size_t cap, const char* s) {
    size_t o = 0;
    for (; *s && o + 2 < cap; s++) {
        if (*s == '"' || *s == '\\') out[o++] = '\\';
        out[o++] = *s;
    }
    out[o] = '\0';
}

bool say(int line, uint32_t now) {
    if (line < 0 || line >= MeshMsg::CANNED_N) {
        fprintf(stderr, "[meshsim] no line %d (0..%u)\n", line, (unsigned)(MeshMsg::CANNED_N - 1));
        return false;
    }
    const uint32_t c = ++ctr;
    size_t n = MeshMsg::sealCanned(MeshCrypto::impl(), PEER_MAC, c, (uint8_t)line,
                                   frame, sizeof frame);
    if (!n) {
        // No key on this side at all -- nobody has set a phrase, and the
        // stand-in cipher is one shared key. Still send a frame of the right
        // shape, so what gets exercised is the receiver refusing it.
        memcpy(frame, MeshMsg::MAGIC, 4);
        frame[4] = MeshMsg::VERSION;
        frame[5] = MeshMsg::KIND_CANNED;
        for (int i = 0; i < 4; i++) frame[6 + i] = (uint8_t)(c >> (8 * i));
        memset(frame + MeshMsg::HDR_LEN, 0xA5, sizeof frame - MeshMsg::HDR_LEN);
        n = sizeof frame;
    } else if (!shares) {
        // Another group's key, as far as we can tell: the tag will not verify.
        frame[n - 1] ^= 0x5A;
    }
    frameLen  = n;
    sendUntil = now + SEND_MS;
    snprintf(said, sizeof said, "%s", MeshMsg::CANNED[line]);
    fprintf(stderr, "[meshsim] %s sends \"%s\"%s\n", peerName(), said,
            shares ? "" : " -- under a different phrase");
    return true;
}

void hear(const uint8_t* out, size_t len, uint32_t now) {
    if (!shares) {
        snprintf(heard, sizeof heard, "(could not read it)");
        fprintf(stderr, "[meshsim] %s picked up a message but is in another group\n", peerName());
        return;
    }
    uint32_t c = 0;
    uint8_t  line = 0;
    const MeshMsg::Open r = MeshMsg::openCanned(MeshCrypto::impl(), OWN_MAC, out, len, c, line);
    if (r != MeshMsg::Open::OK) {
        fprintf(stderr, "[meshsim] %s could not open our frame (%d)\n", peerName(), (int)r);
        return;
    }
    snprintf(heard, sizeof heard, "%s", MeshMsg::CANNED[line]);
    fprintf(stderr, "[meshsim] %s heard \"%s\"\n", peerName(), heard);
    if (autoReply) {
        replyLine = line < sizeof REPLY ? REPLY[line] : 0;
        replyAt   = now + REPLY_MS;
    }
}

bool onOff(const char* arg, bool& v, const char* on, const char* off) {
    if (!strcasecmp(arg, on)  || !strcasecmp(arg, "on"))  { v = true;  return true; }
    if (!strcasecmp(arg, off) || !strcasecmp(arg, "off")) { v = false; return true; }
    return false;
}

bool pickIndex(const char* what, const char* arg, uint8_t n, uint8_t& out) {
    char* end = nullptr;
    const long v = strtol(arg, &end, 10);
    if (end == arg || v < 0 || v >= n) {
        fprintf(stderr, "[meshsim] %s wants 0..%u\n", what, (unsigned)(n - 1));
        return false;
    }
    out = (uint8_t)v;
    return true;
}

} // namespace

namespace Mesh {
bool advertising() { return onAir; }
void radioTick(uint32_t now) { MeshSim::tick(now); }
}

namespace MeshSim {

void tick(uint32_t now) {
    // What the device does once its stack is up (detection.cpp's radioTick).
    if (!macSet) { MeshTalk::setOwnMac(OWN_MAC); macSet = true; }
    onAir = Settings::meshTransmit();

    // It hears our scan response only while it is here and we are on the air,
    // and each message once -- `gen` moves exactly when the message does.
    if (present && onAir) {
        size_t   len = 0;
        uint32_t gen = 0;
        const uint8_t* out = MeshTalk::outgoing(now, len, gen);
        if (out && gen != heardGen) { heardGen = gen; hear(out, len, now); }
    }

    if (present && (advDue || now - lastAdv >= ADV_MS)) {
        advDue  = false;
        lastAdv = now;
        uint8_t buf[2 + SquachMesh::LEN_MAX];
        buf[0] = (uint8_t)(SquachMesh::COMPANY_ID & 0xFF);
        buf[1] = (uint8_t)(SquachMesh::COMPANY_ID >> 8);
        const size_t n = SquachMesh::encode(look, buf + 2);
        Mesh::onManufacturerData(buf, n + 2, PEER_MAC, now);
        // The scan response follows its advert, as it does over the air --
        // which is also what lets the frame pick up the name from it.
        if (live(now)) {
            uint8_t f[2 + sizeof frame];
            f[0] = buf[0]; f[1] = buf[1];
            memcpy(f + 2, frame, frameLen);
            Mesh::onManufacturerData(f, frameLen + 2, PEER_MAC, now);
        }
    }

    if (replyLine >= 0 && (int32_t)(now - replyAt) >= 0) {
        const int l = replyLine;
        replyLine = -1;
        if (present) say(l, now);
    }
}

bool command(const char* line) {
    while (*line == ' ') line++;
    char verb[16] = { 0 };
    size_t v = 0;
    while (*line && *line != ' ' && v < sizeof verb - 1) verb[v++] = (char)tolower((unsigned char)*line++);
    while (*line == ' ') line++;
    const char* arg = line;
    const uint32_t now = millis();

    if (!strcmp(verb, "on"))  { present = true; advDue = true;
                                fprintf(stderr, "[meshsim] %s is here\n", peerName()); return true; }
    if (!strcmp(verb, "off")) { present = false; replyLine = -1;
                                fprintf(stderr, "[meshsim] %s walked off; the visit ends when the firmware stops hearing it\n",
                                        peerName()); return true; }
    if (!strcmp(verb, "outfit")) return pickIndex("outfit", arg, Squachy::outfitCount(), look.outfit);
    if (!strcmp(verb, "shade"))  return pickIndex("shade", arg, 4, look.shade);
    if (!strcmp(verb, "nick"))   return pickIndex("nick", arg, nickCount(), look.nick);
    if (!strcmp(verb, "name")) {
        size_t i = 0;
        for (; *arg && i < SquachMesh::NAME_LEN; arg++) {
            const char c = (char)toupper((unsigned char)*arg);
            if (c >= ' ' && c <= '~' && c != '"' && c != '\\') look.name[i++] = c;
        }
        while (i && look.name[i - 1] == ' ') i--;
        look.name[i] = '\0';
        look.custom = i > 0;
        return true;
    }
    if (!strcmp(verb, "phrase")) {
        if (onOff(arg, shares, "same", "other")) return true;
        fprintf(stderr, "[meshsim] phrase same|other\n");
        return false;
    }
    if (!strcmp(verb, "reply")) {
        if (onOff(arg, autoReply, "on", "off")) return true;
        fprintf(stderr, "[meshsim] reply on|off\n");
        return false;
    }
    if (!strcmp(verb, "say")) {
        if (!present) { fprintf(stderr, "[meshsim] nobody is here to say it\n"); return false; }
        return say(atoi(arg), now);
    }
    if (!strcmp(verb, "setup")) {
        // Everything a person would do by hand through the warning, the menu
        // and the phrase screen -- skipped here because it is the emulator,
        // and it says so. Nothing on a device can call this.
        Settings::setMeshConsent(true);
        if (!Settings::meshDetect())   Settings::cycleMeshDetect();
        if (!Settings::meshTransmit()) Settings::cycleMeshTransmit();
        if (!Settings::messagesOn())   Settings::toggleMessages();
        if (!MeshTalk::havePhrase())   MeshTalk::setPhrase(SETUP_PHRASE);
        present = true; advDue = true;
        fprintf(stderr, "[meshsim] setup (emulator only): consent, DETECT, TRANSMIT, MESSAGES on; phrase %s\n",
                MeshTalk::phrase());
        return true;
    }
    if (!strcmp(verb, "status")) { fprintf(stderr, "[meshsim] %s\n", status()); return true; }
    if (!strcmp(verb, "help") || !verb[0]) {
        fprintf(stderr, "[meshsim] on|off, outfit N, shade N, nick N, name TEXT, phrase same|other, "
                        "reply on|off, say N, setup, status\n");
        return true;
    }
    fprintf(stderr, "[meshsim] unknown: %s (try help)\n", verb);
    return false;
}

const char* status() {
    static char buf[640];
    char name[32], s[96], h[96];
    jsonCopy(name, sizeof name, look.name);
    jsonCopy(s, sizeof s, said);
    jsonCopy(h, sizeof h, heard);
    const uint32_t now = millis();
    snprintf(buf, sizeof buf,
             "{\"present\":%d,\"visiting\":%d,\"shares\":%d,\"reply\":%d,"
             "\"nick\":%u,\"outfit\":%u,\"shade\":%u,\"name\":\"%s\","
             "\"sending\":%d,\"said\":\"%s\",\"heard\":\"%s\",\"replyIn\":%d,"
             "\"consent\":%d,\"detect\":%d,\"transmit\":%d,\"messages\":%d,"
             "\"phrase\":%d,\"ready\":%d}",
             present, Mesh::peer() && !memcmp(Mesh::peerMac(), PEER_MAC, 6), shares, autoReply,
             look.nick, look.outfit, look.shade, name,
             live(now), s, h, replyLine >= 0 ? (int)(replyAt - now) : -1,
             Settings::meshConsent(), Settings::meshDetect(), Settings::meshTransmit(),
             Settings::messagesOn(), MeshTalk::havePhrase(), MeshTalk::ready());
    return buf;
}

const char* catalog() {
    static char buf[2048];
    if (buf[0]) return buf;
    size_t o = 0;
    auto put = [&](const char* t) {
        char e[64];
        jsonCopy(e, sizeof e, t);
        o += snprintf(buf + o, sizeof buf - o, "\"%s\",", e);
    };
    auto list = [&](const char* key, uint8_t n, const char* (*at)(uint8_t)) {
        o += snprintf(buf + o, sizeof buf - o, "\"%s\":[", key);
        for (uint8_t i = 0; i < n; i++) put(at(i));
        if (buf[o - 1] == ',') o--;
        o += snprintf(buf + o, sizeof buf - o, "],");
    };
    o += snprintf(buf, sizeof buf, "{");
    list("nicks", nickCount(), Squachy::nicknameAt);
    list("outfits", Squachy::outfitCount(), Squachy::outfitNameAt);
    list("shades", 4, [](uint8_t i) { return SHADES[i]; });
    list("lines", MeshMsg::CANNED_N, [](uint8_t i) { return MeshMsg::CANNED[i]; });
    buf[o - 1] = '}';
    return buf;
}

} // namespace MeshSim
#endif // SQUACH_MESH
