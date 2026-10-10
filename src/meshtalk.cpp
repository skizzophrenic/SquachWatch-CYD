// SquachWatch-CYD — SquachMesh messages at runtime. See include/meshtalk.h.
#include "meshtalk.h"
#include "gnss.h"

#if SQUACH_MESH
#include "meshcrypto.h"
#include "mesh_airtime.h"
#include "mesh_primary_transport.h"
#include "settings.h"
#include "clock.h"
#include "ota_core.h"
#include "detection.h"      // Mesh::peerLook, for the roster
#include <Arduino.h>
#include <Preferences.h>
#include <esp_system.h>
#include <string.h>

namespace MeshTalk {
namespace {

Preferences s_prefs;
bool     s_selfTestOk = false;
bool     s_havePhrase = false;
char     s_phrase[MeshMsg::PHRASE_TEXT_MAX] = { 0 };
uint32_t s_deriveMs   = 0;

uint8_t  s_ownMac[6] = { 0 };
bool     s_macSet    = false;

MeshMsg::Counter  s_ctr;
MeshMsg::Replay   s_replay;
MeshMsg::Assembly s_asm;

// What is being broadcast right now, and until when. Thirty seconds is about
// twenty adverts: long enough that a pocket or a wall does not cost the
// message, short enough that the device stops announcing it has something to
// say.
constexpr uint32_t SEND_MS = 30000;
// How long each part holds the scan response. The message advertiser uses
// a much shorter interval than this dwell, giving each frame several on-air
// opportunities after every NimBLE advertising restart. See mesh_airtime.h.
constexpr uint32_t PART_MS = MeshAirtime::FRAME_DWELL_MS;
constexpr uint32_t EMOTE_MS = 9000;
// A nudge and its WiFi parts: up to seven frames taking turns, so each is
// on the air far less often than a message's three. A full minute.
constexpr uint32_t NUDGE_MS = 60000;
uint8_t  s_out[MeshMsg::OUT_PARTS_MAX][MeshMsg::FRAME_MAX];
uint8_t  s_outLen[MeshMsg::OUT_PARTS_MAX] = { 0 };
uint8_t  s_outN     = 0;
uint32_t s_outStart = 0;
uint32_t s_outUntil = 0;
uint32_t s_outGen   = 0;
bool     s_outEmote = false;   // what is on the air is an emote, not a message

Message  s_inbox = {};
Message  s_hist[INBOX_N];      // a ring, newest at s_histHead - 1
uint8_t  s_histN = 0, s_histHead = 0;
EmoteIn  s_emote = {};
bool     s_emoteHave = false;
NudgeIn   s_nudge = {};
bool      s_nudgeHave = false;
UpdatedIn s_updated = {};
bool      s_updatedHave = false;
MeshMsg::WifiAssembly s_wifiAsm;

// Who has been heard holding our phrase, and when. Eight is the crowd cap.
constexpr uint8_t  SQUAD_N        = 8;
constexpr uint32_t SQUAD_FRESH_MS = 6 * 60000;   // three missed hellos and you are a stranger again
constexpr uint32_t HELLO_EVERY_MS = 2 * 60000;
constexpr uint32_t HELLO_MS       = 4000;        // about three adverts
struct SquadSeen { uint8_t mac[6]; uint32_t at; bool live; };
SquadSeen s_squad[SQUAD_N] = {};
uint32_t  s_lastHello = 0;

// Read receipts. Ours to send, once, for the message just opened; theirs
// to take, for the last one we sent.
bool     s_readDue   = false;
uint32_t s_readCtr   = 0;
uint32_t s_sentCtr   = 0xFFFFFFFFu;   // the last message this board sent
bool     s_readHave  = false;
char     s_readBy[13] = "";

// The heads-up. One waiting to go out -- the newest catch wins, and news
// older than a minute and a half is not sent late -- and the last few
// devices told, so one that drifts in and out is told once.
constexpr uint32_t HEADSUP_GAP_MS    = 60000;
constexpr uint32_t HEADSUP_STALE_MS  = 90000;
constexpr uint32_t HEADSUP_REPEAT_MS = 10 * 60000;
constexpr uint8_t  HEADSUP_TOLD_N    = 4;
struct HeadsUpOut { uint8_t type; int8_t rssi; uint8_t tail[3]; uint32_t at; bool due; };
struct HeadsUpTold { uint8_t tail[3]; uint32_t at; bool live; };
HeadsUpOut  s_huOut = {};
HeadsUpTold s_huTold[HEADSUP_TOLD_N] = {};
uint8_t     s_huToldNext = 0;
uint32_t    s_huSentAt = 0;
HeadsUpIn   s_huIn = {};
bool        s_huHave = false;

// The roster (see meshtalk.h), and its shape on disk: 25 bytes a member,
// packed by hand so a different compiler's padding cannot scramble it.
Member  s_roster[ROSTER_N] = {};
uint8_t s_rosterN = 0;
constexpr size_t MEMBER_BYTES = 6 + 4 + 13 + 2;

void rosterSave() {
    uint8_t b[ROSTER_N * MEMBER_BYTES];
    size_t  n = 0;
    for (uint8_t i = 0; i < s_rosterN; i++) {
        const Member& m = s_roster[i];
        memcpy(b + n, m.mac, 6); n += 6;
        // The fourth byte was the custom flag alone; the aura took its bit 1.
        b[n++] = m.look.nick; b[n++] = m.look.outfit; b[n++] = m.look.shade;
        b[n++] = (uint8_t)((m.look.custom ? 1 : 0) | (m.look.aura ? 2 : 0));
        memcpy(b + n, m.look.name, 13); n += 13;
        b[n++] = (uint8_t)(m.met & 0xFF); b[n++] = (uint8_t)(m.met >> 8);
    }
    if (n) s_prefs.putBytes("roster", b, n);
    else   s_prefs.remove("roster");
}

void rosterLoad() {
    uint8_t b[ROSTER_N * MEMBER_BYTES];
    const size_t got = s_prefs.getBytes("roster", b, sizeof b);
    s_rosterN = 0;
    for (size_t n = 0; n + MEMBER_BYTES <= got && s_rosterN < ROSTER_N; n += MEMBER_BYTES) {
        Member& m = s_roster[s_rosterN++];
        memcpy(m.mac, b + n, 6);
        m.look.nick = b[n + 6]; m.look.outfit = b[n + 7]; m.look.shade = b[n + 8];
        m.look.custom = (b[n + 9] & 1) != 0; m.look.aura = (b[n + 9] & 2) != 0;
        memcpy(m.look.name, b + n + 10, 13); m.look.name[12] = 0;
        m.met = (uint16_t)(b[n + 23] | (b[n + 24] << 8));
    }
}

// A board just proved it holds the phrase. New to us, or back after being
// away: that is one more meeting. Either way its look is refreshed from
// its latest advert, when we have heard one.
void rosterNote(const uint8_t mac[6], bool newMeeting) {
    int slot = -1;
    for (uint8_t i = 0; i < s_rosterN; i++)
        if (memcmp(s_roster[i].mac, mac, 6) == 0) { slot = i; break; }
    bool changed = false;
    if (slot < 0) {
        if (s_rosterN < ROSTER_N) slot = s_rosterN++;
        else {
            slot = 0;
            for (uint8_t i = 1; i < s_rosterN; i++) if (s_roster[i].met < s_roster[slot].met) slot = i;
        }
        Member& m = s_roster[slot];
        m = Member{};
        memcpy(m.mac, mac, 6);
        newMeeting = true;
        changed = true;
    }
    Member& m = s_roster[slot];
    SquachMesh::Peer look;
    if (Mesh::peerLook(mac, look) && memcmp(&look, &m.look, sizeof look) != 0) { m.look = look; changed = true; }
    if (newMeeting && m.met < 0xFFFF) { m.met++; changed = true; }
    if (changed) rosterSave();
}

void squadNote(const uint8_t mac[6], uint32_t now) {
    uint8_t slot = 0;
    bool found = false;
    for (uint8_t i = 0; i < SQUAD_N; i++)
        if (s_squad[i].live && memcmp(s_squad[i].mac, mac, 6) == 0) { slot = i; found = true; break; }
    const bool fresh = found && now - s_squad[slot].at < SQUAD_FRESH_MS;
    if (!found) {
        for (uint8_t i = 0; i < SQUAD_N; i++) {
            if (!s_squad[i].live) { slot = i; break; }
            if ((int32_t)(s_squad[i].at - s_squad[slot].at) < 0) slot = i;   // the stalest
        }
        memcpy(s_squad[slot].mac, mac, 6);
        s_squad[slot].live = true;
    }
    s_squad[slot].at = now;
    rosterNote(mac, !fresh);
}

// ---- the invite ---------------------------------------------------------
constexpr uint32_t INVITE_OFFER_MS = 60000;   // the public key's time on the air
constexpr uint32_t INVITE_KEY_MS   = 30000;   // the phrase's
constexpr uint32_t INVITE_WAIT_MS  = 90000;   // how long a side waits for the other
InviteState s_invState = InviteState::IDLE;
uint32_t    s_invSince = 0;
bool        s_invInviter = false;
uint8_t     s_invPeerMac[6] = { 0 };
bool        s_invConfirmed  = false;   // DONE: their hello came back
char        s_invPeerName[13] = "";
uint8_t     s_invPriv[MeshCrypto::DH_LEN] = { 0 };
uint8_t     s_invPub[MeshCrypto::DH_LEN]  = { 0 };
uint8_t     s_invPeerPub[MeshCrypto::DH_LEN] = { 0 };
uint8_t     s_invKey[MeshMsg::KEY_LEN] = { 0 };
uint16_t    s_invCode = 0;
const char* s_invWhy  = "";
MeshMsg::InviteAssembly s_invAsm;
char        s_invPhrase[MeshMsg::PHRASE_TEXT_MAX + 1] = "";   // invitee: held until MATCHES
bool        s_invPhraseIn = false;
bool      s_ackPending = false;    // an UPDATED reply owed after this boot

// The replay table, to flash, after each message it records. Rare -- a few a
// day is a lot -- so the wear is nothing, and it is what stops a frame somebody
// recorded being shown again after a reboot.
void saveReplay() {
    uint8_t b[MeshMsg::Replay::BYTES];
    s_replay.save(b);
    s_prefs.putBytes("replay", b, sizeof b);
}

// BLE task -> loop task. Single producer, single consumer, so two indices and
// acquire/release ordering are the whole synchronisation -- no lock, and
// nothing the BLE task can block on.
// `mac` is who it came from, for the visit and the roster; `nmac` is what
// its nonce and replay entry are keyed by: the same bytes for a board, a
// phone's own identity for a phone (meshtalk.h, onFrame).
struct Slot { uint8_t mac[6]; uint8_t nmac[6]; uint8_t len; uint8_t data[MeshMsg::FRAME_MAX]; char name[13]; };
constexpr uint32_t RING = 4;
Slot     s_ring[RING];
uint32_t s_head = 0;       // written only by the BLE task
uint32_t s_tail = 0;       // written only by the loop task
// The producer's own memory of what it last queued. A sender repeats its frame
// every advert, so without this the ring would see the same frame every 1.5
// seconds for half a minute.
uint8_t  s_lastQMac[6] = { 0 };
uint8_t  s_lastQ[MeshMsg::FRAME_MAX] = { 0 };
uint8_t  s_lastQLen    = 0;

static uint32_t s_rxSeen = 0;         // BLE producer
static uint32_t s_rxQueued = 0;       // BLE producer
static uint32_t s_rxQueueFull = 0;    // BLE producer
static uint32_t s_rxReplay = 0;       // loop consumer
static uint32_t s_rxInviteParts = 0;  // loop consumer, hash-validated
static uint32_t s_rxInviteComplete = 0;
static uint32_t s_rxInviteRejected = 0;
static uint8_t s_rxInviteMask = 0;
static uint8_t s_rxLastRole = 0xff;
static uint8_t s_rxLastReject = 0;

static void inviteRxReject(uint8_t code, const char* why) {
    ++s_rxInviteRejected;
    s_rxLastReject = code;
    if (s_rxInviteRejected <= 8 || (s_rxInviteRejected % 32) == 0)
        Serial.printf("[invite-rx] reject %s (%u), total %lu, mask %x/15\n",
                      why, (unsigned)code, (unsigned long)s_rxInviteRejected,
                      (unsigned)s_rxInviteMask);
}

void arrived(const Slot& s, uint32_t now) {
    s_inbox.have   = true;
    s_inbox.unread = true;
    s_inbox.at     = now;
    memcpy(s_inbox.mac, s.mac, 6);
    const char* from = s.name[0] ? s.name : "SOMEONE";
    size_t i = 0;
    for (; i < sizeof s_inbox.from - 1 && from[i]; i++) s_inbox.from[i] = from[i];
    s_inbox.from[i] = '\0';
    Serial.printf("[meshtalk] message from %s: %s\n", s_inbox.from, lineText(s_inbox));
    s_hist[s_histHead] = s_inbox;
    s_histHead = (uint8_t)((s_histHead + 1) % INBOX_N);
    if (s_histN < INBOX_N) s_histN++;
}

void inviteKeyRestore();
void inviteFail(const char* why, uint32_t now);
void inviteTo(InviteState st, uint32_t now) { s_invState = st; s_invSince = now; }

// Just joined: our key frames can come off the air, and the first hello
// under the new phrase goes out on the next tick rather than in two
// minutes. It is what tells the inviter's board the phrase landed.
void joinedHello() {
    s_outN = 0; s_outGen++;
    s_lastHello = 0;
}

void deliverInvite(const Slot& s, uint32_t now, uint32_t ctr, uint8_t kind) {
    if (!s_selfTestOk || !s_macSet) return;
    uint8_t part = 0, bytes[MeshMsg::INVITE_PART_BYTES];
    if (kind == MeshMsg::KIND_INVITE_PUB) {
        if (MeshMsg::openInvitePub(MeshCrypto::sha256, s.data, s.len, ctr, part, bytes) != MeshMsg::Open::OK) {
            inviteRxReject(1, "public-key frame hash");
            return;
        }
        const bool complete = s_invAsm.add(s.mac, kind, ctr, part, bytes);
        ++s_rxInviteParts;
        s_rxInviteMask = s_invAsm.have;
        // At most 4 logs per invitation rotation, not once per beacon.
        // The radio reports many copies but onFrame() coalesces adjacent ones.
        static uint8_t loggedMask = 0;
        if (s_rxInviteMask != loggedMask || complete) {
            loggedMask = s_rxInviteMask;
            Serial.printf("[invite-rx] public key part %u/4, mask %x/15, state %u\n",
                          (unsigned)part + 1, (unsigned)s_rxInviteMask, (unsigned)s_invState);
        }
        if (!complete) return;
        uint8_t target[6], role = 0, pub[MeshMsg::INVITE_PUB_LEN];
        const bool ok = MeshMsg::invitePubUnblob(s_invAsm.bytes, target, role, pub);
        s_invAsm.clear();
        if (!ok) { inviteRxReject(2, "bad public-key blob"); return; }
        s_rxLastRole = role;
        if (memcmp(target, s_ownMac, 6) != 0) {
            inviteRxReject(3, "offer/answer target is not our BLE address");
            return;
        }
        ++s_rxInviteComplete;
        Serial.printf("[invite-rx] complete four-part %s for this board\n", role ? "ANSWER" : "OFFER");
        s_replay.record(s.nmac, ctr);
        saveReplay();
        if (role == 0) {
            // An offer. Only while nothing else is going on: a second offer
            // mid-invite is ignored, not swapped in.
            if (s_invState != InviteState::IDLE && s_invState != InviteState::ASKED) {
                inviteRxReject(4, "offer received in incompatible invite state");
                return;
            }
            memcpy(s_invPeerMac, s.mac, 6);
            memcpy(s_invPeerPub, pub, sizeof s_invPeerPub);
            snprintf(s_invPeerName, sizeof s_invPeerName, "%s", s.name[0] ? s.name : "SOMEONE");
            s_invInviter = false;
            inviteTo(InviteState::ASKED, now);
            Serial.printf("[invite] %s offers to add us to their squad\n", s_invPeerName);
        } else {
            // An answer, to our offer, from the board we offered to.
            if (s_invState != InviteState::OFFERING || !s_invInviter) {
                inviteRxReject(4, "answer received outside OFFERING state");
                return;
            }
            if (memcmp(s.mac, s_invPeerMac, 6) != 0) {
                inviteRxReject(5, "answer sender not invited peer");
                return;
            }
            memcpy(s_invPeerPub, pub, sizeof s_invPeerPub);
            uint8_t shared[MeshCrypto::DH_LEN];
            if (!MeshCrypto::dhShared(s_invPriv, s_invPeerPub, shared)) {
                inviteRxReject(6, "DH agreement failed");
                inviteFail("Bad key from their board", now);
                return;
            }
            MeshCrypto::dhSessionKey(shared, s_invKey);
            memset(shared, 0, sizeof shared);
            s_invCode = MeshCrypto::dhCode(s_invPub, s_invPeerPub);
            s_outN = 0; s_outGen++;      // our offer can come off the air
            inviteTo(InviteState::CODE, now);
            Serial.printf("[invite] %s answered; code %04u\n", s_invPeerName, (unsigned)s_invCode);
        }
        return;
    }
    if (kind == MeshMsg::KIND_INVITE_KEY) {
        // Only the invitee, only from the inviter, only once the keys agree.
        if (s_invInviter || (s_invState != InviteState::CODE && s_invState != InviteState::WAITING)) return;
        if (memcmp(s.mac, s_invPeerMac, 6) != 0) return;
        // The session key is what opens these. Swapped in for the call and
        // the squad key (if this board had one) put back after.
        if (!MeshCrypto::impl().setKey(s_invKey)) return;
        const MeshMsg::Open r = MeshMsg::openInviteKey(MeshCrypto::impl(), s.nmac, s.data, s.len, ctr, part, bytes);
        inviteKeyRestore();
        if (r != MeshMsg::Open::OK) return;
        if (!s_invAsm.add(s.mac, kind, ctr, part, bytes)) return;
        const bool ok = MeshMsg::inviteKeyUnblob(s_invAsm.bytes, s_invPhrase);
        s_invAsm.clear();
        if (!ok) { inviteFail("Garbled phrase", now); return; }
        s_replay.record(s.nmac, ctr);
        saveReplay();
        s_invPhraseIn = true;
        Serial.println("[invite] the phrase is in");
        // Taken now if MATCHES was already pressed, else held for it.
        if (s_invState == InviteState::WAITING) {
            if (setPhrase(s_invPhrase)) { inviteTo(InviteState::JOINED, now); joinedHello(); }
            else inviteFail("Could not take the phrase", now);
            memset(s_invPhrase, 0, sizeof s_invPhrase);
            s_invPhraseIn = false;
        }
    }
}

void deliver(const Slot& s, uint32_t now) {
    uint32_t ctr = 0;
    uint8_t kind = 0;
    if (!MeshMsg::parseHeader(s.data, s.len, ctr, kind)) return;
    // Cheap check first: a counter this sender has already used cannot be a
    // new message, so it never costs a decryption.
    if (!s_replay.fresh(s.nmac, ctr)) { ++s_rxReplay; return; }
    // The invite's frames are the one thing a board without a phrase reads.
    if (kind == MeshMsg::KIND_INVITE_PUB || kind == MeshMsg::KIND_INVITE_KEY) { deliverInvite(s, now, ctr, kind); return; }
    if (!ready()) return;

    if (kind == MeshMsg::KIND_READ) {
        uint32_t mc = 0;
        if (MeshMsg::openRead(MeshCrypto::impl(), s.nmac, s.data, s.len, ctr, mc) == MeshMsg::Open::OK) {
            squadNote(s.mac, now);
            if (mc == s_sentCtr) {
                s_readHave = true;
                snprintf(s_readBy, sizeof s_readBy, "%s", s.name[0] ? s.name : "SOMEONE");
                Serial.printf("[meshtalk] %s read #%lu\n", s_readBy, (unsigned long)mc);
            }
        }
        return;
    }

    if (kind == MeshMsg::KIND_FIX) {
        // Where a phone in the squad is (the companion app). Handed to the GPS
        // module as a phone's position: the black box stamps catches with it
        // and the watches wardrive with it. Logged once a minute, not per frame.
        int32_t la = 0, lo = 0; uint8_t acc = 0;
        if (MeshMsg::openFix(MeshCrypto::impl(), s.nmac, s.data, s.len, ctr, la, lo, acc) == MeshMsg::Open::OK) {
            squadNote(s.mac, now);
            const bool had = Gnss::fresh(now);
            Gnss::phone(la, lo, acc, Clock::trusted() ? Clock::nowEpoch() : 0, now);
            static uint32_t s_saidAt = 0;
            if (!had || now - s_saidAt > 60000) {
                s_saidAt = now;
                Serial.printf("[gnss] position from %s, +/-%u m%s\n", s.name[0] ? s.name : "a phone",
                              (unsigned)acc, Gnss::fromPhone() ? "" : " (ignored: own GPS has a fix)");
            }
        }
        return;
    }

    if (kind == MeshMsg::KIND_HELLO) {
        uint8_t ver[3];
        uint32_t theirEpoch = 0; uint8_t theirZone = 0;
        if (MeshMsg::openHello(MeshCrypto::impl(), s.nmac, s.data, s.len, ctr, ver, theirEpoch, theirZone) == MeshMsg::Open::OK) {
            squadNote(s.mac, now);
#ifdef BENCH_TOOLS
            Serial.printf("[meshtalk] hello from %s: v%u.%u.%u epoch %lu zone %u (clock %s)\n", s.name[0] ? s.name : "a member",
                          ver[0], ver[1], ver[2], (unsigned long)theirEpoch, theirZone, Clock::trusted() ? "already set" : "unset");
#endif
            // A member's clock, for a board that has none: the squad is in
            // the same room, so its zone too when none was ever chosen here.
            // Never over a clock this board already has; a hello is a
            // second-hand answer and the network is the first-hand one.
            if (theirEpoch && !Clock::trusted() && Clock::setEpoch(theirEpoch))
                Serial.printf("[clock] set from %s's hello\n", s.name[0] ? s.name : "a member");
            if (theirZone && !Settings::timeZoneChosen()) {
                Settings::setTimeZone((uint8_t)(theirZone - 1));
                Serial.printf("[zone] %s, from %s's hello\n", Settings::timeZoneName(), s.name[0] ? s.name : "a member");
            }
            // A member on something newer is the field's update check: no
            // WiFi, no site, just somebody nearby who already has it.
            uint8_t mine[3];
            if (ver[0] | ver[1] | ver[2]) {
                if (MeshMsg::parseVersion(OtaCore::runningVersion(), mine) && MeshMsg::versionNewer(ver, mine)) {
                    char vs[16];
                    snprintf(vs, sizeof vs, "%u.%u.%u", ver[0], ver[1], ver[2]);
                    OtaCore::noteAvailable(vs, s.name[0] ? s.name : "");
                }
            }
        }
        return;
    }

    if (kind == MeshMsg::KIND_CANNED) {
        uint8_t line = 0;
        const MeshMsg::Open r = MeshMsg::openCanned(MeshCrypto::impl(), s.nmac,
                                                    s.data, s.len, ctr, line);
        // Anything else is another group's message, or a forgery, or a frame
        // this build cannot read. None of them is recorded, which is what
        // stops a forger poisoning the replay table.
        if (r != MeshMsg::Open::OK && r != MeshMsg::Open::UNKNOWN_LINE) return;
        s_replay.record(s.nmac, ctr);
        saveReplay();
        squadNote(s.mac, now);
        s_inbox.text        = false;
        s_inbox.unknownLine = (r == MeshMsg::Open::UNKNOWN_LINE);
        s_inbox.canned      = line;
        s_inbox.ctr         = ctr;
        arrived(s, now);
        return;
    }

    if (kind == MeshMsg::KIND_TEXT) {
        uint8_t part = 0, total = 0;
        char chars[MeshMsg::TEXT_PART_CHARS + 1];
        if (MeshMsg::openTextPart(MeshCrypto::impl(), s.nmac, s.data, s.len,
                                  ctr, part, total, chars) != MeshMsg::Open::OK) return;
        char body[MeshMsg::TEXT_MAX + 1];
        uint32_t base = 0;
        // Parts wait here, each already authenticated, until the last one
        // lands. Only then is the message recorded -- by its LAST counter, so
        // its own parts coming round again are stale from then on.
        if (!s_asm.add(s.nmac, ctr, part, total, chars, body, base)) return;
        s_replay.record(s.nmac, base + total - 1);
        saveReplay();
        squadNote(s.mac, now);
        s_inbox.text        = true;
        s_inbox.unknownLine = false;
        s_inbox.ctr         = base;
        memcpy(s_inbox.body, body, sizeof s_inbox.body);
        arrived(s, now);
        return;
    }

    if (kind == MeshMsg::KIND_NUDGE) {
        uint8_t ver[3] = { 0, 0, 0 }, parts = 0;
        if (MeshMsg::openNudge(MeshCrypto::impl(), s.nmac, s.data, s.len, ctr, ver, parts)
                != MeshMsg::Open::OK) return;
        // Recorded at the nudge's own counter, not past its WiFi parts: those
        // still have to get through the replay check, and they carry
        // higher counters of their own.
        s_replay.record(s.nmac, ctr);
        saveReplay();
        squadNote(s.mac, now);
        memcpy(s_nudge.ver, ver, 3);
        s_nudge.wifiParts = parts;
        s_nudge.wifiBase  = ctr + 1;
        memcpy(s_nudge.mac, s.mac, 6);
        const char* from = s.name[0] ? s.name : "SOMEONE";
        size_t i = 0;
        for (; i < sizeof s_nudge.from - 1 && from[i]; i++) s_nudge.from[i] = from[i];
        s_nudge.from[i] = '\0';
        s_nudge.at   = now;
        s_nudgeHave  = true;
        Serial.printf("[meshtalk] %s asks the squad to update to v%u.%u.%u (%u wifi parts)\n",
                      s_nudge.from, ver[0], ver[1], ver[2], (unsigned)parts);
        return;
    }

    if (kind == MeshMsg::KIND_WIFI) {
        uint8_t part = 0, total = 0, bytes[MeshMsg::WIFI_PART_BYTES];
        if (MeshMsg::openWifiPart(MeshCrypto::impl(), s.nmac, s.data, s.len, ctr, part, total, bytes)
                != MeshMsg::Open::OK) return;
        // Not recorded in the replay table part by part: a nudge is rare and
        // the series is used once, so the table's slots are better spent on
        // the senders' message counters. The nudge itself was recorded.
        if (s_wifiAsm.add(s.nmac, ctr, part, total, bytes))
            Serial.println("[meshtalk] shared wifi received");
        return;
    }

    if (kind == MeshMsg::KIND_UPDATED) {
        uint8_t ver[3] = { 0, 0, 0 };
        if (MeshMsg::openUpdated(MeshCrypto::impl(), s.nmac, s.data, s.len, ctr, ver)
                != MeshMsg::Open::OK) return;
        s_replay.record(s.nmac, ctr);
        saveReplay();
        squadNote(s.mac, now);
        memcpy(s_updated.ver, ver, 3);
        memcpy(s_updated.mac, s.mac, 6);
        const char* from = s.name[0] ? s.name : "SOMEONE";
        size_t i = 0;
        for (; i < sizeof s_updated.from - 1 && from[i]; i++) s_updated.from[i] = from[i];
        s_updated.from[i] = '\0';
        s_updatedHave = true;
        Serial.printf("[meshtalk] %s reports v%u.%u.%u\n", s_updated.from, ver[0], ver[1], ver[2]);
        return;
    }

    if (kind == MeshMsg::KIND_HEADSUP) {
        uint8_t type = 0, tail[3] = { 0, 0, 0 };
        int8_t  rssi = 0;
        if (MeshMsg::openHeadsUp(MeshCrypto::impl(), s.nmac, s.data, s.len, ctr, type, rssi, tail)
                != MeshMsg::Open::OK) return;
        // Recorded so its nine seconds of repeats are one banner, but not
        // written to flash for it: a busy street would wear the store for
        // news nobody replays.
        s_replay.record(s.nmac, ctr);
        squadNote(s.mac, now);
        if (!Settings::meshHeadsUp() || !headsUpType(type)) return;   // off, or a newer build's type
        s_huIn.type = type;
        s_huIn.rssi = rssi;
        memcpy(s_huIn.tail, tail, 3);
        memcpy(s_huIn.mac, s.mac, 6);
        snprintf(s_huIn.from, sizeof s_huIn.from, "%s", s.name[0] ? s.name : "SOMEONE");
        s_huIn.at = now;
        s_huHave  = true;
        Serial.printf("[meshtalk] heads-up from %s: type %u at %d dBm\n", s_huIn.from, (unsigned)type, (int)rssi);
        return;
    }

    if (kind == MeshMsg::KIND_EMOTE) {
        uint8_t em = 0, setup = 0;
        const MeshMsg::Open r = MeshMsg::openEmote(MeshCrypto::impl(), s.nmac,
                                                   s.data, s.len, ctr, em, setup);
        if (r != MeshMsg::Open::OK && r != MeshMsg::Open::UNKNOWN_LINE) return;
        s_replay.record(s.nmac, ctr);
        saveReplay();
        squadNote(s.mac, now);
        if (r != MeshMsg::Open::OK) return;      // a newer build's: nothing to act out
        s_emote.emote = em;
        s_emote.setup = setup;
        memcpy(s_emote.mac, s.mac, 6);
        s_emote.at    = now;
        s_emoteHave   = true;
        Serial.printf("[meshtalk] emote %u (setup %u) from %s\n", (unsigned)em,
                      (unsigned)setup, s.name[0] ? s.name : "SOMEONE");
    }
}

uint32_t hwRandom() { return esp_random(); }

// `n` consecutive counters -- the parts of one message must be, so a receiver
// can find its first -- with the reservation persisted BEFORE any is used. If
// the write fails, the reservation is thrown away rather than used unrecorded:
// a counter NVS does not know was handed out is one a reboot can hand out again.
bool takeCounters(uint8_t n, uint32_t& base) {
    if (s_ctr.needsReserve() || s_ctr.limit - s_ctr.next < n) {
        const uint32_t hw = s_ctr.reserve(s_prefs.getUInt("ctr", 0));
        if (s_prefs.putUInt("ctr", hw) == 0) { s_ctr = MeshMsg::Counter(); return false; }
    }
    base = s_ctr.next;
    // Three bytes on the air. Refuse rather than wrap: a wrapped counter is a
    // reused nonce.
    if (base + n - 1 > MeshMsg::COUNTER_MAX) return false;
    for (uint8_t i = 0; i < n; i++) s_ctr.take();
    return true;
}

Send checks() {
    if (!ready()) return Send::NOT_READY;
    // Replying needs the radio on. The consent gate lives in meshTransmit(),
    // so a device that never agreed to transmit cannot send a message either.
    if (!Settings::meshTransmit()) return Send::TRANSMIT_OFF;
    if (!s_macSet) return Send::FAILED;          // radio not up yet
    return Send::OK;
}

void onAir(uint8_t n, uint32_t now, uint32_t ms = SEND_MS, bool emote = false) {
    s_outN     = n;
    s_outStart = now;
    s_outUntil = now + ms;
    s_outEmote = emote;
    s_outGen++;
}

} // namespace

void begin() {
    s_prefs.begin("meshtalk", false);
    rosterLoad();

    // Before any key is loaded -- the self-test keys the cipher with its own
    // test key and leaves it unkeyed afterwards.
    s_selfTestOk = MeshCrypto::selfTest();
    Serial.printf("[meshtalk] crypto self-test %s\n", s_selfTestOk ? "PASS" : "FAIL");

    memset(s_phrase, 0, sizeof s_phrase);
    s_prefs.getString("phrase", s_phrase, sizeof s_phrase);
    s_phrase[sizeof s_phrase - 1] = '\0';
    uint8_t key[MeshMsg::KEY_LEN];
    const size_t kl = s_prefs.getBytes("key", key, sizeof key);
    // The stored key, not a re-derived one: stretching takes seconds and boot
    // has better things to do with them.
    s_havePhrase = s_selfTestOk && s_phrase[0] && kl == sizeof key &&
                   MeshCrypto::impl().setKey(key);
    // A board that installed on a nudge owes the squad one word once it is
    // back. Cleared here and sent from tick() when the radio is up.
    if (s_prefs.getBool("nudged", false)) {
        s_prefs.remove("nudged");
        s_ackPending = true;
    }
    // What had already been delivered before the reboot is still delivered.
    if (s_havePhrase && s_prefs.isKey("replay")) {
        uint8_t b[MeshMsg::Replay::BYTES];
        if (!s_replay.load(b, s_prefs.getBytes("replay", b, sizeof b))) s_prefs.remove("replay");
    }
}

bool        selfTestOk()   { return s_selfTestOk; }
bool        havePhrase()   { return s_havePhrase; }
const char* phrase()       { return s_havePhrase ? s_phrase : ""; }
uint32_t    lastDeriveMs() { return s_deriveMs; }

void rollPhrase(uint16_t out[MeshMsg::PHRASE_WORDS]) { MeshMsg::roll(hwRandom, out); }

bool setPhrase(const char* text) {
    if (!s_selfTestOk || !text || !text[0]) return false;
    const size_t n = strlen(text);
    if (n >= sizeof s_phrase) return false;

    uint8_t key[MeshMsg::KEY_LEN];
    const uint32_t t0 = millis();
    if (!MeshCrypto::impl().derive(text, n, (const uint8_t*)MeshMsg::SALT,
                                   strlen(MeshMsg::SALT), MeshMsg::ITERS, key)) return false;
    s_deriveMs = millis() - t0;
    Serial.printf("[meshtalk] key stretched in %lu ms (%lu rounds)\n",
                  (unsigned long)s_deriveMs, (unsigned long)MeshMsg::ITERS);
    if (!MeshCrypto::impl().setKey(key)) return false;

    // A different phrase is a different squad: the roster of the old one
    // goes with it. The same phrase typed again keeps it.
    if (strcmp(s_phrase, text) != 0) { s_rosterN = 0; s_prefs.remove("roster"); }
    s_prefs.putString("phrase", text);
    s_prefs.putBytes("key", key, sizeof key);
    memcpy(s_phrase, text, n + 1);
    s_havePhrase = true;
    // A different group: counters and half-heard messages under the old key
    // mean nothing now.
    s_replay = MeshMsg::Replay();
    s_asm    = MeshMsg::Assembly();
    s_prefs.remove("replay");
    return true;
}

void clearPhrase() {
    s_prefs.remove("phrase");
    s_prefs.remove("key");
    s_prefs.remove("replay");
    s_prefs.remove("roster");
    s_rosterN = 0;
    s_replay = MeshMsg::Replay();
    memset(s_phrase, 0, sizeof s_phrase);
    s_havePhrase = false;
    s_outN = 0;
    s_outGen++;                // the radio drops the scan response on its next tick
}

bool ready() {
    return Settings::messagesOn() && s_havePhrase && s_selfTestOk;
}

Send send(uint8_t canned, uint32_t now) {
    const Send ok = checks();
    if (ok != Send::OK) return ok;
    uint32_t c = 0;
    if (!takeCounters(1, c)) return Send::FAILED;
    s_sentCtr = c;
    const size_t n = MeshMsg::sealCanned(MeshCrypto::impl(), s_ownMac, c, canned,
                                         s_out[0], sizeof s_out[0]);
    if (n == 0) return Send::FAILED;
    s_outLen[0] = (uint8_t)n;
    onAir(1, now);
    Serial.printf("[meshtalk] sending #%lu: %s\n", (unsigned long)c, MeshMsg::CANNED[canned]);
    return Send::OK;
}

Send sendText(const char* text, uint32_t now) {
    const Send ok = checks();
    if (ok != Send::OK) return ok;
    const uint8_t total = MeshMsg::textParts(text);
    if (total == 0) return Send::FAILED;
    uint32_t base = 0;
    if (!takeCounters(total, base)) return Send::FAILED;
    s_sentCtr = base;
    for (uint8_t p = 0; p < total; p++) {
        const size_t n = MeshMsg::sealTextPart(MeshCrypto::impl(), s_ownMac, base + p, text,
                                               p, total, s_out[p], sizeof s_out[p]);
        if (n == 0) { s_outN = 0; return Send::FAILED; }
        s_outLen[p] = (uint8_t)n;
    }
    onAir(total, now);
    Serial.printf("[meshtalk] sending #%lu (%u parts): %s\n",
                  (unsigned long)base, (unsigned)total, text);
    return Send::OK;
}

Send sendEmote(uint8_t emote, uint8_t setup, uint32_t now) {
    const Send ok = checks();
    if (ok != Send::OK) return ok;
    uint32_t c = 0;
    if (!takeCounters(1, c)) return Send::FAILED;
    const size_t n = MeshMsg::sealEmote(MeshCrypto::impl(), s_ownMac, c, emote, setup,
                                        s_out[0], sizeof s_out[0]);
    if (n == 0) return Send::FAILED;
    s_outLen[0] = (uint8_t)n;
    onAir(1, now, EMOTE_MS, true);
    Serial.printf("[meshtalk] sending #%lu: emote %u (setup %u)\n", (unsigned long)c,
                  (unsigned)emote, (unsigned)setup);
    return Send::OK;
}

Send sendNudge(const uint8_t ver[3], const char* ssid, const char* pass, uint32_t now) {
    const Send ok = checks();
    if (ok != Send::OK) return ok;
    uint8_t blob[MeshMsg::WIFI_BLOB_MAX];
    size_t  blobLen = 0;
    if (ssid && ssid[0]) {
        blobLen = MeshMsg::wifiBlob(ssid, pass ? pass : "", blob);
        if (blobLen == 0) return Send::FAILED;      // too long to share
    }
    const uint8_t parts = MeshMsg::wifiParts(blobLen);
    uint32_t base = 0;
    if (!takeCounters((uint8_t)(1 + parts), base)) return Send::FAILED;
    size_t n = MeshMsg::sealNudge(MeshCrypto::impl(), s_ownMac, base, ver, parts, s_out[0], sizeof s_out[0]);
    if (n == 0) return Send::FAILED;
    s_outLen[0] = (uint8_t)n;
    for (uint8_t p = 0; p < parts; p++) {
        n = MeshMsg::sealWifiPart(MeshCrypto::impl(), s_ownMac, base + 1 + p, blob, blobLen,
                                  p, parts, s_out[1 + p], sizeof s_out[1 + p]);
        if (n == 0) { s_outN = 0; memset(blob, 0, sizeof blob); return Send::FAILED; }
        s_outLen[1 + p] = (uint8_t)n;
    }
    memset(blob, 0, sizeof blob);
    onAir((uint8_t)(1 + parts), now, NUDGE_MS);
    Serial.printf("[meshtalk] nudging the squad to v%u.%u.%u (#%lu, %u wifi parts)\n",
                  ver[0], ver[1], ver[2], (unsigned long)base, (unsigned)parts);
    return Send::OK;
}

bool takeNudge(NudgeIn& out) {
    if (!s_nudgeHave) return false;
    s_nudgeHave = false;
    out = s_nudge;
    return true;
}

bool takeNudgeWifi(const NudgeIn& n, char ssid[MeshMsg::WIFI_SSID_MAX + 1], char pass[MeshMsg::WIFI_PASS_MAX + 1]) {
    if (n.wifiParts == 0) return false;
    return s_wifiAsm.take(n.mac, n.wifiBase, ssid, pass);
}

void markNudged() { s_prefs.putBool("nudged", true); }

const uint8_t* ownMac() { return s_ownMac; }

uint8_t       rosterCount()        { return s_rosterN; }
uint16_t rosterMet(const uint8_t mac[6]) {
    for (uint8_t i = 0; i < s_rosterN; i++)
        if (memcmp(s_roster[i].mac, mac, 6) == 0) return s_roster[i].met;
    return 0;
}
const Member& rosterAt(uint8_t i)  { return s_roster[i < s_rosterN ? i : 0]; }
void rosterForget(const uint8_t mac[6]) {
    for (uint8_t i = 0; i < s_rosterN; i++) {
        if (memcmp(s_roster[i].mac, mac, 6) != 0) continue;
        for (uint8_t k = i + 1; k < s_rosterN; k++) s_roster[k - 1] = s_roster[k];
        s_rosterN--;
        // And off the heard-lately table too, or the next hello puts them
        // straight back with a fresh count of one.
        for (uint8_t k = 0; k < SQUAD_N; k++)
            if (s_squad[k].live && memcmp(s_squad[k].mac, mac, 6) == 0) s_squad[k].live = false;
        rosterSave();
        return;
    }
}

bool inSquad(const uint8_t mac[6], uint32_t now) {
    for (uint8_t i = 0; i < SQUAD_N; i++)
        if (s_squad[i].live && memcmp(s_squad[i].mac, mac, 6) == 0)
            return now - s_squad[i].at < SQUAD_FRESH_MS;
    return false;
}

bool takeUpdated(UpdatedIn& out) {
    if (!s_updatedHave) return false;
    s_updatedHave = false;
    out = s_updated;
    return true;
}

// ---- the invite -------------------------------------------------------------
namespace {
void inviteWipe() {
    memset(s_invPriv, 0, sizeof s_invPriv);
    memset(s_invPub, 0, sizeof s_invPub);
    memset(s_invPeerPub, 0, sizeof s_invPeerPub);
    memset(s_invKey, 0, sizeof s_invKey);
    memset(s_invPhrase, 0, sizeof s_invPhrase);
    s_invPhraseIn = false;
    s_invAsm.clear();
    s_invCode = 0;
}
// The squad key back on the cipher after a session-key call. A board with
// no phrase (the invitee, usually) is left unkeyed, as it was.
void inviteKeyRestore() {
    uint8_t key[MeshMsg::KEY_LEN];
    if (s_havePhrase && s_prefs.getBytes("key", key, sizeof key) == sizeof key) MeshCrypto::impl().setKey(key);
    memset(key, 0, sizeof key);
}
void inviteFail(const char* why, uint32_t now) {
    s_invWhy = why;
    inviteWipe();
    s_outN = 0; s_outGen++;
    inviteTo(InviteState::FAILED, now);
    Serial.printf("[invite] failed: %s\n", why);
}
// Four parts of a blob onto the air, INVITE_PUB in the clear or INVITE_KEY
// under the session key.
Send invitePut(uint8_t kind, const uint8_t blob[MeshMsg::INVITE_BLOB], uint32_t ms, uint32_t now) {
    uint32_t base = 0;
    if (!takeCounters(MeshMsg::INVITE_PARTS, base)) return Send::FAILED;
    if (kind == MeshMsg::KIND_INVITE_KEY && !MeshCrypto::impl().setKey(s_invKey)) return Send::FAILED;
    bool ok = true;
    for (uint8_t p = 0; p < MeshMsg::INVITE_PARTS && ok; p++) {
        const size_t n = (kind == MeshMsg::KIND_INVITE_PUB)
            ? MeshMsg::sealInvitePub(MeshCrypto::sha256, base + p, blob, p, s_out[p], sizeof s_out[p])
            : MeshMsg::sealInviteKey(MeshCrypto::impl(), s_ownMac, base + p, blob, p, s_out[p], sizeof s_out[p]);
        s_outLen[p] = (uint8_t)n;
        ok = n != 0;
    }
    if (kind == MeshMsg::KIND_INVITE_KEY) inviteKeyRestore();
    if (!ok) { s_outN = 0; return Send::FAILED; }
    onAir(MeshMsg::INVITE_PARTS, now, ms);
    return Send::OK;
}
} // namespace

InviteState inviteState()    { return s_invState; }
bool        inviteIsInviter() { return s_invInviter; }
const char* inviteWhy()      { return s_invWhy; }
const char* invitePeerName() { return s_invPeerName; }
uint16_t    inviteCode()     { return s_invCode; }
uint32_t    inviteSince()    { return s_invSince; }
bool        inviteConfirmed(){ return s_invConfirmed; }

Send inviteStart(const uint8_t target[6], const char* name, uint32_t now) {
    // Needs a phrase to hand over and the radio to hand it with.
    if (!s_havePhrase || !s_selfTestOk) return Send::NOT_READY;
    if (!Settings::meshTransmit()) return Send::TRANSMIT_OFF;
    if (!s_macSet) return Send::FAILED;
    inviteWipe();
    if (!MeshCrypto::dhKeypair(s_invPriv, s_invPub)) return Send::FAILED;
    memcpy(s_invPeerMac, target, 6);
    snprintf(s_invPeerName, sizeof s_invPeerName, "%s", name && name[0] ? name : "SOMEONE");
    s_invInviter = true;
    uint8_t blob[MeshMsg::INVITE_BLOB];
    MeshMsg::invitePubBlob(target, 0, s_invPub, blob);
    const Send r = invitePut(MeshMsg::KIND_INVITE_PUB, blob, INVITE_OFFER_MS, now);
    if (r != Send::OK) { inviteWipe(); return r; }
    inviteTo(InviteState::OFFERING, now);
    Serial.printf("[invite] offering our squad to %s\n", s_invPeerName);
    return Send::OK;
}

Send inviteAccept(uint32_t now) {
    if (s_invState != InviteState::ASKED) return Send::FAILED;
    if (!Settings::meshTransmit()) return Send::TRANSMIT_OFF;
    if (!s_macSet) return Send::FAILED;
    if (!MeshCrypto::dhKeypair(s_invPriv, s_invPub)) { inviteFail("Could not make a key", now); return Send::FAILED; }
    uint8_t shared[MeshCrypto::DH_LEN];
    if (!MeshCrypto::dhShared(s_invPriv, s_invPeerPub, shared)) { inviteFail("Bad key from their board", now); return Send::FAILED; }
    MeshCrypto::dhSessionKey(shared, s_invKey);
    memset(shared, 0, sizeof shared);
    s_invCode = MeshCrypto::dhCode(s_invPub, s_invPeerPub);
    uint8_t blob[MeshMsg::INVITE_BLOB];
    MeshMsg::invitePubBlob(s_invPeerMac, 1, s_invPub, blob);
    const Send r = invitePut(MeshMsg::KIND_INVITE_PUB, blob, INVITE_OFFER_MS, now);
    if (r != Send::OK) { inviteFail("Could not answer", now); return r; }
    inviteTo(InviteState::CODE, now);
    Serial.printf("[invite] answered %s; code %04u\n", s_invPeerName, (unsigned)s_invCode);
    return Send::OK;
}

void inviteDecline() { inviteCancel(); }

Send inviteConfirm(uint32_t now) {
    if (s_invState != InviteState::CODE) return Send::FAILED;
    if (s_invInviter) {
        uint8_t blob[MeshMsg::INVITE_BLOB];
        if (MeshMsg::inviteKeyBlob(s_phrase, blob) == 0) { inviteFail("No phrase to send", now); return Send::FAILED; }
        const Send r = invitePut(MeshMsg::KIND_INVITE_KEY, blob, INVITE_KEY_MS, now);
        memset(blob, 0, sizeof blob);
        if (r != Send::OK) { inviteFail("Could not send the phrase", now); return r; }
        inviteTo(InviteState::SENDING, now);
        s_invConfirmed = false;
        for (uint8_t i = 0; i < SQUAD_N; i++)
            if (s_squad[i].live && memcmp(s_squad[i].mac, s_invPeerMac, 6) == 0) s_squad[i].live = false;
        Serial.println("[invite] digits matched; the phrase is on the air");
        return Send::OK;
    }
    // Invitee: the phrase may already be in.
    if (s_invPhraseIn) {
        if (setPhrase(s_invPhrase)) { inviteTo(InviteState::JOINED, now); joinedHello(); }
        else inviteFail("Could not take the phrase", now);
        memset(s_invPhrase, 0, sizeof s_invPhrase);
        s_invPhraseIn = false;
        return Send::OK;
    }
    inviteTo(InviteState::WAITING, now);
    return Send::OK;
}

void inviteCancel() {
    if (s_invState == InviteState::IDLE) return;
    inviteWipe();
    if (s_invState == InviteState::OFFERING || s_invState == InviteState::SENDING || s_invState == InviteState::CODE) { s_outN = 0; s_outGen++; }
    s_invState = InviteState::IDLE;
    s_invWhy = "";
}

static Send sendUpdated(uint32_t now) {
    const Send ok = checks();
    if (ok != Send::OK) return ok;
    uint8_t ver[3] = { 0, 0, 0 };
    MeshMsg::parseVersion(OtaCore::runningVersion(), ver);
    uint32_t c = 0;
    if (!takeCounters(1, c)) return Send::FAILED;
    const size_t n = MeshMsg::sealUpdated(MeshCrypto::impl(), s_ownMac, c, ver, s_out[0], sizeof s_out[0]);
    if (n == 0) return Send::FAILED;
    s_outLen[0] = (uint8_t)n;
    onAir(1, now);
    Serial.printf("[meshtalk] telling the squad: updated to v%u.%u.%u\n", ver[0], ver[1], ver[2]);
    return Send::OK;
}

// Broadcast each encrypted frame as a primary advertisement, then one
// appearance/identity slot per complete rotation. Never request a scan
// response from the receiver; both BLE scan modes can now receive frames.
const uint8_t* outgoing(uint32_t now, size_t& len, uint32_t& gen) {
    if (s_outN && (int32_t)(now - s_outUntil) < 0) {
        const uint8_t part = MeshPrimaryTransport::slot(now - s_outStart, s_outN);
        if (part < s_outN) {
            len = s_outLen[part];
            gen = MeshPrimaryTransport::generation(s_outGen, part);
            return s_out[part];
        }
    }
    len = 0; gen = 0; // presence slot or idle
    return nullptr;
}

bool sending(uint32_t now) {
    // A presence slot must NOT allow a hello/receipt to overwrite a live
    // message, invitation, or firmware update nudge.
    return s_outN && (int32_t)(now - s_outUntil) < 0;
}

bool sendingMessage(uint32_t now) { return sending(now) && !s_outEmote; }

bool headsUpType(uint8_t type) {
    switch ((DetectionType)type) {
        case DetectionType::FLOCK:  case DetectionType::AXON:     case DetectionType::SKIMMER:
        case DetectionType::RAVEN:  case DetectionType::ALPR:     case DetectionType::DEAUTH:
        case DetectionType::EVILTWIN: case DetectionType::HACKER:
            return true;
        default: return false;
    }
}

void noteCatch(uint8_t type, int8_t rssi, const uint8_t mac[6], uint32_t now) {
    if (!Settings::meshHeadsUp() || !headsUpType(type)) return;
    if (checks() != Send::OK) return;
    for (uint8_t i = 0; i < HEADSUP_TOLD_N; i++)
        if (s_huTold[i].live && memcmp(s_huTold[i].tail, mac + 3, 3) == 0 &&
            now - s_huTold[i].at < HEADSUP_REPEAT_MS) return;
    s_huOut.type = type;
    s_huOut.rssi = rssi;
    memcpy(s_huOut.tail, mac + 3, 3);
    s_huOut.at  = now;
    s_huOut.due = true;
}

bool takeHeadsUp(HeadsUpIn& out) {
    if (!s_huHave) return false;
    s_huHave = false;
    out = s_huIn;
    return true;
}

void forget() {
    memset(s_phrase, 0, sizeof s_phrase);
    s_havePhrase = false;
    s_replay     = MeshMsg::Replay();
    s_asm        = MeshMsg::Assembly();
    s_ctr        = MeshMsg::Counter();
    s_inbox      = Message{};
    for (uint8_t i = 0; i < INBOX_N; i++) s_hist[i] = Message{};
    s_histN = 0; s_histHead = 0;
    s_emoteHave  = false;
    s_nudgeHave  = false;
    s_updatedHave = false;
    s_wifiAsm    = MeshMsg::WifiAssembly();
    s_huHave     = false;
    s_huOut.due  = false;
    for (uint8_t i = 0; i < SQUAD_N; i++) s_squad[i].live = false;
    s_rosterN = 0;
    s_prefs.remove("roster");
    inviteCancel();
    s_outN       = 0;
    s_outGen++;
}

bool takeEmote(EmoteIn& out) {
    if (!s_emoteHave) return false;
    s_emoteHave = false;
    out = s_emote;
    return true;
}

void setOwnMac(const uint8_t mac[6]) {
    memcpy(s_ownMac, mac, 6);
    s_macSet = true;
}

RxDiagnostics rxDiagnostics() {
    return {
        __atomic_load_n(&s_rxSeen, __ATOMIC_RELAXED),
        __atomic_load_n(&s_rxQueued, __ATOMIC_RELAXED),
        __atomic_load_n(&s_rxQueueFull, __ATOMIC_RELAXED),
        s_rxReplay, s_rxInviteParts, s_rxInviteComplete,
        s_rxInviteRejected, s_rxInviteMask, s_rxLastRole, s_rxLastReject
    };
}

void onFrame(const uint8_t mac[6], const uint8_t* d, size_t len, const char* name, const uint8_t* sealedAs) {
    __atomic_fetch_add(&s_rxSeen, 1u, __ATOMIC_RELAXED);
    if (len == 0 || len > sizeof(Slot::data)) return;
    if (len == s_lastQLen && memcmp(mac, s_lastQMac, 6) == 0 && memcmp(d, s_lastQ, len) == 0) return;
    const uint32_t h = __atomic_load_n(&s_head, __ATOMIC_RELAXED);
    const uint32_t t = __atomic_load_n(&s_tail, __ATOMIC_ACQUIRE);
    if (h - t >= RING) {
        __atomic_fetch_add(&s_rxQueueFull, 1u, __ATOMIC_RELAXED);
        return;  // full -- the sender repeats, it will be back
    }
    Slot& s = s_ring[h % RING];
    memcpy(s.mac, mac, 6);
    memcpy(s.nmac, sealedAs ? sealedAs : mac, 6);
    s.len = (uint8_t)len;
    memcpy(s.data, d, len);
    size_t i = 0;
    if (name) for (; i < sizeof s.name - 1 && name[i]; i++) s.name[i] = name[i];
    s.name[i] = '\0';
    __atomic_store_n(&s_head, h + 1, __ATOMIC_RELEASE);
    __atomic_fetch_add(&s_rxQueued, 1u, __ATOMIC_RELAXED);
    memcpy(s_lastQMac, mac, 6);
    memcpy(s_lastQ, d, len);
    s_lastQLen = (uint8_t)len;
}

void tick(uint32_t now) {
    // Ten seconds in: the radio is up, the sender's screen is likely still
    // showing its tally, and nothing else is on the air yet.
    if (s_ackPending && now > 10000 && s_macSet && ready() && !sending(now)) {
        s_ackPending = false;
        sendUpdated(now);
    }
    // The read receipt, when one is owed and the air is free.
    if (s_readDue && ready() && s_macSet && Settings::meshTransmit() && !sending(now)) {
        s_readDue = false;
        uint32_t c = 0;
        if (takeCounters(1, c)) {
            const size_t n = MeshMsg::sealRead(MeshCrypto::impl(), s_ownMac, c, s_readCtr, s_out[0], sizeof s_out[0]);
            if (n) { s_outLen[0] = (uint8_t)n; onAir(1, now, EMOTE_MS, true); }
        }
    }
    // The heads-up, when one is waiting and the air is free. Ahead of the
    // hello: it is the one of the two that is news.
    if (s_huOut.due && now - s_huOut.at > HEADSUP_STALE_MS) s_huOut.due = false;
    if (s_huOut.due && checks() == Send::OK && !sending(now) &&
        (s_huSentAt == 0 || now - s_huSentAt > HEADSUP_GAP_MS)) {
        s_huOut.due = false;
        uint32_t c = 0;
        if (takeCounters(1, c)) {
            const size_t n = MeshMsg::sealHeadsUp(MeshCrypto::impl(), s_ownMac, c, s_huOut.type, s_huOut.rssi,
                                                  s_huOut.tail, s_out[0], sizeof s_out[0]);
            if (n) {
                s_outLen[0] = (uint8_t)n;
                onAir(1, now, EMOTE_MS, true);
                s_huSentAt = now ? now : 1;
                HeadsUpTold& t = s_huTold[s_huToldNext++ % HEADSUP_TOLD_N];
                memcpy(t.tail, s_huOut.tail, 3); t.at = now; t.live = true;
                Serial.printf("[meshtalk] sending #%lu: heads-up, type %u\n", (unsigned long)c, (unsigned)s_huOut.type);
            }
        }
    }
    // The hello: a few seconds every couple of minutes, only when nothing
    // else wants the air, and only from a board that is transmitting anyway.
    if (now > 15000 && ready() && s_macSet && Settings::meshTransmit() && !sending(now) &&
        (s_lastHello == 0 || now - s_lastHello > HELLO_EVERY_MS)) {
        s_lastHello = now;
        uint32_t c = 0;
        if (takeCounters(1, c)) {
            uint8_t ver[3] = { 0, 0, 0 };
            MeshMsg::parseVersion(OtaCore::runningVersion(), ver);
            const uint8_t zone = Settings::timeZoneChosen() ? (uint8_t)(Settings::timeZone() + 1) : 0;
            // Only a real time goes on the air; a guess would spread.
            const size_t n = MeshMsg::sealHello(MeshCrypto::impl(), s_ownMac, c, ver, Clock::trusted() ? Clock::nowEpoch() : 0, zone,
                                                s_out[0], sizeof s_out[0]);
            if (n) { s_outLen[0] = (uint8_t)n; onAir(1, now, HELLO_MS); }
        }
    }
    // Listen all the time while an invite is under way, on either side. At the
    // usual scan window of 75 a board heard exactly one squad frame in three
    // (every 4.5 s against a 1.5 s beacon; bench, 2026-09-24), which let the
    // handshake run out of time; at 99 it hears every one. Only for the
    // invite: the other 25 is the WiFi sniffer's airtime, and listening costs
    // battery. The first offer is still heard at 75 -- it stays on the air for
    // a minute.
    {
        static bool boosted = false;
        bool want = s_invState == InviteState::OFFERING || s_invState == InviteState::ASKED ||
                          s_invState == InviteState::CODE     || s_invState == InviteState::SENDING ||
                          s_invState == InviteState::WAITING;
        // Never longer than three minutes, even for an offer nobody answers.
        if (want && now - s_invSince > 180000) want = false;
        if (want != boosted) {
            boosted = want;
            setScanBoost(want);
            Serial.printf("[invite] listening %s\n", want ? "all the time until it is done" : "as usual again");
        }
    }
    // The invite's clocks: a side that waits too long for the other gives up,
    // and the inviter is done once the phrase has had its time on the air.
    if (s_invState == InviteState::OFFERING && now - s_invSince > INVITE_WAIT_MS) inviteFail("No answer from their board", now);
    if (s_invState == InviteState::WAITING  && now - s_invSince > INVITE_WAIT_MS) inviteFail("The phrase never arrived", now);
    // Their hello under the new phrase is the proof it landed: done, and
    // the phrase can come off the air. Otherwise done when its time is up,
    // unconfirmed.
    if (s_invState == InviteState::SENDING && inSquad(s_invPeerMac, now)) {
        s_invConfirmed = true;
        s_outN = 0; s_outGen++;
        inviteTo(InviteState::DONE, now);
        Serial.println("[invite] their board answered: added");
    }
    if (s_invState == InviteState::SENDING  && now - s_invSince > INVITE_KEY_MS + 1000) inviteTo(InviteState::DONE, now);
    // A finished or failed invite left on screen must not block the next
    // offer for ever: back to IDLE on its own after a while.
    if ((s_invState == InviteState::DONE || s_invState == InviteState::FAILED || s_invState == InviteState::JOINED) &&
        now - s_invSince > 300000) inviteCancel();
    uint32_t t = __atomic_load_n(&s_tail, __ATOMIC_RELAXED);
    const uint32_t h = __atomic_load_n(&s_head, __ATOMIC_ACQUIRE);
    while (t != h) {
        // Dropped unread when messages are off or there is no key -- the
        // ring still drains, so switching on later does not replay a backlog.
        deliver(s_ring[t % RING], now);
        t++;
        __atomic_store_n(&s_tail, t, __ATOMIC_RELEASE);
    }
}

const Message& inbox() { return s_inbox; }
uint8_t        inboxCount() { return s_histN; }
const Message& inboxAt(uint8_t i) {
    if (i >= s_histN) return s_inbox;
    return s_hist[(s_histHead + INBOX_N - 1 - i) % INBOX_N];
}
void markRead() {
    // Opening a message is the moment to say so: once, on the next tick,
    // if this board transmits at all. A reader with TRANSMIT off leaves the
    // sender seeing "sent" and never "read", which is the truth.
    if (s_inbox.have && s_inbox.unread) { s_readDue = true; s_readCtr = s_inbox.ctr; }
    s_inbox.unread = false;
}
bool takeRead(char* who, size_t cap) {
    if (!s_readHave) return false;
    s_readHave = false;
    snprintf(who, cap, "%s", s_readBy);
    return true;
}

const char* lineText(const Message& m) {
    if (m.text) return m.body;
    if (m.unknownLine) return "(a line this build doesn't know)";
    return MeshMsg::CANNED[m.canned < MeshMsg::CANNED_N ? m.canned : 0];
}

} // namespace MeshTalk
#endif // SQUACH_MESH
