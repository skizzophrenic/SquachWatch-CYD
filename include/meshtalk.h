// SquachWatch-CYD — SquachMesh messages at runtime: the phrase, the key, the
// counter, what is being sent and what has arrived.
//
// The pure protocol is meshmsg.h, the cipher is meshcrypto.h, and the radio is
// the Mesh namespace (mesh.cpp, and detection.cpp's radio half). This is the
// part that holds state, and the part the screens talk to.
//
// Threading: onFrame() is called from the BLE host task and does nothing but
// copy bytes into a small ring. Every decryption happens in tick(), on the loop
// task -- the same task that encrypts -- so the cipher is never used from two
// tasks at once.
#pragma once
#if SQUACH_MESH
#include <stdint.h>
#include <stddef.h>
#include "meshmsg.h"
#include "squachmesh.h"

namespace MeshTalk {

void begin();
void tick(uint32_t now);

bool        selfTestOk();
bool        havePhrase();
const char* phrase();                  // "" when none is set
// Stretches the phrase into a key and stores both. BLOCKS for about 2.75 s
// (see MeshMsg::ITERS) -- the caller should get a "stretching" frame on screen
// first -- and logs how long it took.
bool        setPhrase(const char* text);
void        clearPhrase();
uint32_t    lastDeriveMs();
// Five words from the hardware RNG, which is only cryptographically sound with
// a radio running. On this device one always is.
void        rollPhrase(uint16_t out[MeshMsg::PHRASE_WORDS]);

// Switched on, a phrase set, and the crypto self-test passed at boot.
bool ready();

enum class Send : uint8_t { OK, NOT_READY, TRANSMIT_OFF, FAILED };
Send     send(uint8_t canned, uint32_t now);
// Up to MeshMsg::TEXT_MAX characters from MeshMsg::TEXT_CHARSET. FAILED for
// anything textParts() refuses -- the keyboards only type what it accepts.
Send     sendText(const char* text, uint32_t now);
// An emote (see MeshMsg::Emote): one frame, on the air for nine seconds rather
// than thirty -- a reaction caught a quarter of a minute late would be acted
// out at nothing.
Send     sendEmote(uint8_t emote, uint8_t setup, uint32_t now);
bool     sending(uint32_t now);
// A MESSAGE on the air, not an emote. What an emote must not cut short.
bool     sendingMessage(uint32_t now);
// What the scan response should carry right now, or nullptr. A typed message
// is up to three frames, taken in turn; `gen` changes exactly when the answer
// does -- a new message, or the next part -- so the radio touches the stack
// only then.
const uint8_t* outgoing(uint32_t now, size_t& len, uint32_t& gen);

// ---- radio side ----
void setOwnMac(const uint8_t mac[6]);
// BLE host task. Copies and returns; see the note at the top. `sealedAs` is
// the six bytes the frame's nonce was built from when that is not the
// address it came from: a phone's identity (squachmesh.h). Null for a board.
void onFrame(const uint8_t mac[6], const uint8_t* d, size_t len, const char* name,
             const uint8_t* sealedAs = nullptr);

// Shared, read-only diagnosis of the message receiver. Kept in RAM only:
// identifying a four-part invitation failure must not write to flash.
// "seen" counts frames handed over by the BLE callback; "queued" means
// unique frames copied into the ring, before any crypto. The invite fields
// count hash-validated PUBLIC-key parts and complete four-part answers.
struct RxDiagnostics {
    uint32_t seen;
    uint32_t queued;
    uint32_t queueFull;
    uint32_t replayRejected;
    uint32_t inviteParts;
    uint32_t inviteComplete;
    uint32_t inviteRejected;
    uint8_t  mask;          // 4 bits: which of the latest invite parts arrived
    uint8_t  lastRole;      // 0 offer, 1 answer, 0xff unknown
    uint8_t  lastReject;    // 0 none, 1 tag, 2 blob, 3 target, 4 state, 5 peer MAC, 6 DH, 7 replay
};
RxDiagnostics rxDiagnostics();

struct Message {
    bool     have;
    bool     unread;
    bool     text;          // typed, in `body`; otherwise a canned line
    bool     unknownLine;   // authentic, from a newer build with more lines
    uint8_t  canned;
    char     body[MeshMsg::TEXT_MAX + 1];
    char     from[13];
    uint8_t  mac[6];
    uint32_t at;            // millis() when it arrived
    uint32_t ctr;           // the sender's counter, for the read receipt
};
const Message& inbox();
// The last few messages, newest first -- the SQUAD screen's inbox. RAM only:
// a reboot, or a wipe, and they are gone, as every message always has been.
constexpr uint8_t INBOX_N = 8;
uint8_t        inboxCount();
const Message& inboxAt(uint8_t i);        // 0 is the newest
void           markRead();
// A read receipt came back for the last message this board sent: who
// opened it. Consumes.
bool           takeRead(char* who, size_t cap);
const char*    lineText(const Message& m);

// The last emote to arrive, handed over once. Not in the inbox: it is not
// something to read, and it never lights the red bubble.
struct EmoteIn {
    uint8_t  emote;         // MeshMsg::Emote
    uint8_t  setup;         // and what both boards agree on for it
    uint8_t  mac[6];
    uint32_t at;
};
bool takeEmote(EmoteIn& out);

// ---- the squad update ----------------------------------------------------
// See meshmsg.h. The sender puts a NUDGE (and, if sharing, its WIFI parts)
// on the air for a minute; a board that hears one and decides to act calls
// markNudged() before it installs, and after the reboot this file sends one
// UPDATED reply on its own, once the radio is up.
struct NudgeIn {
    uint8_t  ver[3];
    uint8_t  wifiParts;     // 0: the sender kept its network to itself
    uint32_t wifiBase;      // the counter of the first WIFI part
    char     from[13];
    uint8_t  mac[6];
    uint32_t at;
};
// ssid/pass may be nullptr: a nudge without a network.
Send sendNudge(const uint8_t ver[3], const char* ssid, const char* pass, uint32_t now);
bool takeNudge(NudgeIn& out);
// The shared network for a nudge, once all its parts are in. Once only:
// the copy is wiped on the way out.
bool takeNudgeWifi(const NudgeIn& n, char ssid[MeshMsg::WIFI_SSID_MAX + 1], char pass[MeshMsg::WIFI_PASS_MAX + 1]);
// Remembered across the reboot an install ends in, so the UPDATED reply
// goes out on the next boot.
void markNudged();
struct UpdatedIn {
    uint8_t ver[3];
    char    from[13];
    uint8_t mac[6];
};
bool takeUpdated(UpdatedIn& out);

// ---- the squad heads-up ---------------------------------------------------
// See meshmsg.h. A board that catches something serious tells the squad
// once; the boards that hear it show a banner, not an alert card -- it is
// somebody else's catch. Trackers are left out: a squad member's own keys
// would set it off all day.
struct HeadsUpIn {
    uint8_t  type;          // DetectionType
    int8_t   rssi;          // as the sender heard it
    uint8_t  tail[3];       // the device's last three address bytes
    char     from[13];
    uint8_t  mac[6];
    uint32_t at;
};
// Which types travel.
bool headsUpType(uint8_t type);
// This board just announced a catch. Queued, not sent: it goes out on the
// next tick with the air free, at most one a minute, never the same device
// twice in ten, and not at all with HEADS-UP or TRANSMIT off.
void noteCatch(uint8_t type, int8_t rssi, const uint8_t mac[6], uint32_t now);
bool takeHeadsUp(HeadsUpIn& out);

// Whether that board has shown, within the last few minutes, that it holds
// our phrase: a HELLO, a message, an emote, or any other sealed frame this
// board could open. The SQUAD screen uses it to tell members from strangers.
bool inSquad(const uint8_t mac[6], uint32_t now);
// This board's own address, as it signs frames. All zeros until begin().
const uint8_t* ownMac();

// ---- the roster -----------------------------------------------------------
// Everybody ever heard holding our phrase, kept across restarts: who they
// are, what they looked like last time, and how many separate times they
// have turned up. The SQUAD screen under the SquachMesh menu shows it.
// Sixteen at most; when full, whoever has been met the fewest times makes
// room. Wiped with the phrase, because a new phrase is a new squad.
constexpr uint8_t ROSTER_N = 16;
struct Member {
    uint8_t          mac[6];
    SquachMesh::Peer look;
    uint16_t         met;
};
uint8_t       rosterCount();
uint16_t      rosterMet(const uint8_t mac[6]);   // 0 for a stranger
const Member& rosterAt(uint8_t i);
void          rosterForget(const uint8_t mac[6]);

// ---- the invite -------------------------------------------------------------
// See meshmsg.h and meshcrypto.h. Both roles live here; the screens only ask
// what state it is in and press the three buttons. The invitee needs
// MESSAGES on (to be handed frames at all) and TRANSMIT on (to answer), and
// no phrase -- an invite is how a board gets one.
enum class InviteState : uint8_t {
    IDLE,
    OFFERING,     // inviter: our key is on the air, waiting for theirs
    ASKED,        // invitee: an offer arrived, the person has not answered
    CODE,         // both: keys exchanged, four digits on screen, waiting for MATCHES
    SENDING,      // inviter: MATCHES pressed, the phrase is on the air
    WAITING,      // invitee: MATCHES pressed, waiting for the phrase
    JOINED,       // invitee: phrase taken and the key derived
    DONE,         // inviter: their board answered with the phrase, or the air time ran out -- see inviteConfirmed()
    FAILED,       // see inviteWhy()
};
InviteState inviteState();
bool        inviteIsInviter();
const char* inviteWhy();          // a short reason, from FAILED
const char* invitePeerName();     // the other side
uint16_t    inviteCode();         // 0..9999, from CODE on
uint32_t    inviteSince();        // millis() of the last state change
// DONE only: whether their board was heard holding the phrase. False means
// the phrase went out and nothing came back -- it may still have landed.
bool        inviteConfirmed();
Send        inviteStart(const uint8_t target[6], const char* name, uint32_t now);   // inviter
Send        inviteAccept(uint32_t now);           // invitee, from ASKED
void        inviteDecline();                      // invitee, from ASKED
Send        inviteConfirm(uint32_t now);          // both, from CODE: the digits match
void        inviteCancel();                       // any state back to IDLE, keys wiped

// Forget the phrase, the key and everything heard -- in RAM. The emulator's
// half of a security wipe; on the device the store is erased and the board
// restarts, which forgets all of it anyway.
void forget();

} // namespace MeshTalk
#endif
