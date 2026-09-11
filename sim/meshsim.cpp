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
// conversation, and a random one reads as a bug. A typed message gets
// "Thanks." (20).
const uint8_t REPLY[] = {
    20, 0, 19, 22, 10, 13, 19, 8, 18, 16, 20, 10,
    19, 6, 0, 10, 20, 18, 18, 20, 19, 6, 7, 10,
};
const uint8_t REPLY_TO_TEXT = 20;

// Four shades travel in two bits; names as squachy.cpp's SHADE_NAMES, which
// is not exported. The picker only needs them as labels.
const char* const SHADES[] = { "CYAN", "PINK", "GREEN", "PURPLE" };

// onAir, not "advertising": inside namespace Mesh that name is the function.
bool present = false, shares = true, autoReply = true, onAir = false;
bool macSet  = false, advDue = true;
// Defaults to the VOID EYE costume in pink: the most obviously not-you
// Squachy there is, so nobody wonders which one is the visitor.
SquachMesh::Peer look = { 4, 12, 1, false, "" };

uint32_t lastAdv = 0, ctr = 0, sendUntil = 0, heardGen = 0, replyAt = 0, heardMsg = 0;
// How many SquachWatches are here, counting this one. The rest only advertise:
// the firmware hosts one visitor at a time, so they only ever show up as the
// "+N" beside him.
uint8_t  squad = 1;
int      replyLine = -1;
// What it is sending: one frame for a canned line, up to three for a typed
// one, fed one per advert in turn -- the way a board's scan response rotates.
uint8_t  frames[MeshMsg::TEXT_PARTS_MAX][MeshMsg::FRAME_MAX];
size_t   frameLen[MeshMsg::TEXT_PARTS_MAX] = { 0 };
uint8_t  frameN = 0, frameAt = 0;
char     said[MeshMsg::TEXT_MAX + 1] = "", heard[MeshMsg::TEXT_MAX + 1] = "";
MeshMsg::Assembly asmb;

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

bool live(uint32_t now) { return frameN && (int32_t)(sendUntil - now) > 0; }

// JSON string contents: the only two characters that could break it.
void jsonCopy(char* out, size_t cap, const char* s) {
    size_t o = 0;
    for (; *s && o + 2 < cap; s++) {
        if (*s == '"' || *s == '\\') out[o++] = '\\';
        out[o++] = *s;
    }
    out[o] = '\0';
}

// What happens to every frame it sends, typed or canned: a group it is not in
// means a tag that will not verify; no key at all -- nobody has set a phrase,
// and the stand-in cipher is one shared key -- means a frame of the right
// shape that the receiver has to refuse.
void broadcast(uint8_t n, uint32_t now) {
    if (n == 0) {
        memcpy(frames[0], MeshMsg::MAGIC, sizeof MeshMsg::MAGIC);
        frames[0][2] = (uint8_t)((MeshMsg::VERSION << 4) | MeshMsg::KIND_CANNED);
        for (int i = 0; i < 3; i++) frames[0][3 + i] = (uint8_t)(ctr >> (8 * i));
        memset(frames[0] + MeshMsg::HDR_LEN, 0xA5, MeshMsg::CANNED_FRAME_LEN - MeshMsg::HDR_LEN);
        frameLen[0] = MeshMsg::CANNED_FRAME_LEN;
        n = 1;
    } else if (!shares) {
        for (uint8_t i = 0; i < n; i++) frames[i][frameLen[i] - 1] ^= 0x5A;
    }
    frameN    = n;
    frameAt   = 0;
    sendUntil = now + SEND_MS;
    fprintf(stderr, "[meshsim] %s sends \"%s\"%s\n", peerName(), said,
            shares ? "" : " -- under a different phrase");
}

bool say(int line, uint32_t now) {
    if (line < 0 || line >= MeshMsg::CANNED_N) {
        fprintf(stderr, "[meshsim] no line %d (0..%u)\n", line, (unsigned)(MeshMsg::CANNED_N - 1));
        return false;
    }
    const uint32_t c = ++ctr;
    frameLen[0] = MeshMsg::sealCanned(MeshCrypto::impl(), PEER_MAC, c, (uint8_t)line,
                                      frames[0], sizeof frames[0]);
    snprintf(said, sizeof said, "%s", MeshMsg::CANNED[line]);
    broadcast(frameLen[0] ? 1 : 0, now);
    return true;
}

