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
Send     sendEmote(uint8_t emote, uint32_t now);
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
// BLE host task. Copies and returns; see the note at the top.
void onFrame(const uint8_t mac[6], const uint8_t* d, size_t len, const char* name);

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
};
const Message& inbox();
// The last few messages, newest first -- the SQUAD screen's inbox. RAM only:
// a reboot, or a wipe, and they are gone, as every message always has been.
constexpr uint8_t INBOX_N = 8;
uint8_t        inboxCount();
const Message& inboxAt(uint8_t i);        // 0 is the newest
void           markRead();
const char*    lineText(const Message& m);

// The last emote to arrive, handed over once. Not in the inbox: it is not
// something to read, and it never lights the red bubble.
struct EmoteIn {
    uint8_t  emote;         // MeshMsg::emoteByte
    uint8_t  mac[6];
    uint32_t at;
};
bool takeEmote(EmoteIn& out);

// Forget the phrase, the key and everything heard -- in RAM. The emulator's
// half of a security wipe; on the device the store is erased and the board
// restarts, which forgets all of it anyway.
void forget();

} // namespace MeshTalk
#endif
