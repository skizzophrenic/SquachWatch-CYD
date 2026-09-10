// SquachWatch-CYD — SquachMesh, the half that does not touch a radio.
//
// Who is visiting, what their advert said, when they were last heard, and
// where a message frame goes: all of it is bytes in and state out, so it lives
// here where the emulator can compile it too. The radio half -- NimBLE
// advertising and our own address -- is Mesh::radioTick() in detection.cpp on
// the device and in sim/meshsim.cpp in the emulator.
//
// That split is the point. The emulator's virtual peer feeds real advert bytes
// into the same onManufacturerData() a real scan callback calls, so a visit in
// the emulator goes through the real decoder, the real one-visitor rule and the
// real staleness timeout rather than a copy of them that could drift.
#include "detection.h"

#if SQUACH_MESH
#include "squachmesh.h"
#include "squachy.h"
#include "settings.h"
#include "meshmsg.h"
#include "meshtalk.h"
#include <string.h>

namespace Mesh {

// One visitor at a time -- decided deliberately, and for screen space rather
// than memory: 320x240 already holds Squachy, a pet, twelve counters and
// three buttons. A second arrival while somebody is here is dropped rather
// than queued, because a visit is a moment and not a message that has to be
// delivered.
static SquachMesh::Peer s_peer{};
static uint8_t          s_peerMac[6] = {0};
static uint32_t         s_peerSeen   = 0;
static bool             s_havePeer   = false;
// A peer's name from its advert, kept for the message frame that arrives in
// the same scan callback. Written and read only in the BLE task.
static uint8_t          s_nameMac[6] = { 0 };
static char             s_name[13]   = { 0 };

// How long a peer survives without being heard from again. Adverts go out
// every 1500ms, so this is roughly eight missed ones -- long enough that a
// pocket or a passing wall does not end a visit, short enough that somebody
// who actually left stops standing on your screen.
static const uint32_t PEER_STALE_MS = 12000;

const SquachMesh::Peer* peer() {
    return s_havePeer ? &s_peer : nullptr;
}
const uint8_t* peerMac() { return s_peerMac; }

// What we look like, read fresh each time rather than cached: the outfit and
// the name can both change while this is running, and a peer drawing a stale
// version of us is a bug nobody would think to look for.
size_t buildSelf(uint8_t* out) {
    SquachMesh::Peer me{};
    me.nick   = Squachy::nicknameIndex();
    me.outfit = Squachy::outfitIndex();
    me.shade  = Squachy::shadesIndex();
    const char* cn = Squachy::customName();
    me.custom = (cn && cn[0]);
    me.name[0] = '\0';
    if (me.custom) {
        size_t i = 0;
        for (; i < SquachMesh::NAME_LEN && cn[i]; i++) me.name[i] = cn[i];
        me.name[i] = '\0';
    }
    return SquachMesh::encode(me, out);
}

void begin() { s_havePeer = false; }

bool onManufacturerData(const uint8_t* d, size_t len, const uint8_t* mac, uint32_t now) {
    // Refused at the door when detection is off, so a peer cannot be latched
    // between the setting changing and the next tick.
    if (!Settings::meshDetect()) return false;
    // Company ID first, little-endian, then the payload.
    if (len < 2 + SquachMesh::LEN_INDEXED) return false;
    const uint16_t cid = (uint16_t)(d[0] | ((uint16_t)d[1] << 8));
    if (cid != SquachMesh::COMPANY_ID) return false;

    // A message frame rather than an advert. Queued raw for the loop task:
    // this runs in the BLE host task and the cipher lives on the loop, so no
    // key is ever used from two tasks at once. Consumed when messages are on,
    // so a message can never reach the signature tables either.
    if (MeshMsg::isFrame(d + 2, len - 2)) {
        if (!Settings::messagesOn()) return false;
        MeshTalk::onFrame(mac, d + 2, len - 2,
                          memcmp(mac, s_nameMac, 6) == 0 ? s_name : "");
        return true;
    }

    SquachMesh::Peer p;
    if (!SquachMesh::decode(d + 2, len - 2, p)) return false;

    // Remembered for a message frame later in this same callback: the name
    // goes on the message, and the advert is the only place it travels.
    {
        memcpy(s_nameMac, mac, 6);
        const char* nm = (p.custom && p.name[0]) ? p.name : Squachy::nicknameAt(p.nick);
        size_t i = 0;
        for (; i < sizeof s_name - 1 && nm[i]; i++) s_name[i] = nm[i];
        s_name[i] = '\0';
    }

    // Ours. Keep the one we already have unless this IS the one we already
    // have -- a second SquachWatch arriving mid-visit does not get to shove
    // the first one off the screen.
    if (s_havePeer && memcmp(mac, s_peerMac, 6) != 0) return true;

    s_peer = p;
    memcpy(s_peerMac, mac, 6);
    s_peerSeen = now;
    s_havePeer = true;
    return true;
}

void tick(uint32_t now) {
    radioTick(now);

    // Detecting is its own switch now, and off means no visitor at all --
    // not "advertise less". A peer already on screen is dropped rather than
    // frozen there.
    if (!Settings::meshDetect()) { s_havePeer = false; return; }

    if (s_havePeer && (now - s_peerSeen) > PEER_STALE_MS) s_havePeer = false;
}

} // namespace Mesh
#endif // SQUACH_MESH