bool sayText(const char* text, uint32_t now) {
    const uint8_t total = MeshMsg::textParts(text);
    if (!total) {
        fprintf(stderr, "[meshsim] not a message: up to %u of \"%s\"\n",
                (unsigned)MeshMsg::TEXT_MAX, MeshMsg::TEXT_CHARSET);
        return false;
    }
    const uint32_t base = ctr + 1;
    ctr += total;
    uint8_t n = total;
    for (uint8_t p = 0; p < total; p++) {
        frameLen[p] = MeshMsg::sealTextPart(MeshCrypto::impl(), PEER_MAC, base + p, text,
                                            p, total, frames[p], sizeof frames[p]);
        if (!frameLen[p]) n = 0;
    }
    snprintf(said, sizeof said, "%s", text);
    broadcast(n, now);
    return true;
}

bool emote(int e, int arg, uint32_t now) {
    if (e < 0 || e >= (int)MeshMsg::Emote::COUNT) {
        fprintf(stderr, "[meshsim] no emote %d (0..%u)\n", e, (unsigned)MeshMsg::Emote::COUNT - 1);
        return false;
    }
    // Rock-paper-scissors needs both throws; anything else sends no setup.
    if (arg < 0) arg = (e == (int)MeshMsg::Emote::RPS) ? (int)(millis() % 9) : 0;
    const uint32_t c = ++ctr;
    frameLen[0] = MeshMsg::sealEmote(MeshCrypto::impl(), PEER_MAC, c,
                                     MeshMsg::emoteByte((MeshMsg::Emote)e, (uint8_t)arg),
                                     frames[0], sizeof frames[0]);
    snprintf(said, sizeof said, "(emote %d, setup %d)", e, arg);
    broadcast(frameLen[0] ? 1 : 0, now);
    return true;
}

void replyLater(uint8_t line, uint32_t now) {
    if (!autoReply) return;
    replyLine = line;
    replyAt   = now + REPLY_MS;
}

void hear(const uint8_t* out, size_t len, uint32_t gen, uint32_t now) {
    if (!shares) {
        // Once per message, not once per part.
        if ((gen >> 2) != heardMsg) {
            heardMsg = gen >> 2;
            snprintf(heard, sizeof heard, "(could not read it)");
            fprintf(stderr, "[meshsim] %s picked up a message but is in another group\n", peerName());
        }
        return;
    }
    uint32_t c = 0;
    uint8_t kind = 0;
    if (!MeshMsg::parseHeader(out, len, c, kind)) return;
    if (kind == MeshMsg::KIND_CANNED) {
        uint8_t line = 0;
        if (MeshMsg::openCanned(MeshCrypto::impl(), OWN_MAC, out, len, c, line) != MeshMsg::Open::OK) {
            fprintf(stderr, "[meshsim] %s could not open our frame\n", peerName());
            return;
        }
        snprintf(heard, sizeof heard, "%s", MeshMsg::CANNED[line]);
        fprintf(stderr, "[meshsim] %s heard \"%s\"\n", peerName(), heard);
        replyLater(line < sizeof REPLY ? REPLY[line] : 0, now);
        return;
    }
    if (kind == MeshMsg::KIND_TEXT) {
        uint8_t part = 0, total = 0;
        char chars[MeshMsg::TEXT_PART_CHARS + 1];
        if (MeshMsg::openTextPart(MeshCrypto::impl(), OWN_MAC, out, len, c, part, total, chars)
                != MeshMsg::Open::OK) {
            fprintf(stderr, "[meshsim] %s could not open part of our message\n", peerName());
            return;
        }
        char body[MeshMsg::TEXT_MAX + 1];
        uint32_t base = 0;
        if (!asmb.add(OWN_MAC, c, part, total, chars, body, base)) return;   // more to come
        snprintf(heard, sizeof heard, "%s", body);
        fprintf(stderr, "[meshsim] %s heard \"%s\" (%u parts)\n", peerName(), heard, (unsigned)total);
        replyLater(REPLY_TO_TEXT, now);
        return;
    }
    if (kind == MeshMsg::KIND_EMOTE) {
        uint8_t e = 0;
        if (MeshMsg::openEmote(MeshCrypto::impl(), OWN_MAC, out, len, c, e) != MeshMsg::Open::OK) {
            fprintf(stderr, "[meshsim] %s could not open our emote\n", peerName());
            return;
        }
        snprintf(heard, sizeof heard, "(emote %u, setup %u)", (unsigned)(e >> 4), (unsigned)(e & 0x0F));
        fprintf(stderr, "[meshsim] %s saw emote %u (setup %u)\n", peerName(),
                (unsigned)(e >> 4), (unsigned)(e & 0x0F));
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
    // and each frame once -- `gen` moves exactly when the frame does, part by
    // part for a typed message.
    if (present && onAir) {
        size_t   len = 0;
        uint32_t gen = 0;
        const uint8_t* out = MeshTalk::outgoing(now, len, gen);
        if (out && gen != heardGen) { heardGen = gen; hear(out, len, gen, now); }
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
        // which is also what lets the frame pick up the name from it. A typed
        // message's parts take turns, one per advert.
        if (live(now)) {
            const uint8_t p = frameAt++ % frameN;
            uint8_t f[2 + MeshMsg::FRAME_MAX];
            f[0] = buf[0]; f[1] = buf[1];
            memcpy(f + 2, frames[p], frameLen[p]);
            Mesh::onManufacturerData(f, frameLen[p] + 2, PEER_MAC, now);
        }
        // The rest of the squad, after: on a board NimBLE hands an advert and
        // its scan response over in one callback, so nobody else's advert can
        // land between them and take the name the frame borrows.
        for (uint8_t i = 1; i < squad; i++) {
            SquachMesh::Peer o = look;
            o.custom = false;
            o.name[0] = '\0';
            o.nick = (uint8_t)((look.nick + i) % nickCount());
            uint8_t ob[2 + SquachMesh::LEN_MAX];
            ob[0] = buf[0]; ob[1] = buf[1];
            const size_t on = SquachMesh::encode(o, ob + 2);
            uint8_t mac[6];
            memcpy(mac, PEER_MAC, 6);
            mac[5] = (uint8_t)(0x10 + i);
            Mesh::onManufacturerData(ob, on + 2, mac, now);
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
    if (!strcmp(verb, "text")) {
        if (!present) { fprintf(stderr, "[meshsim] nobody is here to type it\n"); return false; }
        char t[MeshMsg::TEXT_MAX + 2];
        size_t i = 0;
        for (; arg[i] && i < sizeof t - 1; i++) t[i] = (char)toupper((unsigned char)arg[i]);
        t[i] = '\0';
        while (i && t[i - 1] == ' ') t[--i] = '\0';
        return sayText(t, now);
    }
    if (!strcmp(verb, "emote")) {
        if (!present) { fprintf(stderr, "[meshsim] nobody is here to do it\n"); return false; }
        char* end = nullptr;
        const long e = strtol(arg, &end, 10);
        if (end == arg) { fprintf(stderr, "[meshsim] emote N [SETUP]\n"); return false; }
        const long a = (end && *end) ? strtol(end, nullptr, 10) : -1;
        return emote((int)e, (int)a, now);
    }
    if (!strcmp(verb, "squad")) {
        uint8_t n = 0;
        if (!pickIndex("squad", arg, 6, n) || n == 0) {
            fprintf(stderr, "[meshsim] squad wants 1..5\n");
            return false;
        }
        squad = n;
        fprintf(stderr, "[meshsim] %u SquachWatch%s here\n", (unsigned)n, n == 1 ? "" : "es");
        return true;
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
                        "reply on|off, say N, text MESSAGE, emote N [SETUP], squad N, setup, status\n");
        return true;
    }
    fprintf(stderr, "[meshsim] unknown: %s (try help)\n", verb);
    return false;
}

const char* status() {
    static char buf[768];
    char name[32], s[128], h[128];
    jsonCopy(name, sizeof name, look.name);
    jsonCopy(s, sizeof s, said);
    jsonCopy(h, sizeof h, heard);
    const uint32_t now = millis();
    snprintf(buf, sizeof buf,
             "{\"present\":%d,\"visiting\":%d,\"shares\":%d,\"reply\":%d,"
             "\"nick\":%u,\"outfit\":%u,\"shade\":%u,\"name\":\"%s\","
             "\"sending\":%d,\"said\":\"%s\",\"heard\":\"%s\",\"replyIn\":%d,"
             "\"consent\":%d,\"detect\":%d,\"transmit\":%d,\"messages\":%d,"
             "\"phrase\":%d,\"ready\":%d,\"squad\":%u}",
             present, Mesh::peer() && !memcmp(Mesh::peerMac(), PEER_MAC, 6), shares, autoReply,
             look.nick, look.outfit, look.shade, name,
             live(now), s, h, replyLine >= 0 ? (int)(replyAt - now) : -1,
             Settings::meshConsent(), Settings::meshDetect(), Settings::meshTransmit(),
             Settings::messagesOn(), MeshTalk::havePhrase(), MeshTalk::ready(), (unsigned)squad);
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
    o += snprintf(buf + o, sizeof buf - o, "\"textMax\":%u,", (unsigned)MeshMsg::TEXT_MAX);
    buf[o - 1] = '}';
    return buf;
}

} // namespace MeshSim
#endif // SQUACH_MESH
