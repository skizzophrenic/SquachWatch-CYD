// SquachWatch-CYD — clear (idle) screen implementation
#include "ui_clear.h"
#if SQUACH_LORA && defined(TDECK_PLUS)
#include "ui_lorachat.h"   // the LORA n NEW pill: the tiles drop under it while it is up
#endif
#include "clock.h"      // the watch's corner clock
#include "encounters.h" // random encounters on the main screen
#include "pet.h"        // ZOOMIES, and the Kaiju's Mothman
#include "sound.h"      // yoo-hoo, when a visitor walks in
#include "draw_band.h"
#include "december.h"
#include "frame_prof.h"
// Needed this early: the message helpers sit up with the visit machine,
// above where the rest of this file pulls these in.
#include "theme.h"
#include "security.h"   // the lock icon the counter tiles keep clear of
#include "settings.h"
#include "meshtalk.h"
#if SQUACH_MESH
#include "squachmesh.h"
#include "goblin.h"
#include "mesh_visit_appearance.h"
#include "meshtutor.h"
#include "squachy.h"
#include "detection.h"
#include "ignore_list.h"   // the NEARBY headline leaves ignored and snoozed devices out
#include "my_alerts.h"     // MY ALERTS takes the twelfth counter while its list is in use
#include "emote_script.h"
#include <esp_system.h>
#include "crowd_bench.h"
#if defined(CYD_GPS)
#include "gnss.h"       // the GPS label in the top bar
#if defined(CYD_GPS)
#include "wardrive.h"   // the Cardputer's wardrive tag in the same spot
#endif
#endif

// A peer supplied from outside -- the emulator's --peer flag today, the radio
// eventually. Always wins over the demo below.
static const SquachMesh::Peer* s_guest = nullptr;
void uiClearSetGuest(const SquachMesh::Peer* p) { s_guest = p; }
// The emulator's goblin, the same way. Wins over a goblin the radio hears.
static const Goblin::Visitor* s_simGoblin = nullptr;
void uiClearSetGoblin(const Goblin::Visitor* g) { s_simGoblin = g; }

#if SQUACH_MESH_DEMO
// LAB BUILDS ONLY. There is no radio yet, so a mesh build on real hardware
// would look identical to a normal one and there would be nothing to flash it
// for. This synthesises a visitor on a timer instead: he is absent for the
// first stretch so the screen can be compared against itself, then arrives and
// stays, wearing a different outfit on every cycle so one flash exercises the
// whole wardrobe rather than one costume.
//
// Deliberately NOT defined for the emulator, which builds SQUACH_MESH without
// this. A guest appearing unbidden in every sim render of the CLEAR screen
// would corrupt the instrument that every other visual decision is measured
// with -- the exact mistake the shim audit existed to undo.
static SquachMesh::Peer s_demo;
static bool     s_demoUp   = false;
static uint32_t s_demoSeen = 0xFFFFFFFFu;

static void demoTick(uint32_t now) {
    const uint32_t PERIOD = 20000;   // one full alone-then-visited cycle
    const uint32_t ALONE  = 6000;    // long enough to read the screen without him
    const uint32_t cycle  = now / PERIOD;
    if ((now % PERIOD) < ALONE) { s_demoUp = false; return; }
    if (s_demoUp && cycle == s_demoSeen) return;

    const uint8_t outfits = Squachy::outfitCount();
    s_demo.outfit = outfits ? (uint8_t)(cycle % outfits) : 0;
    s_demo.nick   = (uint8_t)(cycle % 10);
    s_demo.shade  = (uint8_t)(cycle % 4);
    s_demo.custom = false;
    s_demo.name[0] = '\0';
    s_demoUp   = true;
    s_demoSeen = cycle;
}
#endif // SQUACH_MESH_DEMO

// Defined below with the rest of the guest resolution, which reads more
// naturally next to the demo it falls back to than it would hoisted up here.
static const SquachMesh::Peer* rawGuest(uint32_t now);
static uint32_t                rawGuestId(uint32_t now);

// ---- the visit ------------------------------------------------------
// A visit has a shape: somebody turns up, they say hello, they stand around
// a while, somebody leaves. Modelling it as phases rather than "a guest is
// present" is what lets the arrival be an event and the hanging-around be a
// state -- the two things that were decided separately and have to coexist.
// HIGH_FIVE and STEP_BACK come between the walk in and the hellos: he walks
// right up to the host, they slap hands, and he steps back to his own spot.
enum class VisitPhase : uint8_t { ARRIVING, HIGH_FIVE, STEP_BACK, MEETING, HANGING, LEAVING, GONE };
static VisitPhase   s_vp       = VisitPhase::GONE;
static uint32_t     s_vpAt     = 0;          // when this phase started
static uint32_t     s_beatAt   = 0;          // when the current line went up
static uint32_t     s_beatNo   = 0;          // advances once per line
static bool         s_guestTurn = false;
static const char*  s_visitGuestLine = nullptr;
// Whether a crowd was actually DRAWN this frame, which is a different question
// from what CROWD is set to: the crowd only appears at two or more peers, so a
// board set to UP TO 8 with a single visitor is running an ordinary visit.
// uiClearEmote() needs the former and tested the latter, which silently
// refused every emote sent to such a board. Declared up here because that
// function is far above the crowd code that maintains it.
static bool         s_crowdDrawn = false;
static uint32_t     s_beatMs   = 2400;       // how long THIS line stays up
static uint8_t      s_hangStep = 0;          // 0 ask, 1 answer, 2 topper
static uint8_t      s_hitsThisBoot = 0;      // the log's size, for the banter
static uint32_t     s_guestLaughUntil = 0;   // the guest's half of the laugh

// A goblin from Network Goblin Scout (goblin.h, issue #37) visiting instead of
// a Squachy. The same phases -- walk in, high five, hello, standing around,
// goodbye -- with his own lines, no set pieces, no messages, and a time
// limit: a goblin's beacon is always on, and one living on the next desk
// would otherwise move in. After a visit that goblin is not hosted again for
// GOBLIN_AGAIN_MS. Nothing about him is kept past that: RAM only.
static bool                   s_hostingGoblin = false;
static Goblin::Visitor        s_goblinHost{};
static const Goblin::Visitor* s_rawGoblin = nullptr;  // set by rawGuestId()
static SquachMesh::Peer       s_goblinPeer{};         // the hosted copy's stand-in
static uint32_t               s_goblinByeId = 0, s_goblinByeAt = 0;
static const uint32_t         GOBLIN_STAY_MS  = 45000;
static const uint32_t         GOBLIN_AGAIN_MS = 30UL * 60UL * 1000UL;

// ---- random encounters (encounters.h) -------------------------------------------
// The one playing. Its visitors -- the Drifter, the goblin, the evil twin --
// go through the visit machine below as made-up guests; the rest are drawn
// over the scene by encounterBack() and encounterFront(), near the bottom of
// this file.
static Encounters::Kind s_encK = Encounters::Kind::NONE;
static uint32_t s_encAt = 0;
static uint32_t s_encSaid = 0;        // which of its lines have been said, a bit each
static uint32_t s_encSeq = 0;         // a fresh id for each encounter's visitor
static bool     s_encTapped = false;
static uint32_t s_encTapAt = 0;
static int      s_encHitX = -1000, s_encHitY = -1000, s_encHitR = 0;   // the tappable thing, last frame
#if SQUACH_MESH
static SquachMesh::Peer s_encPeer{};
static bool             s_encPeerOn = false;
static Goblin::Visitor  s_encGob{};
static bool             s_encGobOn = false;
// The die. It rolls on screen before every encounter, so nobody takes a
// made-up visitor for a SquachWatcher in range, and now and then for a roll
// that came to nothing. See dieTick() and dieDraw() by the encounter player.
//   DROP  falling, bouncing, then at rest showing what it rolled
//   WAIT  at rest, for the visitor it rolled to come and kick it
//   FLY   kicked: over Squachy's head and off the screen
//   PUFF  gone in a puff (the encounters nobody walks on for)
//   MISS  at rest on a dud while Squachy works out what it is
//   AWAY  rolling off, after a miss
//   TOSS  the free roll's die, tapped: up off the screen, to come down as DROP
//   BONK  a 1: off the floor, onto his head and away, and the stars after
enum class Die : uint8_t { NONE, DROP, WAIT, FLY, PUFF, MISS, AWAY, TOSS, BONK };
static Die              s_die     = Die::NONE;
static uint32_t         s_dieAt   = 0;       // when it dropped; for FLY, PUFF and AWAY, when that began
static uint8_t          s_dieNum  = 0;       // 1-5 a miss, 6-19 by rarity, 20 a shiny
static uint8_t          s_dieSaid = 0;       // its cues, a bit each
static Encounters::Kind s_dieFor  = Encounters::Kind::NONE;   // what starts once it has been read
static int              s_bonkX = 0, s_bonkY = 0;             // a 1: the top of his head, where it lands
// The free roll's die (Encounters::freeReady()). It sits on the floor by the
// right-hand edge while the screen is his alone, and a tap throws it.
static bool     s_freeShown  = false;
static uint32_t s_freeAt     = 0;       // when it rolled in
static uint32_t s_freeOutAt  = 0;       // when it was called away, to roll off; 0 for not
static int      s_freeX = -1000, s_freeY = 0, s_freeR = 0;   // where it sits: the tap's target, the throw's start
static bool     s_freeWant   = false;   // asked for by a key or the journal: thrown when the screen can
static uint32_t s_freeWantAt = 0;
static bool     s_freeSaid   = false;   // he has pointed it out
static uint32_t s_freeStayMs = 0, s_freeStayLast = 0;   // how long it has sat there in view, of Encounters::FREE_STAY_MS
static int      s_freeCx = -1000, s_freeCy = 0;   // where it is drawn now (it gets about): what a finger lands on
static int      s_outX = 0;             // where it was when it was called away
static int      s_tossX = 0, s_tossY = 0;         // where it was thrown from
// Squachy and the waiting die: now and then it rolls over and bumps his foot,
// and he boots it off the edge of the screen and back to where it sits.
static uint32_t s_playAt = 0, s_playNext = 0;     // a game under way since; when the next is due
static uint8_t  s_playSaid = 0;
static int      s_playTx = 0, s_playCx0 = 0;      // where it rolls to; where he stood when it set off
static bool     s_playKick = false;               // he was still there to boot it
static int      s_sqAnchor = -10000;              // where he has been standing...
static uint32_t s_sqStillAt = 0;                  // ...and since when
// A shiny encounter: where the dice's visitor was drawn, and the sparkles
// that whatever it is leaves behind it.
static int      s_encGX = 0, s_encGY = 0, s_encGR = 0;
struct ShinySpark { int16_t x, y; uint32_t at; };
static ShinySpark s_trail[10];
static uint8_t  s_trailN = 0;
static uint32_t s_trailAt = 0;
// The mimic (Encounters::Kind::MIMIC): the die that was never a die.
static float    s_mimX = 0;             // where it is, on the floor
static uint32_t s_mimLast = 0;          // the frame it last moved on
static uint32_t s_mimBiteAt = 0;        // when it got him; 0 until it has
static uint32_t s_mimWake = 0;          // ms in before it stops pretending (a die has to roll in first, or not)
static bool     s_mimTapped = false;    // it was the free roll's die, and somebody touched it
static int8_t   s_mimFace = -1;         // which way it looks
// A random visitor does not walk in from the edge, as somebody in range does:
// a hole opens in the sky, he drops out of it, and he leaves by it. On the
// clock since the visit's ARRIVING began (ms): the hole opens, he falls, he
// boots the die, and only then the high five.
static const uint32_t ENC_HOLE_MS = 450, ENC_FALL_MS = 420, ENC_KICK_MS = 1250, ENC_ARRIVE_MS = 2000;
static const uint32_t ENC_RISE_MS = 700;     // ...and back up into it, on LEAVING's clock
static bool encGuestHere();
// A strip of the screen, kept before something is drawn across it and put
// back afterwards: how a visitor is cut off at his hole, and the giant at the
// horizon. There is no clipping to do it with -- the bands own the viewport --
// and a background cannot repaint a strip of itself: each one lays its whole
// scene out between the two rows it is given. One strip at a time, a few
// kilobytes, freed when whatever wanted it is over.
static uint16_t* s_strip = nullptr;
static uint32_t  s_stripCap = 0;
static int       s_stripX = 0, s_stripY = 0, s_stripW = 0, s_stripH = 0;
static void stripFree() { free(s_strip); s_strip = nullptr; s_stripCap = 0; s_stripW = 0; }
static bool stripSave(TFT_eSPI& t, int x, int y, int w, int h) {
    s_stripW = 0;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > t.width())  w = t.width() - x;
    if (y + h > t.height()) h = t.height() - y;
    if (w <= 0 || h <= 0) return false;
    const uint32_t n = (uint32_t)w * (uint32_t)h;
    if (n > 20000u) return false;                 // 40 KB: past that, he is simply not cut off
    if (n > s_stripCap) {
        free(s_strip);
        s_strip = (uint16_t*)malloc(n * sizeof(uint16_t));
        s_stripCap = s_strip ? n : 0;
        if (!s_strip) return false;
    }
    uint32_t i = 0;
    for (int yy = y; yy < y + h; yy++)
        for (int xx = x; xx < x + w; xx++) s_strip[i++] = t.readPixel(xx, yy);
    s_stripX = x; s_stripY = y; s_stripW = w; s_stripH = h;
    return true;
}
static void stripRestore(TFT_eSPI& t) {
    if (!s_strip || s_stripW <= 0) return;
    uint32_t i = 0;
    for (int yy = s_stripY; yy < s_stripY + s_stripH; yy++)
        for (int xx = s_stripX; xx < s_stripX + s_stripW; xx++) t.drawPixel(xx, yy, s_strip[i++]);
    s_stripW = 0;
}
#endif

static const uint32_t WALK_MS  = 1800;       // across the gap, either way
// The high five. Arms up as he arrives, the hands close the last few pixels,
// they meet at FIVE_HIT_MS -- the spark -- and hold a beat before he steps back.
static const uint32_t FIVE_MS     = 1100;
static const uint32_t FIVE_HIT_MS = 420;
static const uint32_t STEP_MS     = 900;
// How far apart their centres are when the hands meet, per unit of scale:
// the HIGHFIVE arm ends S(30) out, and there are two of them.
static const float    REACH_K     = 60.0f;
// Hand height per Reach level (UP, DOWN, LEVEL), per unit of scale: where the
// sparks and the rock-paper-scissors icons go. Must match drawBody's.
static const float    REACH_Y[3]  = { 4.0f, 40.0f, 22.0f };

// The handshake. A board that has already visited since boot gets three
// slaps -- high, low, a fist bump -- where a first meeting gets the one.
static const uint32_t FRIEND_STEP_MS = 750, FRIEND_HIT_MS = 320;
static bool     s_oldFriend = false, s_friendGreeted = false;
static uint32_t s_fiveStepMs = FIVE_MS, s_fiveHitMs = FIVE_HIT_MS;
static uint8_t  s_fiveSteps = 1, s_fiveStep = 0;
// Who has visited since boot. RAM only, eight of them, by the same folded id
// the visit machine already uses: nothing about who you have met survives a
// reboot. This device exists to notice things that keep track of you, and it
// should not quietly become one.
static uint32_t s_friends[8];
static uint8_t  s_friendN = 0, s_friendNext = 0;
static bool friendSeen(uint32_t id) {
    for (uint8_t i = 0; i < s_friendN; i++) if (s_friends[i] == id) return true;
    return false;
}
static void friendAdd(uint32_t id) {
    s_friends[s_friendNext] = id;
    s_friendNext = (uint8_t)((s_friendNext + 1) % 8);
    if (s_friendN < 8) s_friendN++;
}
// The whole gap between one bubble going down and the next coming up. It is
// deliberately tiny.
//
// This used to be a fixed 4600 ms beat plus a further 700 or 3200 ms of
// nothing, and the result was two Squachys who each said a true thing and
// then stood there. Lines are timed by their own length now (Squachy::lineMs)
// and the next one lands almost on top of the last, which is what people
// actually sound like when they are enjoying each other's company. The pause
// was never the problem being solved -- it was the pacing of a status
// readout applied to a conversation.
static const uint32_t TURN_GAP_MS = 220;

// True while he should have a walk cycle under him.
static bool visitWalking() {
    return s_vp == VisitPhase::ARRIVING || s_vp == VisitPhase::STEP_BACK ||
           s_vp == VisitPhase::LEAVING;
}

// Eased so he settles rather than stopping dead. Same shape both ways, with
// the endpoints swapped -- a departure that accelerated away would read as
// fleeing, and he is only going home.
// An emote's high five moves him too; defined with the set pieces below.
static bool fiveGuestX(uint32_t now, int homeX, int meetX, int closePx, int& x);
// ...and so do the scripted emotes that walk him in, or over.
static bool scriptGuestX(uint32_t now, int homeX, int meetX, float gs, int& x);

static int visitGuestX(uint32_t now, int homeX, int offX, int meetX, int closePx, float gs) {
    int fx;
    // The dice's visitors come down where they will stand and go up from
    // where they stood. The lean is his boot going into the die.
    if (s_vp == VisitPhase::ARRIVING && encGuestHere()) {
        const uint32_t e = now - s_vpAt;
        if (e + 130 >= ENC_KICK_MS && e < ENC_KICK_MS) return meetX + (int)(2.0f * gs);
        if (e >= ENC_KICK_MS && e < ENC_KICK_MS + 160) return meetX - (int)(4.0f * gs);
        return meetX;
    }
    if (s_vp == VisitPhase::LEAVING && encGuestHere()) return homeX;
    if (fiveGuestX(now, homeX, meetX, closePx, fx)) return fx;
    if (scriptGuestX(now, homeX, meetX, gs, fx)) return fx;
    // In to the host for the high five, and back to his own spot after it.
    if (s_vp == VisitPhase::HIGH_FIVE) {
        const uint32_t e = now - s_vpAt;
        // The last few pixels close as the arms go up, so the hands arrive
        // together rather than already touching.
        if (e >= s_fiveHitMs) return meetX;
        return meetX + (int)((float)closePx * (1.0f - (float)e / (float)s_fiveHitMs));
    }
    if (s_vp == VisitPhase::STEP_BACK) {
        float k = (float)(now - s_vpAt) / (float)STEP_MS;
        if (k > 1) k = 1;
        k = k * k * (3.0f - 2.0f * k);
        return (int)(meetX + (homeX - meetX) * k);
    }
    if (s_vp == VisitPhase::ARRIVING || s_vp == VisitPhase::LEAVING) {
        // SIGNED, and the guard matters. LEAVING deliberately sets s_vpAt
        // into the FUTURE so the goodbye lands before he moves -- and both
        // are uint32_t, so `now - s_vpAt` wrapped to about 4.29 billion for
        // the whole length of that line. k clamped to 1, which is off-screen:
        // he vanished the instant he said goodbye and reappeared when the
        // bubble expired, then walked off.
        //
        // The identical hazard is already guarded in the LEAVING phase check
        // below, with an (int32_t) cast. It was missed here.
        const int32_t dt = (int32_t)(now - s_vpAt);
        // Still saying goodbye: stand still. An arrival's first frame has
        // dt 0 as well, and used to land here too -- so he was drawn standing
        // on his mark for one frame before walking in from the edge.
        if (dt <= 0) return (s_vp == VisitPhase::ARRIVING) ? offX : homeX;
        float k = (float)dt / (float)WALK_MS;
        if (k > 1) k = 1;
        k = k * k * (3.0f - 2.0f * k);                 // smoothstep
        const float from = (s_vp == VisitPhase::ARRIVING) ? (float)offX : (float)homeX;
        const float to   = (s_vp == VisitPhase::ARRIVING) ? (float)meetX : (float)offX;
        return (int)(from + (to - from) * k);
    }
    return homeX;
}

// Leaning in.
//
// Two Squachys standing at fixed marks looked like two Squachys standing at
// fixed marks, whatever they were saying to each other. This is the cheapest
// honest fix: the one who is LISTENING closes the gap a little, and gives it
// back when it is his turn. Nobody has to face anybody -- the bodies are
// drawn front-on and there is no mirrored artwork to turn them with -- but a
// weight shift that is driven by whose turn it is couples the two of them to
// the same clock, which is most of what "they are talking to each other"
// actually looks like.
//
// It is two movements, not one, and the second only became obviously
// necessary once the first was on screen:
//
// CLOSE is both of them stepping in a little once the talking starts, and
// staying there for the whole visit. It is what makes them a pair rather than
// two occupants of the same picture.
//
// LEAN is the listener leaning in further, and giving it back when it is his
// turn. On its own this had a flaw worth recording: the one who WAS listening
// returns to neutral at the same time the other one starts leaning, so for
// about half a second neither is leaning and the two of them visibly drift
// apart -- a pulse outward on every single turn, which is the exact moment
// they should look most connected. The constant close underneath means the
// crossover only ever redistributes the lean; the gap never opens back up to
// where it started.
//
// Small numbers on purpose: at this scale anything more stops being a lean
// and becomes a step, and then he is walking during a conversation.
static const int CLOSE_PX = 3;      // both, for the whole visit
static const int LEAN_PX  = 4;      // the listener, on top of that
static float     s_leanK  = 0.0f;   // -1 host leaning, 0 neutral, +1 guest
static float     s_closeK = 0.0f;   // 0 apart, 1 stood in
static uint32_t  s_leanAt = 0;

static void leanTick(uint32_t now) {
    const bool chatting = (s_vp == VisitPhase::MEETING ||
                           s_vp == VisitPhase::HANGING);
    // Nobody leans at somebody who is still walking, and nobody leans at
    // somebody who is leaving.
    const float target = !chatting ? 0.0f : (s_guestTurn ? -1.0f : 1.0f);

    // Time-based rather than a per-frame fraction: this screen runs anywhere
    // from 22 to 45 fps depending on the background, and a constant per-frame
    // step would make the lean visibly faster on the cheap ones.
    const uint32_t dt = (s_leanAt && now > s_leanAt) ? (now - s_leanAt) : 0;
    s_leanAt = now;
    float k = (float)dt / 450.0f;
    if (k > 1.0f) k = 1.0f;
    s_leanK  += (target - s_leanK) * k;
    // Slower, because this one is posture rather than attention: closing in
    // as fast as you glance at somebody looks like a flinch.
    float ck = (float)dt / 900.0f;
    if (ck > 1.0f) ck = 1.0f;
    s_closeK += ((chatting ? 1.0f : 0.0f) - s_closeK) * ck;
}

// Positive is rightward. The host stands on the left, so he closes by moving
// right and the guest by moving left.
static int hostLeanPx() {
    const float lean = (s_leanK < 0) ? -s_leanK : 0.0f;
    return (int)(s_closeK * CLOSE_PX + lean * LEAN_PX);
}
static int guestLeanPx() {
    const float lean = (s_leanK > 0) ? s_leanK : 0.0f;
    return -(int)(s_closeK * CLOSE_PX + lean * LEAN_PX);
}

// Which standing-around exchange is running. Advanced only when the HOST
// speaks, so his line and the guest's reply come from the same entry -- that
// pairing is the whole point, and keying it off the beat counter would drift
// the moment a beat is skipped.
static uint32_t s_exchange = 0;

// A goblin's beats: the host asks, the goblin answers. No topper and no
// context-picked banter -- that is about outfits and squads he has none of.
static void goblinBeat(uint32_t now, Squachy::VisitMoment m) {
    if (m == Squachy::VisitMoment::HANGOUT) {
        if (s_hangStep == 0) {
            s_guestTurn      = false;
            s_visitGuestLine = nullptr;
            s_beatMs         = Squachy::goblinHangHost(s_exchange);
            s_hangStep       = 1;
        } else {
            s_guestTurn      = true;
            s_visitGuestLine = Squachy::goblinHangGuest(s_exchange);
            s_beatMs         = Squachy::lineMs(s_visitGuestLine);
            Squachy::visitLaugh(now);
            s_hangStep       = 0;
            s_exchange++;
        }
        return;
    }
    s_guestTurn = !s_guestTurn;
    if (s_guestTurn) {
        s_visitGuestLine = Squachy::goblinGuestLine(m, s_beatNo);
        s_beatMs         = Squachy::lineMs(s_visitGuestLine);
        if (m == Squachy::VisitMoment::MEET) Squachy::visitLaugh(now);
    } else {
        s_visitGuestLine = nullptr;
        s_beatMs         = Squachy::goblinReaction(m, s_beatNo);
    }
}

static void visitBeat(uint32_t now, Squachy::VisitMoment m) {
    s_beatAt = now;
    s_beatNo++;
    if (s_hostingGoblin) { goblinBeat(now, m); return; }
    if (m == Squachy::VisitMoment::HANGOUT) {
        // Three beats, not two: question, answer, and the host getting the
        // last word. The strict two-beat alternation was correct dialogue
        // and still read as correspondence rather than company.
        switch (s_hangStep) {
            case 0: {                               // the host asks
                s_guestTurn      = false;
                s_visitGuestLine = nullptr;
                // What there is to talk about, gathered fresh: the weather
                // can change mid-visit and the hit count only goes up.
                Squachy::VisitContext vc = {};
                vc.background = (uint8_t)Settings::background();
                vc.caught     = (uint8_t)Squachy::lastCaught();
                vc.hostOutfit = Squachy::outfitIndex();
                if (const SquachMesh::Peer* g = uiClearGuest()) {
                    vc.guestOutfit = g->outfit;
                    if (g->custom && g->name[0]) snprintf(vc.guestName, sizeof vc.guestName, "%s", g->name);
                    if (Mesh::peer()) vc.met = MeshTalk::rosterMet(Mesh::peerMac());
                }
                vc.squad   = MeshTalk::rosterCount();
                vc.hits    = s_hitsThisBoot;
                vc.upHours = (uint16_t)(now / 3600000u);
                Squachy::setVisitContext(vc);
                s_beatMs         = Squachy::visitHangHost(s_exchange);
                s_hangStep       = 1;
                break;
            }
            case 1:                                 // the guest answers
                s_guestTurn      = true;
                s_visitGuestLine = Squachy::visitHangGuest(s_exchange);
                s_beatMs         = Squachy::lineMs(s_visitGuestLine);
                // The host has no bubble this beat, so his reaction is the
                // only thing of his on screen -- which is exactly why he
                // gets one. He hears the answer land and cracks up.
                Squachy::visitLaugh(now);
                // Only advance past the pair once it is actually finished.
                s_hangStep = Squachy::visitHangTopper(s_exchange) ? 2 : 0;
                if (s_hangStep == 0) s_exchange++;
                break;
            default:                                // the host's topper
                s_guestTurn       = false;
                s_visitGuestLine  = nullptr;
                s_beatMs          = Squachy::visitSay(
                                        Squachy::visitHangTopper(s_exchange));
                s_guestLaughUntil = now + 1500;     // and now the guest goes
                s_hangStep        = 0;
                s_exchange++;
                break;
        }
        return;
    }
    s_guestTurn = !s_guestTurn;
    if (s_guestTurn) {
        // The guest's line is held, not re-rolled: his bubble is redrawn
        // every frame it is up, and picking again each time would flicker
        // through the pool instead of saying one thing.
        s_visitGuestLine = Squachy::visitGuestLine(m, s_beatNo);
        s_beatMs         = Squachy::lineMs(s_visitGuestLine);
        // Pleased to see him. Not on the goodbye, obviously.
        if (m == Squachy::VisitMoment::MEET) Squachy::visitLaugh(now);
    } else {
        s_visitGuestLine = nullptr;
        // An old friend gets a different hello -- once, the first line.
        if (m == Squachy::VisitMoment::MEET && s_oldFriend && !s_friendGreeted) {
            s_friendGreeted = true;
            s_beatMs = Squachy::visitFriendHello(s_beatNo);
        } else {
            s_beatMs = Squachy::visitReaction(m);   // the host says it himself
        }
    }
}

// ---- set pieces ------------------------------------------------------------
// Now and then, between exchanges, the two of them do something together
// instead of talking. One at a time, never cutting a question off from its
// answer, and rare on purpose -- a set piece every exchange stops being one.
//
// Which one depends on the scene. SNOWFALL brings out the snowballs; SYNTHWAVE,
// the background that is already a dance floor, is mostly dance-offs and gets
// them twice as often; everywhere else, dance-offs and rock-paper-scissors take
// turns.
enum class Piece : uint8_t { NONE, DANCE, RPS, SNOW, WAVE, FIVE, BOO, SCRIPT };
static const uint32_t PIECE_FIRST_MS  = 25000;   // into the hanging-around, at the earliest
static const uint32_t PIECE_EVERY_MS  = 70000;
static const uint32_t PIECE_JITTER_MS = 40000;
static Piece    s_piece = Piece::NONE;
static uint32_t s_pieceAt = 0, s_nextPieceAt = 0;
static uint8_t  s_pieceStep = 0, s_pieceNo = 0;

// Dance-off: three turns -- him, the guest, both at once.
static const uint32_t DANCE_SEG_MS = 1700;
// Rock-paper-scissors: three pumps, the reveal, then how each of them took it.
static const uint32_t RPS_PUMP_MS = 1500, RPS_SHOW_MS = 1500, RPS_REACT_MS = 1400;
static uint8_t  s_rpsHost = 0, s_rpsGuest = 0;   // 0 rock, 1 paper, 2 scissors
// Snowballs: one throw each way. A wind-up, the release, then the flight.
static const uint32_t SNOW_SEG_MS = 1500, SNOW_WIND_MS = 380, SNOW_FLY_MS = 650;
static uint8_t  s_snowDone = 0;                  // which beats of the fight have fired
static uint32_t s_puffAt = 0;
static bool     s_puffGuest = false;

// ---- emotes -----------------------------------------------------------------
// A set piece a PERSON started, from the message screen -- on this board or on
// the visitor's. WAVE, FIVE and BOO exist only as emotes; a dance-off, rock-
// paper-scissors and a snowball fight are the same pieces the clock runs.
// Whoever sent it goes first: on the sending board the host starts, on the
// other the visitor does, so the two screens tell the same story from their
// two sides.
static bool     s_pieceGuestFirst = false;
static uint8_t  s_pieceSub = 0xFF;               // a wave's hand, up and down
// Emotes waiting to be acted out. A QUEUE, not one slot: four can land in the
// time a greeting takes, and a single slot meant every new one silently
// clobbered the last -- three of four sent in a row were lost that way, with
// nothing in the log to say so.
static const uint8_t EMOTE_Q_N = 4;
static struct EmoteQ { uint8_t emote, setup; bool guest; uint32_t at; } s_eq[EMOTE_Q_N];
static uint8_t  s_eqN = 0;
static const uint32_t EMOTE_WAIT_MS = 12000;     // ...and how long one may wait
// One RECEIVED emote, held until the visit can act it out -- see visitTick().
static MeshTalk::EmoteIn s_rxEmote{};
static bool              s_rxHave = false;
static const uint32_t WAVE_SEG_MS = 1400;        // one turn each
static const uint32_t FIVE_IN_MS  = 650;         // in to the host, from where he stands
static const uint32_t BOO_JUMP_MS = 380, BOO_MS = 1700;
static const char* const EMOTE_WAVE_CALL[]  = { "Heyyy!", "Yo!", "Hiii!" };
static const char* const EMOTE_WAVE_BACK[]  = { "Hey hey!", "Yo yo!", "Hi hi hi!" };
static const char* const EMOTE_FIVE_CALL[]  = { "Up top!", "Gimme five!", "Slap it!" };
static const char* const EMOTE_DANCE_CALL[] = { "Dance-off. Now.", "Watch THIS.", "Beat that!" };
static const char* const EMOTE_DANCE_BACK[] = { "Oh, it's ON.", "My turn!", "Amateur." };
static const char* const EMOTE_RPS_CALL[]   = { "Rock, paper...", "Best of one!", "Ready? Shoot!" };
static const char* const EMOTE_SNOW_CALL[]  = { "Think fast!", "Heads up!", "Incoming!" };
static const char* const EMOTE_SNOW_BACK[]  = { "Oh, you're DONE.", "Payback!", "Take THAT!" };
static const char* const EMOTE_BOO[]        = { "BOO!", "BOO!!", "RAAWR!" };
static const char* const EMOTE_SCARED[]     = { "AAAH!", "Not funny!", "My FUR!" };
#define EMOTE_PICK(a) (a[s_exchange % (sizeof(a) / sizeof(a[0]))])

// ---- the shared scare -----------------------------------------------------
// The host reacts to a detection through his mood machine; the guest gets
// the same pose for the same time, and a word about it.
//
// The one place this would seem to miss is the ALERT screen, which takes the
// whole display for a new detection. It does not miss it: ALERT fires the
// host's DETECTION reaction on its way OUT -- tap to dismiss, or its timeout --
// right before coming back here, so the pair of them react on arrival. An
// "aftershock" on top of that was tried and was a second flinch 350 ms later
// that restarted the first one's double-take mid-swing.
static uint32_t s_seenShock = 0, s_guestStartleUntil = 0;

// ---- napping, and why there isn't any here ----------------------------------
// Squachy used to doze off mid-visit: four quiet minutes and the two of them
// would nod off together until something woke them. It is gone deliberately.
// A visitor is the one time there is banter to be had, and sleeping through it
// is the opposite of the point -- so a guest on screen now means he stays
// awake for as long as they are there.
//
// He still naps ALONE. That is a separate mechanism living in squachy.cpp, on
// its own ten-minute idle timer with its own sleepy lines, and nothing here
// touches it.

static bool messageShowing(uint32_t now);         // with the message UI, below

// What the two of them get up to on their own. It used to be a dance and
// rock-paper-scissors, forever, with a snowball fight when it snowed; the
// other thirty-odd emotes only ever played when somebody pressed a button.
// Now the idle clock draws from the lot, minus the ones that need a reason
// (SPOTTED wants a recent catch, SAD and GRR and HEART are answers to
// something, WAVE is the hello they already did). Snow keeps its
// snowballs, every other time. The last six are kept out of the hat so a
// visit does not repeat itself inside ten minutes.
static MeshMsg::Emote idlePick() {
    using E = MeshMsg::Emote;
    static const E POOL[] = {
        E::HIGH_FIVE, E::DANCE, E::RPS, E::BOO,
        E::FIST_BUMP, E::HANDSHAKE, E::SALUTE, E::BOW, E::HUG,
        E::COIN, E::DICE, E::ARM_WRESTLE, E::TUG, E::LEAPFROG,
        E::PIE, E::BALLOON, E::PLANE, E::PILLOW,
        E::GIFT, E::SNACK, E::CHEERS, E::CONFETTI, E::FIREWORKS,
        E::LAUGH, E::SLEEPY, E::TINFOIL, E::CAMERA, E::HOWL, E::SELFIE,
    };
    static const uint8_t N = sizeof POOL / sizeof POOL[0];
    static E       recent[6] = { E::COUNT, E::COUNT, E::COUNT, E::COUNT, E::COUNT, E::COUNT };
    static uint8_t ri = 0;
    const uint8_t n = s_pieceNo++;
    E pick;
    if (Settings::background() == Settings::Background::SNOWFALL && (n % 2 == 0)) {
        pick = E::SNOWBALL;
    } else {
        for (uint8_t tries = 0; tries < 12; tries++) {
            pick = POOL[random(0, N)];
            bool seen = false;
            for (uint8_t i = 0; i < 6; i++) if (recent[i] == pick) seen = true;
            if (!seen) break;
        }
        recent[ri] = pick;
        ri = (uint8_t)((ri + 1) % 6);
    }
    return pick;
}

// Whose clock counts. Two boards both starting pieces at each other every
// minute or so would keep interrupting one another, so when both hold the
// phrase the lower address starts them and the other one just joins in.
// A board that cannot send -- messages off, no phrase -- plays to itself,
// which is what every board did before.
static bool idleLeader(uint32_t now) {
    if (!MeshTalk::ready()) return false;
    const uint8_t* pm = Mesh::peerMac();
    if (!pm || !MeshTalk::inSquad(pm, now)) return true;      // nobody to hand it to
    return memcmp(MeshTalk::ownMac(), pm, 6) < 0;
}
static bool idleFollower(uint32_t now) {
    const uint8_t* pm = Mesh::peerMac();
    return MeshTalk::ready() && pm && MeshTalk::inSquad(pm, now) && !idleLeader(now);
}

static uint32_t pieceGap() {
    uint32_t g = PIECE_EVERY_MS + (uint32_t)random(0, PIECE_JITTER_MS);
    if (Settings::background() == Settings::Background::SYNTHWAVE) g /= 2;
    // The follower's own clock runs slow: it is there for the case where the
    // leader has wandered off into a menu, not to race him.
    if (idleFollower(millis())) g *= 2;
    // BANTER: the set pieces come as often as the idle lines do.
    g = (uint32_t)(g * Settings::banterScale());
    return g;
}

// guestFirst: an emote from the visitor's board -- he starts it, not the host.
static void pieceBegin(uint32_t now, Piece p, bool guestFirst) {
    s_piece           = p;
    s_pieceAt         = now;
    s_pieceStep       = 0;
    s_pieceSub        = 0xFF;
    s_pieceGuestFirst = guestFirst;
    s_guestTurn       = guestFirst;
    s_visitGuestLine  = nullptr;
    switch (s_piece) {
        case Piece::DANCE:
            if (guestFirst) {
                s_visitGuestLine = EMOTE_PICK(EMOTE_DANCE_CALL);
            } else {
                Squachy::visitDanceCall(s_exchange);          // throws down...
                Squachy::visitDance(now, DANCE_SEG_MS);       // ...and goes first
            }
            Serial.println("[visit] dance-off");
            break;
        case Piece::RPS:
            // Rolled here for the clock's; an emote's throws come with it and
            // are set by emoteStart() straight after.
            s_rpsHost  = (uint8_t)random(0, 3);
            s_rpsGuest = (uint8_t)random(0, 3);
            if (guestFirst) s_visitGuestLine = EMOTE_PICK(EMOTE_RPS_CALL);
            else            Squachy::visitRpsCall(s_exchange);
            Squachy::visitPump(now, RPS_PUMP_MS);
            Serial.println("[visit] rock paper scissors");
            break;
        case Piece::SNOW:
            s_snowDone = 0;
            s_puffAt   = 0;
            if (guestFirst) {
                s_visitGuestLine = EMOTE_PICK(EMOTE_SNOW_CALL);
            } else {
                Squachy::visitSnowCall(s_exchange);
                Squachy::visitReach(now, SNOW_WIND_MS, Squachy::Reach::UP);   // winding up
            }
            Serial.println("[visit] snowball fight");
            break;
        case Piece::WAVE:
            if (guestFirst) s_visitGuestLine = EMOTE_PICK(EMOTE_WAVE_CALL);
            else            Squachy::visitSay(EMOTE_PICK(EMOTE_WAVE_CALL));
            Serial.println("[visit] wave");
            break;
        case Piece::FIVE:
            if (guestFirst) s_visitGuestLine = EMOTE_PICK(EMOTE_FIVE_CALL);
            else            Squachy::visitSay(EMOTE_PICK(EMOTE_FIVE_CALL));
            Serial.println("[visit] high five (emote)");
            break;
        case Piece::BOO:
            if (guestFirst) {
                s_visitGuestLine = EMOTE_PICK(EMOTE_BOO);
            } else {
                Squachy::visitSay(EMOTE_PICK(EMOTE_BOO));
                Squachy::visitReach(now, BOO_JUMP_MS + 300, Squachy::Reach::UP);   // arms up
            }
            Serial.println("[visit] boo");
            break;
        default: break;
    }
}

// The idle clock fires: pick one, roll it, and put it on the air the way the
// emote picker does, so the other board acts out the same piece with the
// same result. Queued rather than begun here, because emoteStart() is what
// knows how to play every kind; the loop picks it up on its next pass.
static void pieceStart(uint32_t now) {
    const MeshMsg::Emote e = idlePick();
    const uint8_t setup = EmoteScript::roll(e, esp_random(), (uint8_t)Squachy::lastCaught());
    if (idleLeader(now)) MeshTalk::sendEmote((uint8_t)e, setup, now);   // best effort: busy air just means this one is ours alone
    uiClearEmote((uint8_t)e, setup, false);
    s_nextPieceAt = now + pieceGap();     // pieceEnd() sets the real one; this only stops a re-queue
}

static void pieceEnd(uint32_t now) {
    s_piece          = Piece::NONE;
    s_visitGuestLine = nullptr;
    s_guestTurn      = false;
    s_beatAt         = now;
    s_beatMs         = 900;
    s_exchange++;
    s_nextPieceAt    = now + pieceGap();
}

// ---- scripted emotes -------------------------------------------------------
// The thirty emotes after the originals are tables (emote_script.h), and this
// plays them: one beat at a time, each setting what the two of them do, who
// says what, and what flies about -- drawScriptFx() below draws that part.
static const EmoteScript::Script* s_script = nullptr;
static MeshMsg::Emote s_scriptEmote  = MeshMsg::Emote::WAVE;
static uint8_t  s_scriptSetup  = 0;
static int8_t   s_scriptBeat   = -1;
static uint32_t s_scriptBeatAt = 0;
static bool     s_scriptAHost  = true;        // A, the sender, is the host here
static EmoteScript::Result s_scriptResult   = EmoteScript::Result::NONE;
static Squachy::VisitPose  s_scriptGuestPose = Squachy::VisitPose::NONE;
// The lines built from the setup byte. Two, because the host's and the guest's
// can both be up at once, and a bubble keeps its pointer while it is.
static char     s_dynHost[24], s_dynGuest[24];
static const uint32_t SCRIPT_IN_MS = 650;     // in to the host, for the close ones

// EmoteScript::Pose, in its order, as the VisitPose that draws it.
static const Squachy::VisitPose POSE_MAP[] = {
    Squachy::VisitPose::NONE,      Squachy::VisitPose::LAUGH,
    Squachy::VisitPose::HIGH_FIVE, Squachy::VisitPose::LOW_FIVE, Squachy::VisitPose::FIST,
    Squachy::VisitPose::PUMP,      Squachy::VisitPose::DANCE,    Squachy::VisitPose::SLEEPY,
    Squachy::VisitPose::STRETCH,
    Squachy::VisitPose::COVER,     Squachy::VisitPose::LOOK_AROUND, Squachy::VisitPose::HANDS_UP,
    Squachy::VisitPose::SALUTE,    Squachy::VisitPose::BOW,      Squachy::VisitPose::HUG,
    Squachy::VisitPose::SAD,       Squachy::VisitPose::GRR,      Squachy::VisitPose::CROUCH,
    Squachy::VisitPose::PULL,      Squachy::VisitPose::WIGGLE,   Squachy::VisitPose::CHEER,
    Squachy::VisitPose::SELFIE,    Squachy::VisitPose::HOWL,     Squachy::VisitPose::POINT,
    Squachy::VisitPose::STRAIN,
};
static_assert(sizeof POSE_MAP / sizeof POSE_MAP[0] == (size_t)EmoteScript::Pose::COUNT,
              "a VisitPose for every script pose");

// Which beat `e` ms into the piece falls in, and how far into it; -1 once over.
static int8_t scriptBeatAt(uint32_t e, uint32_t& into) {
    uint32_t acc = 0;
    for (uint8_t i = 0; i < s_script->n; i++) {
        const uint32_t ms = s_script->beat[i].ms;
        if (e < acc + ms) { into = e - acc; return (int8_t)i; }
        acc += ms;
    }
    into = 0;
    return -1;
}

static void scriptBeat(uint8_t i, uint32_t now) {
    using namespace EmoteScript;
    const Beat& b = s_script->beat[i];
    s_scriptBeat   = (int8_t)i;
    s_scriptBeatAt = now;
    Pose pa = b.a, pb = b.b;
    bool aSpeaks = !(b.flags & B_SPEAKS);
    // A contest's beat is written winner-first; turn it round when the
    // receiver won, and make it a laugh for both when nobody did.
    if (b.flags & OUTCOME) {
        if (s_scriptResult == Result::TIE) {
            pa = pb = Pose::LAUGH;
        } else if (s_scriptResult == Result::RECEIVER) {
            const Pose t = pa; pa = pb; pb = t;
            aSpeaks = !aSpeaks;
        }
    }
    const Pose hp = s_scriptAHost ? pa : pb, gp = s_scriptAHost ? pb : pa;
    // A little past the beat, so his mood cannot lapse to idle for a frame
    // before the next one lands.
    Squachy::visitPose(now, b.ms + 150, POSE_MAP[(uint8_t)hp]);
    s_scriptGuestPose = (gp == Pose::LAUGH) ? Squachy::VisitPose::NONE : POSE_MAP[(uint8_t)gp];
    if (gp == Pose::LAUGH)   s_guestLaughUntil = now + b.ms;
    if (gp == Pose::STRETCH) Squachy::visitStretchClock(now);

    s_visitGuestLine = nullptr;
    s_guestTurn      = false;
    if (b.line) {
        const bool hostSays = (aSpeaks == s_scriptAHost);
        const char* text;
        if (isDyn(b.line)) {
            char* buf = hostSays ? s_dynHost : s_dynGuest;
            dynLine(b.line, s_scriptEmote, s_scriptSetup, buf, sizeof s_dynHost);
            text = buf;
        } else {
            text = line(b.line, (uint8_t)(s_exchange % VARIANTS));
        }
        if (text && hostSays) Squachy::visitSay(text);
        else if (text)      { s_visitGuestLine = text; s_guestTurn = true; }
    }
}

static void scriptStart(uint32_t now, MeshMsg::Emote e, uint8_t setup, bool guestFirst) {
    const EmoteScript::Script* s = EmoteScript::script(e);
    if (!s) return;
    pieceBegin(now, Piece::SCRIPT, guestFirst);
    s_script       = s;
    s_scriptEmote  = e;
    s_scriptSetup  = setup;
    s_scriptAHost  = (s->flags & EmoteScript::S_HOST_FIRST) ? true : !guestFirst;
    s_scriptResult = EmoteScript::outcome(e, setup);
    s_scriptGuestPose = Squachy::VisitPose::NONE;
    Serial.printf("[visit] %s %s\n", EmoteScript::name(e), EmoteScript::sub(e));
    scriptBeat(0, now);
}

static void scriptTick(uint32_t now) {
    uint32_t into;
    const int8_t i = s_script ? scriptBeatAt(now - s_pieceAt, into) : -1;
    if (i < 0) {
        s_scriptGuestPose = Squachy::VisitPose::NONE;
        pieceEnd(now);
        return;
    }
    if (i != s_scriptBeat) scriptBeat((uint8_t)i, now);
}

// Where the visitor stands during a script that moves him: in close and back
// out again, or leaping over the host and back. False when it does not.
static bool scriptGuestX(uint32_t now, int homeX, int meetX, float gs, int& x) {
    if (s_piece != Piece::SCRIPT || !s_script) return false;
    const uint32_t e = now - s_pieceAt;
    if (s_script->flags & EmoteScript::S_CLOSE) {
        const uint32_t total = EmoteScript::totalMs(*s_script);
        const uint32_t out   = s_script->beat[s_script->n - 1].ms;   // walks back through the last beat
        // Where two held-out hands meet, whatever the lean has done, then
        // however much nearer this one wants him.
        int m = meetX + hostLeanPx() - guestLeanPx() + (int)((float)s_script->closeUnits * gs);
        // In portrait the two already stand closer than arm's reach, and
        // "in to the host" worked out as a step AWAY from him, off the edge.
        if (m > homeX) m = homeX;
        float k;
        if (e < SCRIPT_IN_MS)    k = (float)e / (float)SCRIPT_IN_MS;
        else if (e + out < total) k = 1.0f;
        else {
            k = 1.0f - (float)(e + out - total) / (float)out;
            if (k < 0.0f) k = 0.0f;
        }
        k = k * k * (3.0f - 2.0f * k);
        x = homeX + (int)((float)(m - homeX) * k);
        return true;
    }
    if (s_script->flags & EmoteScript::S_LEAP) {
        uint32_t into;
        const int8_t i = scriptBeatAt(e, into);
        // Past the host on his far side. The screen is only so wide, so on a
        // landscape board he lands overlapping him a little, and on the
        // narrowest he may clip the edge -- for the half second he is there.
        const int hostX = meetX - (int)(REACH_K * gs);
        int land = hostX - (int)(44.0f * gs);
        if (land < (int)(12.0f * gs)) land = (int)(12.0f * gs);
        if (i == 1 || i == 3) {
            const float k = (float)into / (float)s_script->beat[i].ms;
            x = (i == 1) ? homeX + (int)((float)(land - homeX) * k)
                         : land + (int)((float)(homeX - land) * k);
        } else {
            x = (i == 2) ? land : homeX;
        }
        return true;
    }
    return false;
}

// How much taller the visitor's head is this beat: a tinfoil hat, which his
// bubble and his name have to clear.
static int scriptHatPx(float gs) {
    if (s_piece != Piece::SCRIPT || !s_script || s_scriptBeat < 0) return 0;
    const EmoteScript::Beat& b = s_script->beat[s_scriptBeat];
    if (b.fx != EmoteScript::Fx::HATS) return 0;
    // arg 0 is only A's; the visitor is A when the host is not.
    return (b.fxArg == 1 || !s_scriptAHost) ? (int)(16.0f * gs) : 0;
}

// How high the visitor is off the ground: only ever mid-leap. Enough to clear
// a crouching host's crest, and never so much his head leaves the screen --
// `ceiling` is the most the caller has room for.
static int scriptGuestLift(uint32_t now, float gs, int ceiling) {
    if (s_piece != Piece::SCRIPT || !s_script || !(s_script->flags & EmoteScript::S_LEAP)) return 0;
    uint32_t into;
    const int8_t i = scriptBeatAt(now - s_pieceAt, into);
    if (i != 1 && i != 3) return 0;
    const float k = (float)into / (float)s_script->beat[i].ms;
    int lift = (int)(54.0f * gs);
    if (lift > ceiling) lift = ceiling;
    return (int)(sinf(k * 3.14159265f) * (float)lift);
}

static bool scriptWalking(uint32_t now) {
    if (s_piece != Piece::SCRIPT || !s_script || !(s_script->flags & EmoteScript::S_CLOSE)) return false;
    const uint32_t e = now - s_pieceAt, total = EmoteScript::totalMs(*s_script);
    return e < SCRIPT_IN_MS || e + s_script->beat[s_script->n - 1].ms >= total;
}

static void pieceTick(uint32_t now) {
    const uint32_t e = now - s_pieceAt;
    switch (s_piece) {
    case Piece::DANCE: {
        const uint8_t step = (uint8_t)(e / DANCE_SEG_MS);
        if (step == s_pieceStep) return;
        s_pieceStep = step;
        if (step == 1) {                              // the other one answers it
            if (s_pieceGuestFirst) {
                s_guestTurn      = false;
                s_visitGuestLine = nullptr;
                Squachy::visitSay(EMOTE_PICK(EMOTE_DANCE_BACK));
                Squachy::visitDance(now, DANCE_SEG_MS);
            } else {
                s_guestTurn      = true;
                s_visitGuestLine = Squachy::visitDanceReply(s_exchange);
            }
        } else if (step == 2) {                       // then both at once
            s_guestTurn      = false;
            s_visitGuestLine = nullptr;
            Squachy::visitDance(now, DANCE_SEG_MS);
        } else {                                      // they crack up; back to talking
            Squachy::visitLaugh(now);
            s_guestLaughUntil = now + 1500;
            pieceEnd(now);
        }
        return;
    }
    case Piece::RPS: {
        const uint8_t step = e < RPS_PUMP_MS ? 0
                           : e < RPS_PUMP_MS + RPS_SHOW_MS ? 1
                           : e < RPS_PUMP_MS + RPS_SHOW_MS + RPS_REACT_MS ? 2 : 3;
        if (step == s_pieceStep) return;
        s_pieceStep = step;
        // From his side: paper beats rock, rock beats scissors, scissors paper.
        const uint8_t outcome = (uint8_t)((s_rpsHost + 3 - s_rpsGuest) % 3);   // 0 tie, 1 won, 2 lost
        if (step == 1) {                              // the reveal: hands out
            Squachy::visitReach(now, RPS_SHOW_MS, Squachy::Reach::LEVEL);
            Serial.printf("[visit] rps %u vs %u\n", (unsigned)s_rpsHost, (unsigned)s_rpsGuest);
        } else if (step == 2) {                       // and who is pleased about it
            Squachy::visitRpsResult(outcome, s_exchange);
            if (outcome != 2) Squachy::visitLaugh(now);            // won, or drew
            if (outcome != 1) s_guestLaughUntil = now + RPS_REACT_MS;
        } else {
            pieceEnd(now);
        }
        return;
    }
    case Piece::SNOW: {
        const uint8_t  step = (uint8_t)(e / SNOW_SEG_MS);
        const uint32_t se   = e % SNOW_SEG_MS;
        if (step != s_pieceStep) {
            s_pieceStep = step;
            if (step == 1) {                          // the other one throws back
                if (s_pieceGuestFirst) {
                    s_guestTurn      = false;
                    s_visitGuestLine = nullptr;
                    Squachy::visitSay(EMOTE_PICK(EMOTE_SNOW_BACK));
                    Squachy::visitReach(now, SNOW_WIND_MS, Squachy::Reach::UP);
                } else {
                    s_guestTurn      = true;
                    s_visitGuestLine = Squachy::visitSnowReply(s_exchange);
                }
            } else if (step == 2) {                   // both of them laughing it off
                s_guestTurn      = false;
                s_visitGuestLine = nullptr;
                Squachy::visitLaugh(now);
                s_guestLaughUntil = now + 1500;
            } else if (step >= 3) {
                pieceEnd(now);
                return;
            }
        }
        // The release and each hit land inside a turn, not on its edge. Which
        // of them throws on which turn depends on who started it.
        if (step <= 1) {
            const bool    host = (step == 0) != s_pieceGuestFirst;
            const uint8_t rel  = step ? 4 : 1, hit = step ? 8 : 2;
            if (host && se >= SNOW_WIND_MS && !(s_snowDone & rel)) {
                s_snowDone |= rel;
                Squachy::visitReach(now, 300, Squachy::Reach::LEVEL);    // and let go
            }
            if (se >= SNOW_WIND_MS + SNOW_FLY_MS && !(s_snowDone & hit)) {
                s_snowDone |= hit;
                if (host) {                                            // got him
                    s_guestStartleUntil = now + 450;
                    s_puffAt = now; s_puffGuest = true;
                } else {                                               // got HIM
                    Squachy::visitLaugh(now);
                    s_puffAt = now; s_puffGuest = false;
                }
            }
        }
        return;
    }
    case Piece::WAVE: {
        // His hand going up and down the whole time: a wave is a reach, repeated.
        const uint8_t sub  = (uint8_t)(e / 300);
        const uint8_t step = (uint8_t)(e / WAVE_SEG_MS);
        if (step < 2 && sub != s_pieceSub) {
            s_pieceSub = sub;
            Squachy::visitReach(now, 300, (sub & 1) ? Squachy::Reach::LEVEL : Squachy::Reach::UP);
        }
        if (step == s_pieceStep) return;
        s_pieceStep = step;
        if (step == 1) {                              // and the wave back
            if (s_pieceGuestFirst) {
                s_guestTurn      = false;
                s_visitGuestLine = nullptr;
                Squachy::visitSay(EMOTE_PICK(EMOTE_WAVE_BACK));
            } else {
                s_guestTurn      = true;
                s_visitGuestLine = EMOTE_PICK(EMOTE_WAVE_BACK);
            }
        } else if (step >= 2) {
            Squachy::visitLaugh(now);
            s_guestLaughUntil = now + 900;
            pieceEnd(now);
        }
        return;
    }
    case Piece::FIVE: {
        // In to the host, the slap, and back to his spot -- fiveGuestX() moves
        // him; this is the host's arm and the laugh after.
        if (s_pieceStep == 0 && e >= FIVE_IN_MS) {
            s_pieceStep = 1;
            Squachy::visitReach(now, FIVE_MS, Squachy::Reach::UP);
        }
        if (s_pieceStep == 1 && e >= FIVE_IN_MS + FIVE_MS) {
            s_pieceStep = 2;
            Squachy::visitLaugh(now);
            s_guestLaughUntil = now + 900;
        }
        if (s_pieceStep == 2 && e >= FIVE_IN_MS + FIVE_MS + STEP_MS) pieceEnd(now);
        return;
    }
    case Piece::SCRIPT:
        scriptTick(now);
        return;
    case Piece::BOO: {
        const uint8_t step = e < BOO_JUMP_MS ? 0 : (e < BOO_MS ? 1 : 2);
        if (step == s_pieceStep) return;
        s_pieceStep = step;
        if (step == 1) {                              // and the other one jumps out of his fur
            if (s_pieceGuestFirst) {
                Squachy::visitSay(EMOTE_PICK(EMOTE_SCARED));
                Squachy::visitReach(now, BOO_MS - BOO_JUMP_MS, Squachy::Reach::UP);   // hands up
            } else {
                s_guestStartleUntil = now + 1100;
                s_guestTurn         = true;
                s_visitGuestLine    = EMOTE_PICK(EMOTE_SCARED);
            }
        } else {
            Squachy::visitLaugh(now);
            s_guestLaughUntil = now + 1200;
            pieceEnd(now);
        }
        return;
    }
    default:
        s_piece = Piece::NONE;
        return;
    }
}

// Where the visitor stands during an emote high five: in to the host, where
// the hands meet whatever the lean has done, and back. False the rest of the
// time, when visitGuestX() decides.
static bool fiveGuestX(uint32_t now, int homeX, int meetX, int closePx, int& x) {
    if (s_piece != Piece::FIVE) return false;
    const uint32_t e = now - s_pieceAt;
    // The caller adds guestLeanPx() afterwards; taking it off here puts him
    // exactly where the host's reach ends.
    int m = meetX + hostLeanPx() - guestLeanPx();
    if (m > homeX) m = homeX;          // portrait: never a step away from him
    float k;
    if (e < FIVE_IN_MS) {
        k = (float)e / (float)FIVE_IN_MS;
        k = k * k * (3.0f - 2.0f * k);
        x = homeX + (int)((float)(m + closePx - homeX) * k);
    } else if (e < FIVE_IN_MS + FIVE_HIT_MS) {
        k = (float)(e - FIVE_IN_MS) / (float)FIVE_HIT_MS;
        x = m + (int)((float)closePx * (1.0f - k));
    } else if (e < FIVE_IN_MS + FIVE_MS) {
        x = m;
    } else {
        k = (float)(e - FIVE_IN_MS - FIVE_MS) / (float)STEP_MS;
        if (k > 1.0f) k = 1.0f;
        k = k * k * (3.0f - 2.0f * k);
        x = m + (int)((float)(homeX - m) * k);
    }
    return true;
}

static bool fiveWalking(uint32_t now) {
    if (s_piece != Piece::FIVE) return false;
    const uint32_t e = now - s_pieceAt;
    return e < FIVE_IN_MS || e >= FIVE_IN_MS + FIVE_MS;
}

bool uiClearEmote(uint8_t emote, uint8_t setup, bool fromGuest) {
    if (s_vp == VisitPhase::GONE || s_vp == VisitPhase::LEAVING) return false;
    // An emote is two Squachys acting something out at arm's length from each
    // other. With a crowd drifting about the screen there are no such two, so
    // the compose screen says so rather than playing it to nobody.
    //
    // What matters is whether a crowd is ACTUALLY ON SCREEN, not what the
    // setting says. This tested the setting, and the crowd only draws at two
    // or more peers -- so a board set to UP TO 8 with one visitor was running
    // an ordinary visit and silently refusing every emote sent to it.
    if (s_crowdDrawn) return false;
    if (emote >= (uint8_t)MeshMsg::Emote::COUNT) return false;
    if (s_eqN == EMOTE_Q_N) {
        // Full: the oldest goes, and says so. Silently dropping one is what
        // made this look like emotes simply did not work.
        Serial.printf("[visit] emote %u dropped: %u already waiting\n",
                      (unsigned)s_eq[0].emote, (unsigned)EMOTE_Q_N);
        for (uint8_t i = 1; i < s_eqN; i++) s_eq[i - 1] = s_eq[i];
        s_eqN--;
    }
    s_eq[s_eqN].emote = emote;
    s_eq[s_eqN].setup = setup;
    s_eq[s_eqN].guest = fromGuest;
    s_eq[s_eqN].at    = millis();
    s_eqN++;
    return true;
}

static void emoteStart(uint32_t now) {
    if (!s_eqN) return;
    const uint8_t b = s_eq[0].emote, setup = s_eq[0].setup;
    const bool    g = s_eq[0].guest;
    for (uint8_t i = 1; i < s_eqN; i++) s_eq[i - 1] = s_eq[i];
    s_eqN--;
    // A question left hanging when the button was pressed is dropped rather
    // than answered after the piece, when nobody remembers it.
    s_hangStep = 0;
    Serial.printf("[visit] emote %u (setup %u) from %s\n", (unsigned)b, (unsigned)setup,
                  g ? "the visitor" : "us");
    switch ((MeshMsg::Emote)b) {
        case MeshMsg::Emote::WAVE:      pieceBegin(now, Piece::WAVE,  g); break;
        case MeshMsg::Emote::HIGH_FIVE: pieceBegin(now, Piece::FIVE,  g); break;
        case MeshMsg::Emote::DANCE:     pieceBegin(now, Piece::DANCE, g); break;
        case MeshMsg::Emote::SNOWBALL:  pieceBegin(now, Piece::SNOW,  g); break;
        case MeshMsg::Emote::BOO:       pieceBegin(now, Piece::BOO,   g); break;
        case MeshMsg::Emote::RPS: {
            pieceBegin(now, Piece::RPS, g);
            // The sender's throw, then the receiver's: the same game on both
            // boards, each from its own side.
            const uint8_t a = (uint8_t)((setup / 3) % 3), c = (uint8_t)(setup % 3);
            s_rpsHost  = g ? c : a;
            s_rpsGuest = g ? a : c;
            break;
        }
        default:
            scriptStart(now, (MeshMsg::Emote)b, setup, g);
            break;
    }
}

// Whether the visitor is saying hello: walking in, through the hellos,
// walking out, or waved at. He waves then, and only then.
static bool guestGreeting() {
    return s_vp == VisitPhase::ARRIVING || s_vp == VisitPhase::MEETING ||
           s_vp == VisitPhase::LEAVING || s_piece == Piece::WAVE;
}

// What the guest is doing this frame, beyond standing and talking.
static Squachy::VisitPose guestPose(uint32_t now) {
    typedef Squachy::VisitPose P;
    if (s_vp == VisitPhase::HIGH_FIVE) {
        // The handshake's three slaps are high, low, then a fist bump.
        const uint32_t st = (now - s_vpAt) / s_fiveStepMs;
        return (s_fiveSteps < 3 || st == 0) ? P::HIGH_FIVE : (st == 1 ? P::LOW_FIVE : P::FIST);
    }
    if ((int32_t)(s_guestStartleUntil - now) > 0)           return P::STARTLED;
    const uint32_t e = now - s_pieceAt;
    switch (s_piece) {
        // Whoever sent an emote goes first: the guest dances on turns 0 and 2
        // when it came from him, on 1 and 2 otherwise.
        case Piece::DANCE: return (s_pieceGuestFirst ? s_pieceStep != 1 : s_pieceStep >= 1) ? P::DANCE : P::NONE;
        case Piece::RPS:   return s_pieceStep == 0 ? P::PUMP : (s_pieceStep == 1 ? P::FIST : P::NONE);
        case Piece::FIVE:  return (e >= FIVE_IN_MS && e < FIVE_IN_MS + FIVE_MS) ? P::HIGH_FIVE : P::NONE;
        case Piece::BOO:   return (s_pieceGuestFirst && s_pieceStep == 0) ? P::HIGH_FIVE : P::NONE;
        case Piece::SCRIPT: return s_scriptGuestPose;
        case Piece::SNOW: {
            if (e / SNOW_SEG_MS != (s_pieceGuestFirst ? 0u : 1u)) return P::NONE;
            const uint32_t se = e % SNOW_SEG_MS;
            if (se < SNOW_WIND_MS)       return P::HIGH_FIVE;   // winding up
            if (se < SNOW_WIND_MS + 300) return P::FIST;        // and letting go
            return P::NONE;
        }
        default: return P::NONE;
    }
}

// A slap needs a spark: eight short rays and a hot centre, opening out over
// the flash.
static void drawSpark(TFT_eSPI& t, int x, int y, float k, float scale) {
    const float r0 = (2.0f + 3.0f * k) * scale, r1 = (5.0f + 9.0f * k) * scale;
    for (int i = 0; i < 8; i++) {
        const float a = (float)i * 0.78539816f;
        const float cx = cosf(a), sy = sinf(a);
        t.drawLine(x + (int)(cx * r0), y + (int)(sy * r0), x + (int)(cx * r1), y + (int)(sy * r1),
                   (i % 2) ? Theme::WHITE : Theme::VAPOR_YELLOW);
    }
    t.fillCircle(x, y, (int)(2.0f * scale) + 1, Theme::WHITE);
}

// Rock, paper or scissors, as a small icon over a held-out hand. Outlined in
// the shadow grey so it reads over snow, fire and synthwave alike.
static void drawRps(TFT_eSPI& t, int x, int y, uint8_t kind, float s) {
    const int u = (int)(s * 5.0f) + 2;
    if (kind == 0) {                                  // rock: a fist
        t.fillCircle(x, y, u + 1, Theme::W95_SHADOW);
        t.fillCircle(x, y, u, Theme::W95_LIGHT);
        t.drawFastHLine(x - u / 2, y - u / 3, u, Theme::W95_SHADOW);
    } else if (kind == 1) {                           // paper: a sheet
        t.fillRect(x - u - 1, y - u - 3, 2 * u + 2, 2 * u + 6, Theme::W95_SHADOW);
        t.fillRect(x - u, y - u - 2, 2 * u, 2 * u + 4, Theme::WHITE);
        for (int i = 0; i < 3; i++)
            t.drawFastHLine(x - u + 2, y - u + 1 + i * (u / 2 + 1), 2 * u - 4, Theme::W95_SHADOW);
    } else {                                          // scissors: two blades crossed
        t.drawWideLine(x - u, y - u - 2, x + u / 2, y + u / 2, 2, Theme::WHITE);
        t.drawWideLine(x + u, y - u - 2, x - u / 2, y + u / 2, 2, Theme::WHITE);
        t.drawCircle(x - u / 2, y + u / 2 + 2, u / 3 + 1, Theme::VAPOR_PINK);
        t.drawCircle(x + u / 2, y + u / 2 + 2, u / 3 + 1, Theme::VAPOR_PINK);
    }
}


// ---- what the scripted emotes draw ------------------------------------------
// Everything below is shapes: the frame is a sprite, and a prop is only ever a
// few of them. The particle effects are capped at two dozen apiece, which is
// what keeps them inside the frame budget on the slowest backgrounds.
static void fxHeart(TFT_eSPI& t, int x, int y, int r, uint16_t c) {
    t.fillCircle(x - r / 2, y, r / 2 + 1, c);
    t.fillCircle(x + r / 2, y, r / 2 + 1, c);
    t.fillTriangle(x - r - 1, y + 1, x + r + 1, y + 1, x, y + r + 2, c);
}

static void fxObj(TFT_eSPI& t, EmoteScript::Obj o, int x, int y, float s, int dir) {
    auto U = [s](float v) { const int r = (int)(v * s); return r < 1 ? 1 : r; };
    switch (o) {
    case EmoteScript::Obj::PIE:
        t.fillEllipse(x, y + U(2), U(7), U(3), Theme::W95_SHADOW);
        t.fillEllipse(x, y, U(6), U(3), Theme::WHITE);
        t.fillCircle(x, y - U(2), U(1.5f), Theme::RED);
        break;
    case EmoteScript::Obj::BALLOON:
        t.fillCircle(x, y, U(4.5f), Theme::VAPOR_BLUE);
        t.fillCircle(x - U(1.5f), y - U(1.5f), U(1), Theme::WHITE);
        t.fillTriangle(x - U(1), y + U(5), x + U(1), y + U(5), x, y + U(3), Theme::VAPOR_BLUE);
        break;
    case EmoteScript::Obj::PLANE:
        t.fillTriangle(x + dir * U(7), y, x - dir * U(5), y - U(3), x - dir * U(5), y + U(3), Theme::WHITE);
        t.drawLine(x + dir * U(7), y, x - dir * U(5), y + U(1), Theme::W95_SHADOW);
        break;
    case EmoteScript::Obj::PILLOW:
        t.fillRoundRect(x - U(7), y - U(4), U(14), U(9), U(3), Theme::W95_SHADOW);
        t.fillRoundRect(x - U(6), y - U(4), U(12), U(8), U(3), Theme::WHITE);
        break;
    case EmoteScript::Obj::GIFT:
        t.fillRect(x - U(5), y - U(4), U(10), U(9), Theme::VAPOR_PINK);
        t.fillRect(x - U(1), y - U(4), U(2), U(9), Theme::VAPOR_YELLOW);
        t.fillRect(x - U(5), y - U(1), U(10), U(2), Theme::VAPOR_YELLOW);
        t.fillCircle(x - U(2), y - U(5), U(1.5f), Theme::VAPOR_YELLOW);
        t.fillCircle(x + U(2), y - U(5), U(1.5f), Theme::VAPOR_YELLOW);
        break;
    case EmoteScript::Obj::PIZZA:
        t.fillTriangle(x - U(5), y - U(4), x + U(5), y - U(4), x, y + U(6), Theme::VAPOR_YELLOW);
        t.fillRect(x - U(5), y - U(5), U(10), U(2), Theme::AMBER);
        t.fillCircle(x - U(1), y - U(1), U(1), Theme::RED);
        t.fillCircle(x + U(2), y + U(1), U(1), Theme::RED);
        break;
    default: break;
    }
}

static void fxDie(TFT_eSPI& t, int x, int y, uint8_t v, float s) {
    const int h = (int)(5.0f * s) + 1, d = (int)(2.6f * s) + 1, r = s > 1.2f ? 2 : 1;
    t.fillRoundRect(x - h, y - h, 2 * h, 2 * h, 2, Theme::WHITE);
    auto pip = [&](int dx, int dy) { t.fillCircle(x + dx, y + dy, r, Theme::BLACK); };
    if (v & 1) pip(0, 0);                                     // 1, 3, 5
    if (v >= 2) { pip(-d, -d); pip(d, d); }
    if (v >= 4) { pip(d, -d); pip(-d, d); }
    if (v == 6) { pip(-d, 0); pip(d, 0); }
}

// A small five-pointed star, for BONK: a plus and an X.
static void fxStar(TFT_eSPI& t, int x, int y, int r, uint16_t c) {
    t.drawFastHLine(x - r, y, 2 * r + 1, c);
    t.drawFastVLine(x, y - r, 2 * r + 1, c);
    t.drawLine(x - r + 1, y - r + 1, x + r - 1, y + r - 1, c);
    t.drawLine(x - r + 1, y + r - 1, x + r - 1, y - r + 1, c);
}

// The guest's head bobs -- drawWaving() does it off the clock -- and a hat that
// did not bob with it would float. Same curve, same numbers.
static int guestBob(uint32_t now, float gs) {
    const bool laugh = now < s_guestLaughUntil;
    const uint32_t per = laugh ? 240u : 900u;
    return (int)(sinf((float)(now % per) / (float)per * 6.2831853f) * (laugh ? 7.0f : 6.0f) * gs);
}

static void drawScriptFx(TFT_eSPI& t, uint32_t now, int hx, int gx, int headTop, float gs) {
    using namespace EmoteScript;
    if (!s_script || s_scriptBeat < 0) return;
    const Beat& b = s_script->beat[s_scriptBeat];
    const uint32_t be = now - s_scriptBeatAt;
    float k = (float)be / (float)b.ms;
    if (k > 1.0f) k = 1.0f;
    auto U = [gs](float v) { return (int)(v * gs); };
    // A sent it; the effect starts from its actor and goes to the other one.
    const int  ax = s_scriptAHost ? hx : gx, bx = s_scriptAHost ? gx : hx;
    const bool fromB = (b.flags & FX_FROM_B) != 0;
    const int  cx = fromB ? bx : ax, ox = fromB ? ax : bx;
    const int  dir = (ox > cx) ? 1 : -1;
    const int  handX = cx + dir * U(30), handY = headTop + U(22), oHandX = ox - dir * U(30);
    const int  headY = headTop + U(12), groundY = headTop + U(54);
    const int  midX = (hx + gx) / 2;
    const uint8_t arg = b.fxArg;
    const uint16_t CONF[5] = { Theme::VAPOR_PINK, Theme::CYAN, Theme::VAPOR_YELLOW,
                               Theme::GREEN, Theme::VAPOR_PURPLE };
    t.setTextSize(1);
    switch (b.fx) {
    case Fx::SPARK: {
        // As the hands arrive, whichever height they meet at.
        const int32_t s0 = (int32_t)b.ms - 420;
        if ((int32_t)be >= s0 && (int32_t)be < s0 + 320)
            drawSpark(t, hx + U(REACH_K * 0.5f), headTop + U(REACH_Y[arg % 3]),
                      (float)((int32_t)be - s0) / 320.0f, gs);
        break;
    }
    case Fx::THROW: {
        const Obj o = (Obj)arg;
        if (k >= 1.0f) break;
        const int x = handX + (int)((float)(ox - handX) * k);
        int y = handY - U(4) + (int)((float)(headY - handY + U(4)) * k);
        if (o == Obj::PLANE) y -= (int)(sinf(k * 3.14159265f) * U(8)) + (int)(sinf(k * 12.566f) * U(3));
        else                 y -= (int)(sinf(k * 3.14159265f) * U(26));
        fxObj(t, o, x, y, gs, dir);
        break;
    }
    case Fx::LOB: {
        const int x = handX + (int)((float)(oHandX - handX) * k);
        const int y = handY - U(5) - (int)(sinf(k * 3.14159265f) * U(18));
        fxObj(t, (Obj)arg, x, y, gs, dir);
        break;
    }
    case Fx::HOLD:
        fxObj(t, (Obj)arg, handX, handY - U(5), gs, dir);
        break;
    case Fx::HIT: {
        const Burst kind = (Burst)arg;
        const int tx = ox, ty = headY;
        if (kind == Burst::CREAM) {
            // A pie's worth on his face, sliding slowly off it.
            const int dy = (int)(k * U(3));
            t.fillEllipse(tx, ty + U(3) + dy, U(11), U(8), Theme::WHITE);
            t.fillCircle(tx - U(8), ty + U(10) + dy, U(2), Theme::WHITE);
            t.fillCircle(tx + U(6), ty + U(11) + dy, U(2), Theme::WHITE);
            t.fillCircle(tx + U(2), ty - U(2) + dy, U(1.5f) + 1, Theme::RED);
            break;
        }
        const uint32_t burstMs = (kind == Burst::FEATHERS) ? b.ms : 450;
        if (be >= burstMs) break;
        const float q = (float)be / (float)burstMs;
        const int n = (kind == Burst::CONFETTI) ? 16 : 10;
        for (int i = 0; i < n; i++) {
            const float a = (float)i / (float)n * 6.2831853f + (float)i * 0.37f;
            if (kind == Burst::FEATHERS) {
                // Out, then drifting down and swaying.
                const int fx = tx + (int)(cosf(a) * (float)U(8 + 10.0f * q)) + (int)(sinf(q * 9.0f + i) * U(3));
                const int fy = ty + (int)(sinf(a) * U(6)) + (int)(q * U(30));
                t.drawLine(fx - U(2), fy, fx + U(2), fy - U(1), Theme::WHITE);
            } else {
                const float r = (float)U(4) + q * (float)U(16);
                const int px = tx + (int)(cosf(a) * r);
                const int py = ty + (int)(sinf(a) * r) + (kind == Burst::SPLASH ? (int)(q * q * U(14)) : 0);
                if (kind == Burst::CONFETTI) t.fillRect(px, py, U(2) + 1, U(1) + 1, CONF[i % 5]);
                else t.fillCircle(px, py, 1 + (int)gs, kind == Burst::SPLASH ? Theme::VAPOR_BLUE : Theme::WHITE);
            }
        }
        break;
    }
    case Fx::HEARTS:
        for (int i = 0; i < 4; i++) {
            const float p = (float)((be + (uint32_t)i * 300u) % 1200u) / 1200.0f;
            // Across between their faces, below where either bubble goes.
            const int x = cx + (int)((float)(ox - cx) * p);
            const int y = headTop + U(12) - (int)(sinf(p * 3.14159265f) * U(12));
            fxHeart(t, x, y, U(3) + 1, (i & 1) ? Theme::PINK : Theme::VAPOR_PINK);
        }
        break;
    case Fx::STEAM:
        for (int i = 0; i < 4; i++) {
            const float p = (float)((be + (uint32_t)i * 250u) % 1000u) / 1000.0f;
            const int side = (i & 1) ? 1 : -1;
            t.fillCircle(cx + side * (U(15) + (int)(p * U(6))), headTop + U(6) - (int)(p * U(18)),
                         U(2) + (int)(p * U(3)), Theme::blend(Theme::BG, Theme::W95_LIGHT, (uint16_t)(255 * (1.0f - p))));
        }
        break;
    case Fx::WAVES:
        for (int i = 0; i < 3; i++) {
            const float p = (float)((be + (uint32_t)i * 330u) % 1000u) / 1000.0f;
            t.drawCircle(cx, headTop + U(16), U(18) + (int)(p * U(26)),
                         Theme::blend(Theme::BG, Theme::CYAN, (uint16_t)(255 * (1.0f - p))));
        }
        break;
    case Fx::FLASH: {
        int x0 = (hx < gx ? hx : gx) - U(34), x1 = (hx > gx ? hx : gx) + U(34);
        if (x0 < 2) x0 = 2;
        if (x1 > t.width() - 3) x1 = t.width() - 3;
        // Down to his feet and no further: the counters start right below.
        const int y0 = headTop - U(10), y1 = headTop + U(58);
        if (arg == 0 && be < 160) {
            t.fillRect(x0, y0, x1 - x0, y1 - y0, Theme::WHITE);        // the flash
        } else if ((arg == 1 && be < 700) || (arg == 0 && be >= 250)) {
            for (int j = 0; j < 3; j++) t.drawRect(x0 + j, y0 + j, x1 - x0 - 2 * j, y1 - y0 - 2 * j, Theme::WHITE);
            t.fillRect(x0, y1 - U(4), x1 - x0, U(4), Theme::WHITE);    // the photo's bottom edge
        }
        break;
    }
    case Fx::HATS:
        for (int who = 0; who < 2; who++) {
            if (who == 1 && arg == 0) break;                           // only A has his on yet
            const bool host = (who == 0) == s_scriptAHost;
            const int x = host ? hx : gx, top = headTop + (host ? 0 : guestBob(now, gs));
            t.fillTriangle(x - U(13), top + U(3), x + U(13), top + U(3), x + U(3), top - U(16), Theme::W95_LIGHT);
            t.drawLine(x - U(6), top, x + U(1), top - U(10), Theme::WHITE);
            t.drawLine(x + U(5), top + U(1), x + U(1), top - U(6), Theme::W95_SHADOW);
            t.drawFastHLine(x - U(12), top + U(3), U(24), Theme::W95_SHADOW);
        }
        break;
    case Fx::CAMERA: {
        // Up between their heads, clear of the host's bubble in the top row.
        int y = headTop - U(6);
        if (y < 30) y = 30;
        t.drawFastVLine(midX, y - U(12), U(7), Theme::W95_SHADOW);
        t.fillRoundRect(midX - U(9), y - U(6), U(18), U(11), 2, Theme::W95_SHADOW);
        t.fillCircle(midX, y, U(3) + 1, Theme::BLACK);
        t.drawCircle(midX, y, U(3) + 2, Theme::W95_LIGHT);
        if ((now / 250) & 1) t.fillCircle(midX + U(6), y - U(3), U(1) + 1, Theme::RED);
        break;
    }
    case Fx::ICON: {
        // Between their faces when there is room for it (landscape), above
        // their heads when there is not -- in portrait they nearly touch.
        const int room = (gx > hx ? gx - hx : hx - gx) - U(48);
        const bool roomy = room >= U(30);
        int y = roomy ? headTop + U(6) : headTop - U(8);
        if (y < 30) y = 30;
        const DetectionType type = (DetectionType)s_scriptSetup;
        if (type != DetectionType::UNKNOWN && s_scriptSetup < (uint8_t)DetectionType::COUNT)
            Theme::drawTypeIcon(t, type, midX, y, U(8) + (int)((be / 180) & 1));
        t.setTextColor(Theme::VAPOR_YELLOW);
        t.setTextSize(2);
        // Up there, the right-hand side is the visitor's bubble: go left.
        t.setCursor(roomy ? midX + U(12) : midX - U(12) - 24, y - 7);
        t.print("?!");
        t.setTextSize(1);
        break;
    }
    case Fx::COIN: {
        const int ahx = ax + (bx > ax ? 1 : -1) * U(30);
        if (arg == 2) {                                              // waiting in his hand
            t.fillCircle(ahx, handY - U(4), U(3) + 1, Theme::VAPOR_YELLOW);
            t.drawCircle(ahx, handY - U(4), U(3) + 1, Theme::AMBER);
        } else if (arg == 0) {                                       // up, spinning, and down
            const int x = ahx + (int)((float)(midX - ahx) * k);
            const int y = handY - U(4) + (int)((float)(groundY - handY + U(4)) * k)
                          - (int)(sinf(k * 3.14159265f) * U(48));
            const int wv = (int)(fabsf(cosf((float)be / 60.0f)) * U(3)) + 1;
            t.fillEllipse(x, y, wv, U(3) + 1, Theme::VAPOR_YELLOW);
        } else {                                                     // landed, face up
            const int y = groundY - U(6);
            t.fillCircle(midX, y, U(6) + 1, Theme::VAPOR_YELLOW);
            t.drawCircle(midX, y, U(6) + 1, Theme::AMBER);
            t.setTextColor(Theme::BLACK);
            t.setCursor(midX - 2, y - 3);
            t.print((s_scriptSetup & 1) ? "T" : "H");
        }
        break;
    }
    case Fx::DICE: {
        const int dA = (bx > ax ? -1 : 1);                            // A's lands on A's side
        const int landA = midX + dA * U(10), landB = midX - dA * U(10);
        const uint8_t va = EmoteScript::die(s_scriptSetup, 0), vb = EmoteScript::die(s_scriptSetup, 1);
        if (arg == 0) {
            const int hA = ax + (bx > ax ? 1 : -1) * U(30), hB = bx + (ax > bx ? 1 : -1) * U(30);
            const int lift = (int)(sinf(k * 3.14159265f) * U(24));
            const int yA = handY + (int)((float)(groundY - handY) * k) - lift;
            const uint8_t spin = (uint8_t)(1 + (be / 90) % 6);
            fxDie(t, hA + (int)((float)(landA - hA) * k), yA, spin, gs);
            fxDie(t, hB + (int)((float)(landB - hB) * k), yA, (uint8_t)(7 - spin), gs);
        } else {
            fxDie(t, landA, groundY - U(5), va, gs);
            fxDie(t, landB, groundY - U(5), vb, gs);
        }
        break;
    }
    case Fx::TABLE: {
        const int mx = hx + U(30);                                   // where the two grips meet
        const uint16_t wood = Theme::blend(Theme::AMBER, Theme::BLACK, 120);
        t.fillRect(mx - U(13), headTop + U(31), U(26), U(3), wood);
        t.fillRect(mx - U(11), headTop + U(34), U(2), groundY - headTop - U(34), wood);
        t.fillRect(mx + U(9),  headTop + U(34), U(2), groundY - headTop - U(34), wood);
        if (arg == 0) {
            // Nobody winning yet: little strain marks over the grip.
            const int s = (int)((now / 140) & 1);
            t.drawLine(mx - U(4), headTop + U(12) - s, mx - U(2), headTop + U(16), Theme::WHITE);
            t.drawLine(mx + U(4), headTop + U(12) + s, mx + U(2), headTop + U(16), Theme::WHITE);
        }
        break;
    }
    case Fx::ROPE: {
        const int y = headTop + U(28);
        const int hL = hx + U(26), gR = gx - U(26);
        const uint16_t rope = Theme::blend(Theme::AMBER, Theme::WHITE, 60);
        if (arg == 0) {
            const int mx = (hL + gR) / 2 + (int)(sinf((float)be / 200.0f) * U(3));
            t.drawWideLine(hL, y, mx, y + U(3), U(1) + 1, rope);
            t.drawWideLine(mx, y + U(3), gR, y, U(1) + 1, rope);
            t.fillTriangle(mx - U(3), y + U(3), mx + U(3), y + U(3), mx, y + U(9), Theme::RED);
        } else {
            // The loser let go: the rope runs from the winner's hands to the floor.
            const bool hostWon = (s_scriptResult == Result::SENDER) == s_scriptAHost;
            const int wx = hostWon ? hL : gR, fx = hostWon ? gR : hL;
            t.drawWideLine(wx, y, midX, groundY, U(1) + 1, rope);
            t.drawWideLine(midX, groundY, fx, groundY, U(1) + 1, rope);
            t.fillTriangle(midX - U(3), groundY - U(1), midX + U(3), groundY - U(1), midX, groundY - U(7), Theme::RED);
        }
        break;
    }
    case Fx::CUPS: {
        const int y = headTop + U(22) - U(6);
        const int c1 = hx + U(30) - (arg == 1 ? 0 : U(2)), c2 = gx - U(30) + (arg == 1 ? 0 : U(2));
        for (int i = 0; i < 2; i++) {
            const int x = i ? c2 : c1;
            t.fillRect(x - U(3), y - U(4), U(6) + 1, U(8), Theme::AMBER);
            t.fillRect(x - U(3), y - U(5), U(6) + 1, U(2) + 1, Theme::WHITE);
        }
        if (arg == 1 && be >= 100 && be < 420)
            drawSpark(t, (c1 + c2) / 2, y - U(4), (float)(be - 100) / 320.0f, gs);
        break;
    }
    case Fx::CONFETTI: {
        const int x0 = (hx < gx ? hx : gx) - U(40);
        const int span = (hx > gx ? hx - gx : gx - hx) + U(80);
        const int fall = U(110);
        for (int i = 0; i < 24; i++) {
            uint32_t hsh = (uint32_t)(i + 1) * 2654435761u;
            hsh ^= hsh >> 13;
            const int x = x0 + (int)(hsh % (uint32_t)(span > 0 ? span : 1)) + (int)(sinf((float)be / 180.0f + i) * U(3));
            const uint32_t v = 30u + (hsh >> 8) % 40u;               // px a second, per piece
            const int y = headTop - U(40) + (int)(((be * v) / 1000u + (hsh >> 16)) % (uint32_t)(fall > 0 ? fall : 1));
            t.fillRect(x, y, U(2) + 1, U(1) + 1, CONF[i % 5]);
        }
        break;
    }
    case Fx::FIREWORKS: {
        static const int8_t BX[3] = { -30, 28, 0 }, BY[3] = { -24, -34, -14 };
        for (int j = 0; j < 4; j++) {
            const int32_t st = j * 650;
            if ((int32_t)be < st || (int32_t)be >= st + 800) continue;
            const float p = (float)((int32_t)be - st) / 800.0f;
            int y = headTop + U(BY[j % 3]);
            if (y < 22) y = 22;
            const int x = midX + U(BX[j % 3]);
            const uint16_t c = Theme::blend(Theme::BG, CONF[j % 5], (uint16_t)(255 * (1.0f - p)));
            for (int r = 0; r < 12; r++) {
                const float a = (float)r * 0.5235988f;
                const float d = p * (float)U(22);
                t.drawLine(x + (int)(cosf(a) * d * 0.6f), y + (int)(sinf(a) * d * 0.6f),
                           x + (int)(cosf(a) * d), y + (int)(sinf(a) * d), c);
            }
        }
        break;
    }
    case Fx::HAHA:
        for (int i = 0; i < 3; i++) {
            const float p = (float)((be + (uint32_t)i * 400u) % 1200u) / 1200.0f;
            t.setTextColor(Theme::blend(Theme::BG, Theme::VAPOR_YELLOW, (uint16_t)(255 * (1.0f - p))));
            // Off the side of his head away from the other one, rising --
            // above it they ran into his bubble.
            t.setCursor(cx - dir * (U(24) + i * U(5)) - 6, headTop + U(16) - (int)(p * U(18)));
            t.print("HA");
        }
        break;
    case Fx::BONK:
        for (int i = 0; i < 3; i++) {
            const float a = (float)be / 150.0f + (float)i * 2.0943951f;
            fxStar(t, ox + (int)(cosf(a) * U(13)), headTop + U(2) + (int)(sinf(a) * U(4)), U(2) + 1,
                   Theme::VAPOR_YELLOW);
        }
        break;
    default: break;
    }
}

// What the set pieces throw about and hold up: snowballs and their puffs, and
// the rock-paper-scissors reveal. Drawn over both of them.
static void drawPieceFx(TFT_eSPI& t, uint32_t now, int hx, int gx, int headTop, float gs) {
    if (s_piece == Piece::SCRIPT) {
        drawScriptFx(t, now, hx, gx, headTop, gs);
    } else if (s_piece == Piece::SNOW) {
        const uint32_t e = now - s_pieceAt, step = e / SNOW_SEG_MS, se = e % SNOW_SEG_MS;
        if (step <= 1 && se >= SNOW_WIND_MS && se < SNOW_WIND_MS + SNOW_FLY_MS) {
            // From the thrower's hand to the other one's head, on an arc.
            const float k  = (float)(se - SNOW_WIND_MS) / (float)SNOW_FLY_MS;
            const bool  hostThrows = (step == 0) != s_pieceGuestFirst;
            const int   x0 = hostThrows ? hx + (int)(22.0f * gs) : gx - (int)(22.0f * gs);
            const int   x1 = hostThrows ? gx : hx;
            const int   y0 = headTop + (int)(8.0f * gs), y1 = headTop + (int)(14.0f * gs);
            const int   x  = x0 + (int)((x1 - x0) * k);
            const int   y  = y0 + (int)((y1 - y0) * k) - (int)(sinf(k * 3.14159265f) * 26.0f * gs);
            t.fillCircle(x, y, (int)(2.5f * gs) + 2, Theme::W95_SHADOW);
            t.fillCircle(x, y, (int)(2.5f * gs) + 1, Theme::WHITE);
        }
        if (s_puffAt && now - s_puffAt < 300) {
            const float k  = (float)(now - s_puffAt) / 300.0f;
            const int   px = s_puffGuest ? gx : hx, py = headTop + (int)(14.0f * gs);
            for (int i = 0; i < 8; i++) {
                const float a = (float)i * 0.78539816f, r = (3.0f + 10.0f * k) * gs;
                t.fillCircle(px + (int)(cosf(a) * r), py + (int)(sinf(a) * r), 1 + (int)gs, Theme::WHITE);
            }
        }
    } else if (s_piece == Piece::RPS && s_pieceStep >= 1) {
        // Over the hands the LEVEL reach holds out -- S(30) across, S(22) down
        // -- and held through the reaction, so the result is still readable
        // while they react to it.
        const int y = headTop + (int)(22.0f * gs) - (int)(16.0f * gs);
        drawRps(t, hx + (int)(30.0f * gs), y, s_rpsHost, gs);
        drawRps(t, gx - (int)(30.0f * gs), y, s_rpsGuest, gs);
    } else if (s_piece == Piece::FIVE) {
        // Where the UP reach ends -- S(30) across, REACH_Y[0] down.
        const uint32_t e = now - s_pieceAt, hit = FIVE_IN_MS + FIVE_HIT_MS;
        if (e >= hit && e < hit + 320)
            drawSpark(t, hx + (int)(REACH_K * 0.5f * gs), headTop + (int)(REACH_Y[0] * gs),
                      (float)(e - hit) / 320.0f, gs);
    }
}

// The guest being drawn, held as a COPY rather than a pointer.
//
// A different peer must still walk out/in rather than silently turning one
// Squachy into another, and a peer that leaves must keep its final outfit
// throughout the goodbye walk. But a *same-MAC* guest who changes clothes or
// shades while visiting should update in place; Squad already reflects the
// latest radio appearance and CLEAR must not keep the arrival snapshot.
static SquachMesh::Peer s_hosting{};
static uint32_t         s_hostingId = 0;

// What the screen should actually draw: the copy, for as long as the visit
// lasts, and nothing once it is over.
static const SquachMesh::Peer* visitHosting() {
    return (s_vp == VisitPhase::GONE) ? nullptr : &s_hosting;
}

// Is the guest on screen one the dice made up? He keeps being one after the
// encounter has let go of him, for as long as his goodbye takes.
static bool encGuestHere() {
    if (s_vp == VisitPhase::GONE) return false;
    if (s_hostingGoblin) return s_encGob.id != 0 && s_goblinHost.id == s_encGob.id;
    return s_hostingId == (0xE6C00000u | (s_encSeq & 0xFFFFu));
}
// How open the hole is, 0 to 1.
static float encHoleOpen(uint32_t now) {
    if (s_vp == VisitPhase::ARRIVING) {
        const uint32_t e = now - s_vpAt, shut = ENC_HOLE_MS + ENC_FALL_MS + 120;
        if (e < shut) return e < 300 ? (float)e / 300.0f : 1.0f;
        return e < shut + 250 ? 1.0f - (float)(e - shut) / 250.0f : 0.0f;
    }
    if (s_vp == VisitPhase::LEAVING) {
        const int32_t dt = (int32_t)(now - s_vpAt);     // negative while he says goodbye
        if (dt < -400) return 0.0f;
        if (dt < 0) return (float)(dt + 400) / 400.0f;
        if (dt < (int32_t)ENC_RISE_MS) return 1.0f;
        return dt < (int32_t)ENC_RISE_MS + 200 ? 1.0f - (float)(dt - (int32_t)ENC_RISE_MS) / 200.0f : 0.0f;
    }
    return 0.0f;
}
// How far off the floor he is: all the way up (out of sight above the band)
// until the hole is open, then down; and back up to leave.
static int encGuestLift(uint32_t now, int full) {
    if (s_vp == VisitPhase::ARRIVING) {
        const uint32_t e = now - s_vpAt;
        if (e < ENC_HOLE_MS) return full;
        if (e < ENC_HOLE_MS + ENC_FALL_MS) {
            const float k = (float)(e - ENC_HOLE_MS) / (float)ENC_FALL_MS;
            return (int)((float)full * (1.0f - k * k));
        }
        return 0;
    }
    if (s_vp == VisitPhase::LEAVING) {
        const int32_t dt = (int32_t)(now - s_vpAt);
        if (dt <= 0) return 0;
        float k = (float)dt / (float)ENC_RISE_MS;
        if (k > 1.0f) k = 1.0f;
        return (int)((float)full * k * k);
    }
    return 0;
}
// The hole itself: a wobbling ellipse, a row at a time.
static void drawEncHole(TFT_eSPI& t, int cx, int cy, int u, float open, uint32_t now, bool farRim = false) {
    const int rx = (int)(42.0f * u * open), ry = (int)(18.0f * u * open);
    if (rx < 3 || ry < 2) return;
    const uint16_t RIM = Theme::VAPOR_PINK, BODY = 0x693B, RING = Theme::CYAN, DEEP = 0x200A;
    for (int dy = -ry; dy <= (farRim ? -1 : ry); dy++) {
        const float f = sqrtf(1.0f - (float)(dy * dy) / (float)(ry * ry));
        const float wob = 1.0f + 0.10f * sinf(dy * 0.55f + now / 140.0f) + 0.05f * sinf(dy * 1.3f - now / 90.0f);
        const int a = (int)((float)rx * f * wob);
        if (a < 1) continue;
        const int y = cy + dy;
        t.drawFastHLine(cx - a, y, 2 * a, RIM);
        if (a > 2 * u) t.drawFastHLine(cx - a + 2 * u, y, 2 * (a - 2 * u), BODY);
        const int b = a * 4 / 5;
        if (b > 2 * u) {
            t.drawFastHLine(cx - b, y, 2 * b, RING);
            t.drawFastHLine(cx - b + u, y, 2 * (b - u), DEEP);
        }
        const int c = a * 11 / 20;
        if (c > 2 * u) {
            t.drawFastHLine(cx - c, y, 2 * c, RIM);
            t.drawFastHLine(cx - c + u, y, 2 * (c - u), Theme::BG);
        }
    }
}

// Public answer to "who is visiting": the hosted copy, not the raw radio
// state. Anything asking should see the same visitor the screen does,
// including while he is still walking out after the peer has gone.
// A goblin is not one: he cannot be messaged, sent an emote or hunted, so
// everything that asks gets "nobody" while the screen still draws him.
const SquachMesh::Peer* uiClearGuest() { return s_hostingGoblin ? nullptr : visitHosting(); }

// ---- messages ---------------------------------------------------------------
// A received message is drawn in RED, filled, so it can never be mistaken for
// the scripted banter two Squachys trade on their own. That is pink and black,
// and it is the device talking to itself; red means a person sent it.
static const uint32_t MSG_SHOW_MS = 8000;

static bool    s_msgGuestOn = false;      // a visitor is on screen this frame
static int     s_msgGx = 0, s_msgHeadTop = 0;
// The visitor's own speech bubble this frame, if he is talking: the message
// button keeps out from under it.
static bool    s_msgGbOn = false;
static int     s_msgGbX = 0, s_msgGbY = 0, s_msgGbW = 0, s_msgGbH = 0;
static float   s_msgGs = 1.0f;            // the visitor's scale this frame
static bool    s_bubbleOn = false;        // the tap target, filled by the draw
static int16_t s_bubX = 0, s_bubY = 0, s_bubW = 0, s_bubH = 0;

static bool messageShowing(uint32_t now) {
    const MeshTalk::Message& m = MeshTalk::inbox();
    return MeshTalk::ready() && m.have && (uint32_t)(now - m.at) < MSG_SHOW_MS;
}

// The tutorial's pretend reply, painted where a real one would be.
static bool tutorReply() {
    return MeshTutor::active() && MeshTutor::step() == MeshTutor::Step::REPLY;
}

// Who, then what, over the speaker's head, with a tail aimed at him. The name
// is always in it, so even when the sender is not the one standing there the
// bubble cannot misattribute.
static void drawRedBubble(TFT_eSPI& t, int cx, int headTop, const char* from, const char* line) {
    t.setTextSize(1);
    t.setTextWrap(false);
    const int w = t.width();
    // The message itself in font 2, the same face Squachy's own bubble uses
    // now; the sender's name stays in the small font above it, a caption.
    // A typed message runs to 48 characters, wider than a portrait screen:
    // it wraps to two lines, or three, rather than running off the edge.
    Theme::bubbleFontOn(t);
    const int lineH = Theme::bubbleTextH() + 1;
    int maxW = w - 20;
    char rows[3][48];
    const uint8_t n = Theme::wrapText(t, line, maxW, rows, 3);
    int bw = 0;
    for (uint8_t i = 0; i < n; i++) {
        const int rw = t.textWidth(rows[i]);
        if (rw > bw) bw = rw;
    }
    Theme::bubbleFontOff(t);
    if (t.textWidth(from) > bw) bw = t.textWidth(from);
    bw += 12;
    if (bw > w - 8) bw = w - 8;
    const int bh = 14 + n * lineH + 3;
    int bx = cx - bw / 2;
    if (bx < 4) bx = 4;
    if (bx + bw > w - 4) bx = w - 4 - bw;
    int by = headTop - bh - 6;
    if (by < 18) by = 18;                     // clear of the corner icons
    t.fillRoundRect(bx, by, bw, bh, 4, Theme::RED);
    int tx = cx;
    if (tx < bx + 6) tx = bx + 6;
    if (tx > bx + bw - 7) tx = bx + bw - 7;
    t.fillTriangle(tx - 4, by + bh - 1, tx + 4, by + bh - 1, tx, by + bh + 4, Theme::RED);
    t.setTextColor(Theme::W95_LIGHT, Theme::RED);
    t.setCursor(bx + 6, by + 3);
    t.print(from);
    Theme::bubbleFontOn(t);
    t.setTextColor(Theme::WHITE, Theme::RED);
    for (uint8_t i = 0; i < n; i++) {
        t.setCursor(bx + 6, by + 13 + i * lineH + Theme::bubbleAscent());
        t.print(rows[i]);
    }
    Theme::bubbleFontOff(t);
}

// The little bubble. Cyan dots when there is nothing new; the dots take turns
// while a message of ours is on the air, like somebody typing; and it goes red
// with a "!" when one has arrived and not been read.
// Sized for a thumb, not for discretion. It is the only way into messages,
// and at the first size, 20x13, it was hard to see and harder to hit.
static const int MSG_ICON_W = 32, MSG_ICON_H = 22;

static void drawMessageIcon(TFT_eSPI& t, int x, int y, bool unread, bool sending, uint32_t now) {
    const int iw = MSG_ICON_W, ih = MSG_ICON_H;
    // Unread is filled with the theme's RED. In GH0ST that RED is near white,
    // so the bubble was a solid white block with a white mark on it: on a
    // pale RED the fill drops to a mid grey and the mark goes black.
    uint16_t fill = unread ? Theme::RED : Theme::BG;
    uint16_t mark = Theme::WHITE;
    if (unread && Theme::labelOn(fill) == Theme::BLACK) {
        fill = Theme::blend(fill, Theme::BLACK, 96);
        mark = Theme::BLACK;
    }
    const uint16_t edge = unread ? fill : Theme::CYAN;
    t.fillRoundRect(x, y, iw, ih, 5, fill);
    t.drawRoundRect(x, y, iw, ih, 5, edge);
    t.fillTriangle(x + 4, y + ih - 1, x + 12, y + ih - 1, x + 4, y + ih + 5, edge);
    if (unread) {
        t.setTextSize(2);
        t.setTextColor(mark, fill);
        t.setCursor(x + (iw - 12) / 2 + 1, y + (ih - 16) / 2 + 1);
        t.print("!");
        t.setTextSize(1);
    } else {
        for (int i = 0; i < 3; i++) {
            const bool lit = !sending || (int)((now / 250) % 3) == i;
            t.fillRect(x + 8 + i * 6, y + 9, 4, 4, lit ? Theme::CYAN : Theme::W95_SHADOW);
        }
    }
    // A finger-sized target around a thumbnail-sized icon.
    s_bubX = (int16_t)(x - 8);
    s_bubY = (int16_t)(y - 8);
    s_bubW = (int16_t)(iw + 16);
    s_bubH = (int16_t)(ih + 16);
    s_bubbleOn = true;
}

// More SquachWatches in range than the one visiting: a small "+N" under his
// message button, N being everybody beyond him. Not drawn for one visitor --
// that is the normal case, and a badge that is always there says nothing.
// The badge's tap target, filled by the draw -- finger-sized around a small pill.
static bool    s_badgeOn = false;
static int16_t s_badX = 0, s_badY = 0, s_badW = 0, s_badH = 0;

// How many SquachWatches are in range, bottom left, sitting just above the
// detection counters. Tapping it opens the SQUAD screen.
//
// It used to be a small "+N" pill beside the visitor's head: it moved around
// with him, it was eleven pixels tall, and it only appeared while somebody was
// actually on screen. Down here it has a fixed home, it is thumb-sized, and it
// can say how many are out there whether or not one of them is visiting.
// `rightX` is its right edge: the badge grows leftward from there, so a count
// that reaches two digits cannot push it off the screen.
static void drawSquadBadge(TFT_eSPI& t, int rightX, int bottomY, uint8_t count) {
    char b[6];
    snprintf(b, sizeof b, "%u", (unsigned)count);
    t.setTextSize(2);
    const int bh = 22, bw = 24 + t.textWidth(b) + 8;
    const int x = rightX - bw, y = bottomY - bh;
    // PURPLE fill, VAPOR_PINK head and a white count: in GH0ST all three are
    // white, and the badge was a blank white block. On a pale fill the head
    // and rim go grey and the count goes black.
    const bool pale = Theme::labelOn(Theme::PURPLE) == Theme::BLACK;
    const uint16_t head = pale ? Theme::blend(Theme::PURPLE, Theme::BLACK, 120) : Theme::VAPOR_PINK;
    const uint16_t num  = pale ? Theme::BLACK : Theme::WHITE;
    t.fillRoundRect(x, y, bw, bh, 6, Theme::PURPLE);
    t.drawRoundRect(x, y, bw, bh, 6, head);
    // A little head, for "more of us": round, with two ears.
    t.fillCircle(x + 12, y + 13, 6, head);
    t.fillRect(x + 5, y + 4, 4, 4, head);
    t.fillRect(x + 15, y + 4, 4, 4, head);
    t.setTextColor(num, Theme::PURPLE);
    t.setCursor(x + 24, y + (bh - 14) / 2);
    t.print(b);
    t.setTextSize(1);
    // A finger-sized target around it.
    s_badX = (int16_t)(x - 6); s_badY = (int16_t)(y - 8);
    s_badW = (int16_t)(bw + 12); s_badH = (int16_t)(bh + 16);
    s_badgeOn = true;
}

// ---- the watch/hunt indicator ------------------------------------------------
// Until this existed, a watch was invisible: its label was drawn in exactly one
// place, the alert screen, which only appears when the target comes back into
// range AND the 30s cooldown has passed. Set one and walk away and there was no
// way to learn it was still running -- or that you had set one at all.
//
// In the TITLE BAR's empty middle, not down by the counters. The bottom-left
// corner looked free and is not: the landscape counter row is 312px wide on a
// 320px screen, so it starts at x=4 -- exactly where this sat -- and
// drawCounterLine() runs later in the same tick and painted over it. Portrait
// is worse: that row overflows the screen. The renders showed no pill at all.
//
// drawTitleBar() paints the gear (x 0..28), the rotate icon (right 28px) and
// the lock (26px inside that right group) and throws its title argument away,
// so the span between them is genuinely empty -- and it is drawn BEFORE this,
// which is what makes the placement safe rather than merely available.
//
// Unlike the squad badge this draws in boring mode too: it is not about
// Squachy, it is the state of a detection feature, and boring mode keeps all
// of those.
static bool    s_watchPillOn = false;

// The watch's corner clock: 12-hour, cyan on a black tile like the icons, just
// left of whichever right-hand icons are showing (the corner itself when
// rotation is locked, the default). Only once the time is real -- a guessed or
// unset clock draws nothing. Drawn straight after the background, BEFORE
// Squachy, the visitor and every speech bubble, so a bubble that reaches the
// top band covers the clock rather than the clock cutting a hole in the
// bubble. Returns where the WATCH pill's free span must end, or -1.
static int16_t s_cornerClockPillR = -1;

// ---- the battery gauge: a lava lamp -------------------------------------------
// Picked from the battery lab (2026-10-10): a vaporwave lava lamp under the
// menu icon, filled to the charge, with no box behind it -- a one-pixel
// outline traced round its shape instead, glowing the lava's colour beside
// the lit glass. Blobs rise and sink; on the cable they bubble faster and the
// lamp brightens, bubbles stream up, its glowing edge beats and a rainbow
// scrolls through the cap and base; under 10% it
// flickers like a dying bulb; full, it twinkles.
// The lava drifts pink -> purple -> cyan, the theme's own colours.
//
// The art is 11 x 19 "pixels", doubled on the 3.5in. It is built in a small
// grid first, so the outline can be traced round whatever got drawn, and then
// goes on the screen as runs -- about 40 fillRects a frame, not 300.
static BatterySource s_battSrc = nullptr;
void uiClearSetBatterySource(BatterySource fn) { s_battSrc = fn; }
bool uiClearHasBattery() { return s_battSrc != nullptr; }
bool uiClearBatteryNow(uint8_t& pct, bool& charging) { return s_battSrc && s_battSrc(pct, charging); }

static float lampHash(uint32_t a, uint32_t b) {
    uint32_t h = (a * 374761393u + b * 668265263u) ^ 0x5bd1e995u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return (float)(h ^ (h >> 16)) / 4294967295.0f;
}

// Full-strength colour at `deg` round the colour wheel, 0..359.
static uint16_t lampRainbow(uint16_t deg) {
    const uint8_t f = (uint8_t)((deg % 60) * 255 / 60);
    uint8_t r, g, b;
    switch (deg / 60) {
        case 0:  r = 255;     g = f;       b = 0;       break;
        case 1:  r = 255 - f; g = 255;     b = 0;       break;
        case 2:  r = 0;       g = 255;     b = f;       break;
        case 3:  r = 0;       g = 255 - f; b = 255;     break;
        case 4:  r = f;       g = 0;       b = 255;     break;
        default: r = 255;     g = 0;       b = 255 - f; break;
    }
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

// Where the lamp goes: h2 is the size of one art pixel in HALF pixels (so it
// can be 1.5), and (ox, oy) the top left of its outline. Small screens: under
// the menu icon. The 3.5in draws it triple size (asked for 2026-10-10), under the
// menu icon in both orientations. Portrait's two-line speech bubble overlaps
// the side of the glass, and that is wanted: the bubble is drawn later and
// sits on top, like any other bubble over the background.
// The 320-wide boards draw it double, hard against the left edge (asked for
// 2026-10-12, on the Marauder V8); the art's outline column is x -1, so ox = s
// puts that on pixel 0. The 240-wide screens go hard against the left edge
// too, and the watches draw it one and a half times (both asked for
// 2026-10-13, on the T-Watch); the 135-row sticks keep it as drawn.
static void lampPlace(TFT_eSPI& t, int& h2, int& ox, int& oy) {
    h2 = Theme::large() ? 6 : t.width() >= 320 ? 4 : Theme::compact() ? 2 : 3;
    ox = 0;
    oy = Theme::TITLE_ICON_BAND_H + 3 - h2 / 2;     // art row 0 where it always was
}

static void drawLavaLamp(TFT_eSPI& t, uint32_t now, uint8_t L, bool chg) {
    // Art x -1..11 and y -1..19: the lamp plus a ring for its outline.
    static const int GW = 13, GH = 21;
    uint16_t col[GH][GW];
    uint8_t  kind[GH][GW] = {};          // 0 nothing, 1 lamp, 2 outline
    auto put = [&](int x, int y, uint16_t c) {
        if (x < -1 || x > 11 || y < -1 || y > 19) return;
        kind[y + 1][x + 1] = 1; col[y + 1][x + 1] = c;
    };
    auto fill = [&](int x, int y, int w, int h, uint16_t c) {
        for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) put(x + i, y + j, c);
    };
    // The glass widens from 5 wide under the cap to 9 at the base.
    auto glass = [](int y, int& a, int& z) {
        const float hw = 2.0f + (float)(y - 3) / 11.0f * 2.5f;
        a = (int)lroundf(5.0f - hw); z = (int)lroundf(5.0f + hw);
    };
    const bool low = L <= 10 && !chg;
    const int  n   = (int)lroundf(12.0f * L / 100.0f);   // rows of lava, of 12

    const uint16_t metal  = Theme::PURPLE;
    const uint16_t metalD = Theme::blend(Theme::PURPLE, TFT_BLACK, 90);
    const float k = (sinf((float)now / 1800.0f) + 1.0f) * 0.5f;
    const uint16_t hue = k < 0.5f
        ? Theme::blend(Theme::VAPOR_PINK, Theme::VAPOR_PURPLE, (uint16_t)(k * 2.0f * 256.0f))
        : Theme::blend(Theme::VAPOR_PURPLE, Theme::CYAN, (uint16_t)((k - 0.5f) * 2.0f * 256.0f));
    uint16_t liq = hue;
    if (low && lampHash(1, now / 90) > 0.55f) liq = Theme::blend(hue, TFT_BLACK, 179);
    if (chg) liq = Theme::blend(hue, TFT_WHITE, (uint16_t)((0.15f + 0.15f * sinf((float)now / 150.0f)) * 256.0f));
    const uint16_t liqEdge   = Theme::blend(liq, TFT_BLACK, 102);
    const uint16_t emptyFill = 0x2065;   // ( 35, 12, 45)
    const uint16_t emptyEdge = 0x51ED;   // ( 80, 60,110)

    // The cap and base: purple, or on the cable a rainbow scrolling through
    // them row by row, one lap every two seconds or so.
    auto metalRow = [&](int y, bool dark) -> uint16_t {
        if (!chg) return dark ? metalD : metal;
        const uint16_t c = lampRainbow((uint16_t)((now / 6 + (uint32_t)y * 24u) % 360u));
        return dark ? Theme::blend(c, TFT_BLACK, 90) : c;
    };
    fill(4, 0, 3, 1, metalRow(0, false)); fill(3, 1, 5, 1, metalRow(1, false)); fill(3, 2, 5, 1, metalRow(2, true));
    for (int y = 3; y <= 14; y++) {
        int a, z; glass(y, a, z);
        const bool filled = 14 - y < n;
        for (int x = a; x <= z; x++) {
            const bool edge = x == a || x == z;
            put(x, y, filled ? (edge ? liqEdge : liq) : (edge ? emptyEdge : emptyFill));
        }
    }
    if (n > 1) {
        const float sp = chg ? 2.5f : 1.0f;
        static const float PH[3] = { 0.0f, 1.7f, 3.1f };
        for (int i = 0; i < 3; i++) {
            const uint16_t c = i == 1 ? Theme::CYAN : Theme::VAPOR_YELLOW;
            const float ph = (float)now * sp / 900.0f + PH[i];
            const int yy = 14 - (int)lroundf((sinf(ph) + 1.0f) * 0.5f * (float)(n - 1));
            int a, z; glass(yy, a, z);
            const int xx = (int)lroundf(5.0f + sinf(PH[i] * 3.0f + i) * (float)(z - a) / 4.0f);
            put(xx, yy, c); put(xx + 1, yy, c);
            const int y2 = yy + (cosf(ph) > 0.0f ? 1 : -1);
            if (14 - y2 < n && y2 <= 14) put(xx, y2, c);
        }
    }
    if (chg && n >= 2) {
        // On the cable: a stream of bubbles racing up through the lava, so
        // charging reads from across the room and not only side by side.
        const uint16_t fizz = Theme::blend(hue, TFT_WHITE, 200);
        static const uint8_t BX[3] = { 4, 6, 5 };
        for (int i = 0; i < 3; i++) {
            const uint32_t ph = (now + (uint32_t)i * 233u) % 700u;
            const int yy = 14 - (int)(ph * (uint32_t)n / 700u);
            if (yy > 14 - n) put(BX[i] + (int)((now / 140 + i) % 2), yy, fizz);
        }
    }
    fill(2, 15, 7, 1, metalRow(15, true)); fill(2, 16, 7, 1, metalRow(16, false));
    fill(2, 17, 7, 1, metalRow(17, false)); fill(1, 18, 9, 1, metalRow(18, false));
    if (L >= 100 && (now / 300) % 2 == 0) put(5, 0, TFT_WHITE);

    // The outline: every empty neighbour of the lamp. Black, except beside
    // the lit part of the glass, where it glows the lava's colour.
    // On the cable the glow beats, bright to dim, about once a second.
    const uint16_t glow = chg
        ? Theme::blend(hue, TFT_BLACK, (uint16_t)(60.0f + 110.0f * (0.5f + 0.5f * sinf((float)now / 160.0f))))
        : Theme::blend(hue, TFT_BLACK, 154);
    for (int gy = 0; gy < GH; gy++) for (int gx = 0; gx < GW; gx++) {
        if (kind[gy][gx]) continue;
        bool touch = false;
        for (int dy = -1; dy <= 1 && !touch; dy++) for (int dx = -1; dx <= 1; dx++) {
            const int ny = gy + dy, nx = gx + dx;
            if (ny >= 0 && ny < GH && nx >= 0 && nx < GW && kind[ny][nx] == 1) { touch = true; break; }
        }
        if (!touch) continue;
        const int y = gy - 1;
        kind[gy][gx] = 2;
        col[gy][gx] = (y >= 3 && y <= 14 && 14 - y < n) ? glow : TFT_BLACK;
    }

    int h2, ox, oy;
    lampPlace(t, h2, ox, oy);
    // Each art pixel's edges land on whole pixels, so at 1.5 the rows and
    // columns come out 1 and 2 pixels by turns, with no gaps.
    for (int gy = 0; gy < GH; gy++) {
        const int y0 = oy + (gy * h2 >> 1), y1 = oy + ((gy + 1) * h2 >> 1);
        int gx = 0;
        while (gx < GW) {
            if (!kind[gy][gx]) { gx++; continue; }
            const uint16_t c = col[gy][gx];
            int run = 1;
            while (gx + run < GW && kind[gy][gx + run] && col[gy][gx + run] == c) run++;
            const int x0 = ox + (gx * h2 >> 1), x1 = ox + ((gx + run) * h2 >> 1);
            t.fillRect(x0, y0, x1 - x0, y1 - y0, c);
            gx += run;
        }
    }
}

// Asks the board for its battery once a second, and draws the lamp when there
// is a cell to show.
// The battery as last read: once a second at most. False with no cell to show.
static bool battRead(uint32_t now, uint8_t& pctOut, bool& chgOut) {
    if (!s_battSrc) return false;
    static uint32_t at = 0;
    static bool     has = false, chg = false;
    static uint8_t  pct = 0;
    if (!at || now - at >= 1000) {
        at = now ? now : 1;
        has = s_battSrc(pct, chg);
        if (pct > 100) pct = 100;
    }
    pctOut = pct; chgOut = chg;
    return has;
}

static void drawBatteryGauge(TFT_eSPI& t, uint32_t now) {
    // TERMINAL MODE has its own battery, in the LOG's title bar: the lamp
    // stood on the LOG's title and its TIME heading.
    if (Settings::boringMode() || !Settings::lavaLamp()) return;   // LAVA LAMP OFF (#132)
    uint8_t pct; bool chg;
    if (!battRead(now, pct, chg)) return;
    int h2, ox, oy;
    lampPlace(t, h2, ox, oy);
    if (DrawBand::has(oy, oy + (21 * h2 >> 1))) drawLavaLamp(t, now, pct, chg);
}

#if defined(TWATCH_S3)
void twatchGpsBadge(TFT_eSPI& t);   // main.cpp: the GPS counter, top centre, while the GPS is on
#endif
#if SQUACH_LORA && (defined(TWATCH_S3) || defined(TDECK_PLUS))
void twatchLoraBadge(TFT_eSPI& t);  // main.cpp: LORA n NEW, under it, while chats are unread
#endif
#if defined(TWATCH_S3)
static int16_t drawCornerClock(TFT_eSPI& t, int w) {
    if (!Clock::trusted()) return -1;
    char tm[8];
    Clock::formatTime(tm, sizeof tm, !Settings::clock24Hour());
    t.setTextSize(2);
    const int icons = Theme::titleBarRightIconsX(w);
    const int right = icons - (icons < w ? 2 : 4);
    const int tw = t.textWidth(tm) - 2;   // no spacing column after the last glyph
    const int x = right - tw;
    t.fillRect(x - 3, 0, tw + 6, 20, TFT_BLACK);
    t.setTextColor(Theme::CYAN, TFT_BLACK);
    t.setCursor(x, 3);
    t.print(tm);
    return (int16_t)(x - 3 - 4);
}
#endif
#if defined(CYD_GPS)
// The GPS label, left of the right-hand icons in the top bar: there only
// while the GPS has a fresh fix (or a bench fix typed in on the console), and
// always red, whatever the theme or the rain colour (2026-10-18). Drawn
// straight after the background like the watch's corner clock. Returns where
// the WATCH pill's free span must end, or -1.
static const int GPS_LABEL_W_PLAIN = 22;
#if defined(CARDPUTER_ADV)
// With wardrive on, the label is a wardrive tag: green rows written, amber
// satellites heard while it waits, red when there is no GPS or no card.
// With it off, the label is the one every GPS board has.
static const int GPS_LABEL_W_WD = 52;
static int gpsLabelW() { return Wardrive::enabled() ? GPS_LABEL_W_WD : GPS_LABEL_W_PLAIN; }
static bool gpsLabelShown() { return Wardrive::enabled() || Gnss::fresh(millis()) || Gnss::faked(); }
static void wardriveTag(char* buf, size_t n, uint16_t& col) {
    const uint32_t now = millis();
    const bool talking = Gnss::lastGoodMs() && now - Gnss::lastGoodMs() < 5000;
    const Wardrive::SdState st = Wardrive::sdState(now);
    const uint32_t r = Wardrive::written();
    if (st == Wardrive::SdState::NO_CARD)         { snprintf(buf, n, "NO CARD"); col = Theme::RED; }
    else if (st == Wardrive::SdState::LOW_MEMORY) { snprintf(buf, n, "WD MEM"); col = Theme::RED; }
    else if (!talking)                            { snprintf(buf, n, "NO GPS"); col = Theme::RED; }
    else if (st == Wardrive::SdState::LOGGING) {
        if (r < 10000) snprintf(buf, n, "WD %lu", (unsigned long)r);
        else           snprintf(buf, n, "WD %lu.%luK", (unsigned long)(r / 1000), (unsigned long)(r / 100 % 10));
        col = Theme::GREEN;
    }
    else { snprintf(buf, n, "WD %u SAT", Gnss::sky().heard); col = Theme::AMBER; }
}
#else
static int gpsLabelW() { return GPS_LABEL_W_PLAIN; }
static bool gpsLabelShown() { return Gnss::fresh(millis()) || Gnss::faked(); }
#endif
static int16_t drawGpsLabel(TFT_eSPI& t, int w) {
    if (!gpsLabelShown()) return -1;
    uint16_t col = 0xF800;          // TFT_RED, not a theme colour
    char txt[12] = "GPS";
#if defined(CARDPUTER_ADV)
    if (Wardrive::enabled()) wardriveTag(txt, sizeof txt, col);
#endif
    const int lw = gpsLabelW();
    const int right = Theme::titleBarRightIconsX(w) - 2;
    const int x = right - lw;
    t.setTextSize(1);
    t.fillRect(x, 0, lw, 20, TFT_BLACK);
    t.setTextColor(col, TFT_BLACK);
    t.setCursor(x + (lw - (t.textWidth(txt) - 1)) / 2, 6);
    t.print(txt);
    return (int16_t)(x - 4);
}
#endif
static int16_t s_wpX = 0, s_wpY = 0, s_wpW = 0, s_wpH = 0;

// spanR: the right end of the free span. -1 keeps the old fixed reserve for
// the rotate button and the padlock; the watch passes the corner clock's
// left edge instead, which already sits left of both.
static void drawWatchPill(TFT_eSPI& t, int screenW, bool watching, bool hunting, int spanR = -1) {
    // HUNT wins the label when both are set: it is the active, look-at-me mode.
    // The two are independent slots (see DetectionEngine), so both can be on.
    const char* txt = hunting ? "HUNT" : "WATCH";
    const uint16_t accent = hunting ? Theme::AMBER : Theme::CYAN;
    t.setTextSize(1);
    // 16 in a 20px bar: two rows of clearance top and bottom.
    const int bh = 16;
    const int bw = 16 + t.textWidth(txt) + 7;
    // Left edge of the free span, past the gear. The right limit is the rotate
    // icon (28) plus the lock (26) -- reserve both whether or not either is
    // showing, so the pill cannot move when a PIN is set or rotation locked.
    const int spanL = 32;
    if (spanR < 0) spanR = screenW - 54;
    int x = spanL + ((spanR - spanL) - bw) / 2;
    if (x < spanL) x = spanL;
    const int y = (20 - bh) / 2;
    t.fillRoundRect(x, y, bw, bh, 4, Theme::BG);
    t.drawRoundRect(x, y, bw, bh, 4, accent);
    // An eye: open for a passive watch, with a line through it for a hunt.
    t.drawCircle(x + 9, y + bh / 2, 4, accent);
    t.fillCircle(x + 9, y + bh / 2, 1, accent);
    if (hunting) t.drawFastHLine(x + 3, y + bh / 2, 12, accent);
    t.setTextColor(accent, Theme::BG);
    t.setCursor(x + 16, y + (bh - 8) / 2);
    t.print(txt);
    // A finger-sized target: the bar is only 20px tall, so grow downward.
    s_wpX = (int16_t)(x - 4); s_wpY = (int16_t)0;
    s_wpW = (int16_t)(bw + 8); s_wpH = (int16_t)(bh + 14);
    s_watchPillOn = true;
}

// The counter tiles as last drawn, each with the column it counts.
struct CounterTile { int16_t x, y, w, h; DetectionType type; };
static const uint8_t TILE_MAX = 12;
static CounterTile s_tiles[TILE_MAX];
static uint8_t     s_tileN = 0;

// The NEARBY headline's last drawn rectangle, grown to a finger-sized target.
// CLASSIC only.
static bool    s_nearbyOn = false;
static int16_t s_nbX = 0, s_nbY = 0, s_nbW = 0, s_nbH = 0;

bool uiClearNearbyHit(int x, int y) {
    return s_nearbyOn &&
           x >= s_nbX && x < s_nbX + s_nbW && y >= s_nbY && y < s_nbY + s_nbH;
}

bool uiClearCounterHit(int x, int y, DetectionType& column) {
    // A few pixels past the tile each way: they are only 12 rows tall.
    for (uint8_t i = 0; i < s_tileN; i++) {
        const CounterTile& c = s_tiles[i];
        if (x >= c.x - 3 && x < c.x + c.w + 3 && y >= c.y - 3 && y < c.y + c.h + 3) {
            column = c.type;
            return true;
        }
    }
    return false;
}

uint8_t uiClearCounterStops(int16_t* xs, int16_t* ys, uint8_t cap) {
    uint8_t n = 0;
    for (uint8_t i = 0; i < s_tileN && n < cap; i++, n++) {
        xs[n] = (int16_t)(s_tiles[i].x + s_tiles[i].w / 2);
        ys[n] = (int16_t)(s_tiles[i].y + s_tiles[i].h / 2);
    }
    return n;
}

bool uiClearWatchPillHit(int x, int y) {
    return s_watchPillOn &&
           x >= s_wpX && x < s_wpX + s_wpW && y >= s_wpY && y < s_wpY + s_wpH;
}

bool uiClearSquadHit(int x, int y) {
    return s_badgeOn && !Settings::boringMode() &&
           x >= s_badX && x < s_badX + s_badW && y >= s_badY && y < s_badY + s_badH;
}

// ---- the crowd ---------------------------------------------------------------
// Everybody in range at once, drifting rather than standing. Two things make
// this cheap enough to be worth having, both measured on hardware rather than
// reasoned about: drawing cost follows the AREA a Squachy covers, so they get
// cheaper as fast as they get more numerous; and eight at the size they land
// on (about 0.72) cost 17 ms a frame against 29 ms for today's two at 2.00.
//
// Their drift is a pure function of `now` and the cell each one stands in, the
// same discipline the backgrounds follow, so a board that renders in bands
// cannot tear them and a peer arriving mid-frame cannot shuffle them.
//
// Two things are kept between frames, and only two: where each of them was
// drawn, so a tap can find one, and which one was last asked to say his name.
static const uint32_t CROWD_NAME_MS = 3000;
static bool     s_tapName   = false;
static uint8_t  s_tapMac[6] = { 0 };
static uint32_t s_tapUntil  = 0;
// Remembered by ADDRESS, never by index: the squad list re-sorts as adverts
// arrive, and an index would hang the name on whoever inherited the slot.
// Seven at most -- the eighth body in a crowd of eight is always ours, and
// ours is not in here because a tap on him pets him instead.
static uint8_t  s_crowdN = 0;
static int16_t  s_crowdX[8], s_crowdHalf[8], s_crowdTop[8], s_crowdBot[8];
static uint8_t  s_crowdMac[8][6];

void uiClearDrawCrowd(TFT_eSPI& t, uint32_t now, const Mesh::SquadMember* crowd,
                      uint8_t n, int top, int floorY, bool advance, bool msgFresh,
                      int bottomInset, float grow, int bubbleY) {
    const int w = t.width();
    // Read by the emotes and the set pieces, which stand down for a crowd.
    s_crowdDrawn = true;
    // Ours plus theirs. squadList() reports the peers it can hear and NEVER
    // this board, so the body count is one higher than the list is long.
    // Sizing the screen for `n` and then spending slot zero on ourselves is
    // what quietly dropped a visitor: with four in range you saw three.
    const uint8_t total = (uint8_t)(n + 1);
    // Clear of the squad badge and the counters on the main screen. The desk
    // has neither under its band, so it asks for none.
    const int bottom = floorY - bottomInset;

    // The shape is chosen, not fixed: one, two or three rows, whichever lets
    // them stand LARGEST, and never more than four across -- five across
    // reads as a queue where four still reads as a group. The back rows are
    // full and the front row holds whatever is left over, centred.
    //
    // Rows OVERLAP, the way a group photo does: the row in front stands over
    // the legs of the row behind, never over a face. Two rows used to share
    // the band half and half with a gap between them, which is what held a
    // landscape crowd of five at x1.0 while one on his own stands at x1.9.
    //
    // A body is 56 wide and 74 tall in scale units. A body may be wider than
    // its cell: they stand shoulder to shoulder and pass in front of one
    // another as they drift. `sMax` is the size he stands at alone, so a
    // small crowd never outgrows him.
    const float OVERLAP = 0.35f;   // of a body's height, covered by the row in front
    // No room is kept above the back row for hats and horns: a horn in the
    // title bar is a better trade than the whole crowd standing smaller.
    const float bandH = (float)(bottom - top);
    const float sMax = (float)(floorY - top) / 91.0f;
    float fitW = 1.15f, fitH = 1.00f;
#ifndef ARDUINO
    // Emulator only: SQUACHSIM_CROWDFIT="w,h" tries another pair of margins,
    // for rendering the choices side by side before one of them ships.
    if (const char* fit = getenv("SQUACHSIM_CROWDFIT")) sscanf(fit, "%f,%f", &fitW, &fitH);
#endif
    int rows = 1, cols = total;
    float s = 0.0f;
    for (int r = 1; r <= 3; r++) {
        const int c = ((int)total + r - 1) / r;
        if (c > 4) continue;
        if ((r - 1) * c >= (int)total) break;          // a row with nobody in it
        const float stack = 1.0f + (float)(r - 1) * (1.0f - OVERLAP);
        float sr = fminf(fitW * ((float)(w - 8) / (float)c) / 56.0f,
                         fitH * bandH / (74.0f * stack));
        if (sr > sMax) sr = sMax;
        // A clear win or nothing: at a tie the fewer rows read better.
        if (sr > s * 1.03f) { s = sr; rows = r; cols = c; }
    }
    const float cw = (float)(w - 8) / (float)cols;
    // Asked to stand bigger than the band would choose: past the size he
    // stands at alone, but never taller than the band, and never so wide
    // they stand in each other rather than beside each other.
#ifndef ARDUINO
    // Emulator only, beside SQUACHSIM_CROWDFIT: SQUACHSIM_CROWDK=k scales the
    // crowd by k (either way), under the same two caps as `grow`.
    if (const char* kk = getenv("SQUACHSIM_CROWDK")) {
        const float k = (float)atof(kk);
        if (k > 0.0f) s = fminf(s * k, fminf(bandH / 74.0f, 1.3f * cw / 56.0f));
    }
#endif
    if (grow > 1.0f) {
        s = fminf(s * grow, fminf(bandH / 74.0f, 1.3f * cw / 56.0f));
    }
    const float bodyH = 74.0f * s, pitch = bodyH * (1.0f - OVERLAP);
    const float used = bodyH + (float)(rows - 1) * pitch;
    // The back row's feet, with the band's spare height split above and below.
    const float feet0 = (float)top + (bandH - used) * 0.5f + bodyH;
    // How far they bob: a tenth of the band when there is one row, a tenth of
    // the step between rows when there are more, so a row never walks into
    // the faces behind it.
    const float bob = 0.10f * (rows == 1 ? bandH : pitch);

    // Where a cell sits, and how far its occupant has wandered off the middle
    // of it. `drift` is what ours is denied.
    // Half a body each way: for keeping them on the screen, and for standing
    // them in the middle of their own patch of it.
    const int halfW = (int)(28.0f * s);
    const int lastRow = rows - 1;
    const int inLast = (int)total - cols * lastRow;
    auto cellX = [&](int k, int i, bool drift) {
        const float dx = drift ? sinf((float)now / 1900.0f + (float)i * 1.7f) * cw * 0.15f : 0.0f;
        // A short front row stands centred, in the gaps of the row behind.
        const float shift = (k / cols == lastRow) ? (float)(cols - inLast) * cw * 0.5f : 0.0f;
        int x = 4 + (int)(((float)(k % cols) + 0.5f) * cw + shift + dx);
        // Nobody drifts off an edge. The left-hand cell was taking his box to
        // x=-2, which is half a Squachy hanging off the side of the screen.
        if (x < halfW + 2)     x = halfW + 2;
        if (x > w - halfW - 2) x = w - halfW - 2;
        return x;
    };
    // His FEET. With one row that puts the body in the middle of the band
    // rather than along the bottom of it, which is the whole of roaming.
    auto cellY = [&](int k, int i, bool drift) {
        const float dy = drift ? sinf((float)now / 2600.0f + (float)i * 0.9f) * bob : 0.0f;
        return (int)(feet0 + (float)(k / cols) * pitch + dy);
    };

    // Who is visiting, if anybody. He keeps the conversation -- his line, his
    // nodding, his laugh -- and the rest are company.
    int guestI = -1;
    for (uint8_t i = 0; i < n; i++)
        if (Mesh::peer() && memcmp(Mesh::peerMac(), crowd[i].mac, 6) == 0) { guestI = (int)i; break; }

    // Ours takes the FRONT row's most central cell and holds it. In a crowd
    // where everything is moving, the one you actually control is the one you
    // have to be able to pick out, and a seat that never moves -- and that
    // nobody stands in front of -- picks itself out. The visitor stands in
    // the front row beside him, because he and ours are drawn last (they
    // hold the bubbles) and a back-row body drawn last would stand over the
    // faces in front of it.
    auto nearest = [&](const bool* taken, float toX, bool frontOnly) {
        int best = -1; float bestD = 0.0f;
        for (int k = frontOnly ? cols * lastRow : 0; k < (int)total; k++) {
            if (taken[k]) continue;
            const float d = fabsf((float)cellX(k, 0, false) - toX);
            if (best < 0 || d < bestD) { best = k; bestD = d; }
        }
        return best;
    };
    bool taken[8] = { false };
    const int selfIdx = nearest(taken, (float)w * 0.5f, true);
    taken[selfIdx] = true;
    int cellOf[8] = { 0 };
    if (guestI >= 0) {
        int g = nearest(taken, (float)cellX(selfIdx, 0, false), true);
        if (g < 0) g = nearest(taken, (float)cellX(selfIdx, 0, false), false);
        cellOf[guestI] = g;
        taken[g] = true;
    }
    {
        int k = 0;
        for (uint8_t i = 0; i < n; i++) {
            if ((int)i == guestI) continue;
            while (taken[k]) k++;
            cellOf[i] = k;
            taken[k] = true;
        }
    }
    // Which cell peer `i` gets.
    auto peerCell = [&](uint8_t i) { return cellOf[i]; };

#ifndef ARDUINO
    // Emulator only: the geometry itself, not where bodies happened to land.
    // Two fixes aimed at selfIdx and the row headroom changed nothing on
    // screen, which means the numbers are not what they are being read as.
    if (getenv("SQUACHSIM_CROWDBOX"))
        fprintf(stderr, "[crowdgeom] total=%u cols=%d rows=%d s=%.3f top=%d bottom=%d "
                        "cw=%.1f pitch=%.1f selfIdx=%d selfX=%d selfY=%d\n",
                (unsigned)total, cols, rows, (double)s, top, bottom,
                (double)cw, (double)pitch, selfIdx,
                cellX(selfIdx, 0, false), cellY(selfIdx, 0, false));
#endif

    // Nameplates stop being labels and start being clutter somewhere around
    // five: past that they come off, and a tap puts one back for a few
    // seconds. Four or fewer and everybody keeps his, which is the size where
    // they still read.
    const bool allNames = total <= 4;
    if (s_tapName && (int32_t)(s_tapUntil - now) <= 0) s_tapName = false;
    s_crowdN = 0;

    auto wantsName = [&](uint8_t i) {
        return allNames || (s_tapName && memcmp(s_tapMac, crowd[i].mac, 6) == 0);
    };
    auto peerName = [&](uint8_t i) -> const char* {
        const SquachMesh::Peer& p = crowd[i].peer;
        return (p.custom && p.name[0]) ? p.name : Squachy::nicknameAt(p.nick);
    };

    // One of them, body only. Records where he landed so a tap can find him,
    // and reports whether he is holding a bubble.
    auto drawOne = [&](uint8_t i, int cx, int baseY, bool isGuest) {
        const SquachMesh::Peer& p = crowd[i].peer;
        Squachy::setOutfitPreview((int8_t)p.outfit);
        Squachy::setShadesPreview((int8_t)p.shade);
        Squachy::setAuraPreview(p.aura ? 1 : 0);
        // His name, on a sticker on his chest, so it is his and moves with
        // him. Four or fewer and everybody wears one; past that only the
        // one who was tapped, for a few seconds.
        Squachy::setNameTag(wantsName(i) ? peerName(i) : nullptr);
        // A phone (the companion app) wears the WATCH screen's headset: it can't
        // catch anything itself, and at a glance it looks like any other board.
        Squachy::setHeadset(p.phone);
        const char* line = (isGuest && !msgFresh) ? s_visitGuestLine : nullptr;
        // His bubble hangs from the caller's row when it names one; the gap
        // is taken from where his head is this frame, so the bubble holds
        // still while he drifts.
        const int gap = bubbleY >= 0 ? (baseY - (int)(58.0f * s)) - bubbleY : 18;
        // Nobody waves for ever. A third of them used to, every frame, which
        // with a single visitor meant him: he stood there waving for the
        // whole visit. The visitor waves while he is saying hello, the same
        // as on the ground; the rest give a wave now and then, each on his
        // own clock -- three seconds in every twenty-five.
        const bool waving = isGuest ? guestGreeting()
                                    : ((now + (uint32_t)i * 8111u) % 25000u) < 3000u;
        Squachy::drawWaving(t, cx, baseY, now + (uint32_t)i * 137u, s, line,
                            line != nullptr, 0, waving, gap, isGuest && now < s_guestLaughUntil,
                            isGuest && !s_guestTurn, line != nullptr,
                            isGuest ? guestPose(now) : Squachy::VisitPose::NONE);
        Squachy::setNameTag(nullptr);
        Squachy::setHeadset(false);
        Squachy::setAuraPreview(-1);
        Squachy::setShadesPreview(-1);
        Squachy::setOutfitPreview(-1);
        if (s_crowdN < 8) {
            int half = (int)(30.0f * s);
            if (half < 14) half = 14;      // a fingertip, however small he is
            s_crowdX[s_crowdN]    = (int16_t)cx;
            s_crowdHalf[s_crowdN] = (int16_t)half;
            s_crowdTop[s_crowdN]  = (int16_t)(baseY - (int)(62.0f * s));
            s_crowdBot[s_crowdN]  = (int16_t)(baseY + 4);
            memcpy(s_crowdMac[s_crowdN], crowd[i].mac, 6);
#ifndef ARDUINO
            // Emulator only, and off unless asked: where the tap targets are.
            // Aiming a tap by eye at a screenshot missed twice, which is two
            // render cycles spent proving nothing about the feature itself.
            if (getenv("SQUACHSIM_CROWDBOX"))
                fprintf(stderr, "[crowdbox] %u x=%d..%d y=%d..%d centre %d,%d\n",
                        (unsigned)s_crowdN, cx - half, cx + half,
                        (int)s_crowdTop[s_crowdN], (int)s_crowdBot[s_crowdN],
                        cx, (baseY + (int)s_crowdTop[s_crowdN]) / 2);
#endif
            s_crowdN++;
        }
        // Where the newest of them is, so the message bubble has somewhere to
        // point. The visitor wins it when he is here.
        if (isGuest || !s_msgGuestOn) {
            s_msgGuestOn = true;
            s_msgGx      = cx;
            s_msgHeadTop = baseY - (int)(58.0f * s);
        }
        return line != nullptr;
    };


    // The order of what follows is the whole point of it.
    //
    // At eight, a speech bubble is wider than the patch its owner stands in,
    // so it WILL cross a neighbour -- there is no layout where it doesn't.
    // Drawn last it crosses him the way a speech bubble is meant to, in front,
    // instead of being half-painted over by whoever happened to come after.
    // So: the silent ones go down first, then the only two who can be holding
    // a bubble -- the visitor, and ours. Names ride on the bodies now.

    // 1. everybody who is neither talking nor us -- back row first, so the
    //    row in front stands over its legs. Their cells rise with `i`.
    for (uint8_t i = 0; i < n; i++) {
        if ((int)i == guestI) continue;
        const int k = peerCell(i);
        drawOne(i, cellX(k, i, true), cellY(k, i, true), false);
    }
    // 2. the visitor. His bubble is held back and drawn after ours (step 4):
    //    ours sits in the middle seat, and drawn after the words he stood on
    //    them -- which crowds of phones and boards on a bench made common.
    if (guestI >= 0) {
        const uint8_t i = (uint8_t)guestI;
        const int k = peerCell(i);
        const int cx = cellX(k, i, true), baseY = cellY(k, i, true);
        Squachy::deferBubble(true);
        drawOne(i, cx, baseY, true);
        Squachy::deferBubble(false);
    }
    // 3. and ours last of all, in the seat that does not move. He is drawn
    //    through tick(), which owns his moods, his quips and his bubble, so he
    //    keeps all of that while the others drift around him.
    {
        const int cx = cellX(selfIdx, 0, false), baseY = cellY(selfIdx, 0, false);
        // He has to come out the same size as the cameos around him, and
        // tick() works that out from numbers only it knows -- the bubble
        // row it reserves, the headroom his crest and his costume need,
        // and a base height that is not the one a cameo scales against.
        // Reproducing that arithmetic here is how he ended up towering
        // over everybody.
        //
        // So it is measured instead of derived: lastScale() is written by
        // tick() alone (never by the cameo path), so it says what HE came
        // out at last frame, and the percentage is nudged toward whatever
        // lands him on the crowd's scale. It settles within a few frames
        // and survives anything tick() changes about its own sizing.
        //
        // Only while he is off by more than one step of that percentage. He
        // can only land on whole steps, so he is never exactly on the crowd's
        // size; chasing the leftover sliver crept the correction along until
        // the rounding flipped, and every eighth frame or so he shrank for a
        // frame and popped back -- the twitch the desk showed, worst there
        // because a pair stands big enough for one step to be a pixel.
        static float corr = 0.85f;
        static int   lastPct = 100;
        const float got = Squachy::lastScale();
        const float tol = 1.2f / (float)(lastPct > 10 ? lastPct : 10);
        if (got > 0.05f && s > 0.05f && fabsf(got / s - 1.0f) > tol) {
            float want = corr * s / got;
            if (want < 0.2f) want = 0.2f;
            if (want > 3.0f) want = 3.0f;
            corr += (want - corr) * 0.35f;
        }
        // His bubble rises 16 above the top of the band he is handed, so a
        // caller's bubble row moves that top up to match. The size is a
        // percentage of the band, so it follows along.
        const int bandTop = bubbleY >= 0 ? bubbleY + 16 : top;
        const int band = baseY - bandTop;
        int pct = band > 1 ? (int)(corr * s * 56.0f * 100.0f / (float)band) : 100;
        if (pct < 10)  pct = 10;
        if (pct > 100) pct = 100;
        lastPct = pct;
        Squachy::setCompany(true);
        Squachy::tick(t, cx, bandTop, band, now, advance, 0.3f, false, 0, (uint8_t)pct);
        Squachy::setCompany(false);
    }
    // 4. the visitor's words, over everybody.
    Squachy::drawDeferredBubble(t, now);
}

// A tap on one of the crowd: puts his name up for a few seconds. Ours is
// deliberately not in the list -- a tap on him pets him, which is what a tap
// on him has always done, and asking a Squachy you own who he is is not a
// question anybody has.
bool uiClearCrowdTap(int x, int y, uint32_t now) {
    if (Settings::boringMode()) return false;
    // Newest drawn first: where two overlap, the one in front is the one
    // the finger is on.
    for (int i = (int)s_crowdN - 1; i >= 0; i--) {
        if (x < s_crowdX[i] - s_crowdHalf[i] || x > s_crowdX[i] + s_crowdHalf[i]) continue;
        if (y < s_crowdTop[i] || y > s_crowdBot[i]) continue;
        memcpy(s_tapMac, s_crowdMac[i], 6);
        s_tapName  = true;
        s_tapUntil = now + CROWD_NAME_MS;
        return true;
    }
    return false;
}

static void drawMessageUi(TFT_eSPI& t, uint32_t now, int titleBottom, int squachyBottom) {
    s_bubbleOn = false;
    // The tutorial runs before anybody has a phrase, so it shows the bubble
    // regardless -- finding it is the thing it teaches.
    const bool tut = MeshTutor::active();
    if (!MeshTalk::ready() && !tut) return;
    const MeshTalk::Message& m = MeshTalk::inbox();
    const int w = t.width();
    // Where the visitor is, or where he would stand if there were one.
    const int gx   = s_msgGuestOn ? s_msgGx : w / 2 + w / 4;
    const int head = s_msgGuestOn ? s_msgHeadTop
                                  : titleBottom + (squachyBottom - titleBottom) / 3;
    const bool showing = tut ? tutorReply() : messageShowing(now);
    if (showing) {
        if (tut) drawRedBubble(t, gx, head, MeshTutor::DEMO_NAME, MeshTutor::DEMO_REPLY);
        else     drawRedBubble(t, gx, head, m.from, MeshTalk::lineText(m));
    }
    // Only with somebody around -- or something unread from somebody who was.
    if (s_msgGuestOn || (m.unread && !tut)) {
        int ix = gx + 26;
        // Beside his head rather than on it on the big panels, where he is
        // twice the size the 26 was measured against and it sat on his
        // sunglasses. The other boards keep their 26 exactly.
        if (Theme::large() && s_msgGuestOn) {
            const int beside = (int)(29.0f * s_msgGs);
            if (beside > 26) ix = gx + beside;
        }
        if (ix + MSG_ICON_W > w - 4) ix = w - 4 - MSG_ICON_W;
        // Dropped clear of the red bubble while one is up: at the larger
        // size the two overlapped at the corner and, both red, read as one.
        int iy = head + 4 + (showing ? 8 : 0);
        // And clear of his own speech bubble. On the 3.5" his lines wrap to
        // two or three double-size rows that hang down past his head, right
        // through where the button sits (2026-10-12): drop it under them.
        if (s_msgGuestOn && s_msgGbOn &&
            ix < s_msgGbX + s_msgGbW && ix + MSG_ICON_W > s_msgGbX &&
            iy < s_msgGbY + s_msgGbH + 2 && iy + MSG_ICON_H + 6 > s_msgGbY)
            iy = s_msgGbY + s_msgGbH + 4;
        drawMessageIcon(t, ix, iy,
                        tut ? showing : m.unread, tut ? false : MeshTalk::sending(now), now);
    }
}

bool uiClearBubbleHit(int x, int y) {
    return s_bubbleOn && !Settings::boringMode() &&
           x >= s_bubX && x < s_bubX + s_bubW && y >= s_bubY && y < s_bubY + s_bubH;
}

// Received emotes. See ui_clear.h: called from main.cpp's loop rather than
// from the draw, because the draw does not happen on every frame and an emote
// that arrives on one of the others must not be lost.
//
// HELD, not tested and dropped. takeEmote() consumes: it hands the emote over
// and forgets it. It was once the first term of an && chain, so every
// condition after it filtered something already off the queue -- and anything
// arriving a moment before the visit machine was ready for it was gone for
// good. Now it is parked and retried until it plays or twelve seconds pass.
//
// EVERY AGE IN HERE IS SIGNED, and that is the bug that kept received emotes
// from ever playing on hardware. `now` is read once at the top of loop();
// the frame is decrypted and queued some milliseconds later, stamped from
// millis(). So the entry's `at` sits a few ms AHEAD of `now`, and the unsigned
// `now - at` wraps to four billion -- older than any window -- and the entry
// was expired in the same call that queued it. Measured: at=20549, now=20541,
// age=4294967288. The emulator could not reproduce it because its clock does
// not move within a frame, and the sender never hit it because its own emote
// is queued on one iteration and checked on the next, when `now` has caught
// up. The signed form treats a stamp from the near future as an age of zero,
// which is what it is.
void uiClearEmoteTick(uint32_t now) {
    {
        MeshTalk::EmoteIn in;
        if (MeshTalk::takeEmote(in)) { s_rxEmote = in; s_rxHave = true; }
        if (s_rxHave) {
            if ((int32_t)(now - s_rxEmote.at) >= (int32_t)EMOTE_WAIT_MS) {
                s_rxHave = false;
                Serial.printf("[visit] emote %u dropped: no visit with the sender in %lus\n",
                              (unsigned)s_rxEmote.emote, (unsigned long)(EMOTE_WAIT_MS / 1000));
            } else if (Mesh::peer() && memcmp(Mesh::peerMac(), s_rxEmote.mac, 6) == 0 &&
                       uiClearEmote(s_rxEmote.emote, s_rxEmote.setup, true)) {
                s_rxHave = false;
            }
        }
        // Anything that has waited out its welcome goes, and says so. Signed --
        // see the comment on this function.
        while (s_eqN && (int32_t)(now - s_eq[0].at) > (int32_t)EMOTE_WAIT_MS) {
            Serial.printf("[visit] emote %u expired after %lus unplayed\n",
                          (unsigned)s_eq[0].emote, (unsigned long)(EMOTE_WAIT_MS / 1000));
            for (uint8_t i = 1; i < s_eqN; i++) s_eq[i - 1] = s_eq[i];
            s_eqN--;
        }

        // And acted out here, in the loop, rather than from inside the draw --
        // the draw only runs on frames that paint Squachy, and an emote must
        // not depend on one of those coming along.
        //
        // An emote INTERRUPTS. Somebody pressed a button and is watching for
        // it; the set pieces and the naps start themselves and can wait. Not
        // during the walk in or out, which own his position -- under two
        // seconds -- and not with no visit at all.
        if (s_eqN && s_vp != VisitPhase::GONE && s_vp != VisitPhase::ARRIVING &&
            s_vp != VisitPhase::LEAVING) {
            s_piece     = Piece::NONE;      // whatever was running, this is louder
            if (s_vp == VisitPhase::HIGH_FIVE || s_vp == VisitPhase::STEP_BACK) {
                s_vp = VisitPhase::MEETING;
                s_vpAt = now; s_beatAt = now; s_beatMs = 0;
            }
            emoteStart(now);
        }
    }
}

static void visitTick(uint32_t now) {
    const uint32_t id = rawGuestId(now);

    // A scare, live: the host has just reacted to a detection here on this
    // screen, so the guest jumps with him. Fresh ones only -- one from before
    // he arrived is not his to react to.
    const bool talking = (s_vp == VisitPhase::MEETING || s_vp == VisitPhase::HANGING);
    const uint32_t shock = Squachy::lastShockAt();
    if (shock != s_seenShock) {
        s_seenShock = shock;
        if (talking && now - shock < 500) {
            s_guestStartleUntil = now + 1400;
            // Only into a gap: a line he is already saying stays said.
            if (!s_visitGuestLine) s_visitGuestLine = Squachy::visitScareLine(s_beatNo);
            Serial.println("[visit] shared scare");
        }
    }

    // Told once per frame rather than on transitions, so it cannot get stuck
    // set if a phase change is ever missed. The same goes for listening: it
    // is a per-frame statement of who is quiet right now, not an event.
    Squachy::setVisiting(s_vp != VisitPhase::GONE);
    Squachy::setListening((s_vp == VisitPhase::MEETING ||
                           s_vp == VisitPhase::HANGING) && s_guestTurn);
    leanTick(now);

    if (s_vp == VisitPhase::GONE) {
        if (!id) return;
        const SquachMesh::Peer* g = rawGuest(now);
        if (!g) return;
        s_hosting   = *g;                   // copy on arrival; live same-peer edits refresh below
        s_hostingId = id;
        s_hostingGoblin = (s_rawGoblin != nullptr);
        if (s_hostingGoblin) {
            s_goblinHost = *s_rawGoblin;
            Squachy::setGoblinGuest(s_goblinHost.name, s_goblinHost.level, s_goblinHost.hoard);
            Serial.printf("[visit] goblin \"%s\" level %u\n", s_goblinHost.name, (unsigned)s_goblinHost.level);
        }
        s_vp = VisitPhase::ARRIVING; s_vpAt = now;
        Sound::play(Sound::Id::VISITOR);
        s_guestTurn = true;                 // so the HOST speaks first
        s_hangStep  = 0;                    // and asks the first question
        s_guestLaughUntil = 0;              // no laughter carried in from
        s_piece = Piece::NONE;              // nor a set piece, a scare or a nap
        s_nextPieceAt = 0;
        s_guestStartleUntil = 0;
        // Seen before, this boot? Then it is a handshake, not a high five.
        s_oldFriend = friendSeen(id);
        if (!s_oldFriend) friendAdd(id);
        s_friendGreeted = false;
        s_fiveSteps  = s_oldFriend ? 3 : 1;
        s_fiveStepMs = s_oldFriend ? FRIEND_STEP_MS : FIVE_MS;
        s_fiveHitMs  = s_oldFriend ? FRIEND_HIT_MS : FIVE_HIT_MS;
        s_fiveStep   = 0;
        s_visitGuestLine = nullptr;         // whoever was here before
        return;
    }

    // A DIFFERENT guest, or none. Either way the one being hosted has to
    // leave properly rather than being quietly replaced -- which is exactly
    // what used to happen: the synthetic visitor took the slot from a real
    // one and the guest appeared to change outfit instead of walking off.
    // The same handover applies to two real peers, one arriving as another
    // goes.
    if (id != s_hostingId && s_vp != VisitPhase::LEAVING) {
        // Whatever they were in the middle of ends with the visit.
        s_piece = Piece::NONE;
        s_vp = VisitPhase::LEAVING;
        visitBeat(now, Squachy::VisitMoment::PART);
        s_vpAt = now + s_beatMs;            // goodbye first, then the walk
        return;
    }
    // Same guest, same identity: follow their newest appearance without
    // resetting the visit's phase, high-five, conversation, or emote state.
    // The radio's Mesh::peer() is refreshed on every presence advertisement;
    // SQUAD already draws that current data. This updates our hosted COPY
    // only while the identical visitor is still here, never during LEAVING.
    if (id && id == s_hostingId && s_vp != VisitPhase::LEAVING && !s_hostingGoblin)
        MeshVisitAppearance::refresh(s_hosting, s_hostingId, id, rawGuest(now), false);

    // An emote INTERRUPTS. Somebody pressed a button and is watching for it;
    // the set pieces and the naps start themselves, unprompted, and can wait.
    //
    // This used to live in the HANGING case alone, which is the last of six
    // phases and twenty seconds into a visit -- so every emote sent while the
    // other board was still walking in, slapping hands or exchanging hellos
    // sat in the queue until it expired, unplayed and unlogged. That is the
    // whole window in which somebody actually sends one.
    //
    // Not while he is walking in or out: the arrival and the goodbye own his
    // position, and an emote's own choreography would teleport him. That wait
    // is under two seconds.
    switch (s_vp) {
        case VisitPhase::ARRIVING:
            // Straight up to the host and a high five before anybody says a
            // word -- and nobody talks while he is still walking. A greeting
            // delivered to somebody's back is a worse joke than no greeting.
            if (now - s_vpAt >= (encGuestHere() ? ENC_ARRIVE_MS : WALK_MS)) {
                s_vp = VisitPhase::HIGH_FIVE; s_vpAt = now; s_fiveStep = 0;
                Squachy::visitReach(now, s_fiveStepMs, Squachy::Reach::UP);
                Serial.println(s_oldFriend ? "[visit] handshake (old friend)" : "[visit] high five");
            }
            break;
        case VisitPhase::HIGH_FIVE: {
            const uint32_t e = now - s_vpAt;
            if (e >= (uint32_t)s_fiveSteps * s_fiveStepMs) {
                s_vp = VisitPhase::STEP_BACK; s_vpAt = now;
                Squachy::visitLaugh(now);           // pleased with that one
                s_guestLaughUntil = now + 900;
            } else {
                // The next slap of a handshake: low, then the fist bump.
                const uint8_t st = (uint8_t)(e / s_fiveStepMs);
                if (st != s_fiveStep) {
                    s_fiveStep = st;
                    Squachy::visitReach(now, s_fiveStepMs,
                                        st == 1 ? Squachy::Reach::DOWN : Squachy::Reach::LEVEL);
                }
            }
            break;
        }
        case VisitPhase::STEP_BACK:
            if (now - s_vpAt >= STEP_MS) {
                s_vp = VisitPhase::MEETING; s_vpAt = now;
                visitBeat(now, Squachy::VisitMoment::MEET);
            }
            break;
        case VisitPhase::MEETING:
            if (now - s_beatAt >= s_beatMs + TURN_GAP_MS) {
                if (s_guestTurn) {          // guest has answered; hello is done
                    s_vp = VisitPhase::HANGING; s_vpAt = now;
                    // A goblin does not dance, throw snowballs or play
                    // rock-paper-scissors: those are two Squachys' routines.
                    s_nextPieceAt = s_hostingGoblin ? 0 : now + PIECE_FIRST_MS + (uint32_t)random(0, 15000);
                    s_visitGuestLine = nullptr;
                    s_beatAt = now;
                    s_hangStep = 0;
                    s_beatMs = 500;         // half a breath, then straight in
                } else {
                    visitBeat(now, Squachy::VisitMoment::MEET);
                }
            }
            break;
        case VisitPhase::HANGING:
            // One line down, the next one up. Every beat is timed by the
            // line it is showing, so the rhythm comes from what is being
            // said rather than from a constant that has to suit both "Good."
            // and a full sentence.
            // A set piece holds the conversation until it is done.
            if (s_piece != Piece::NONE)  { pieceTick(now); break; }
            // A goblin stays a while, not for good: at the end of a line
            // after GOBLIN_STAY_MS he says goodbye and walks off.
            if (s_hostingGoblin && now - s_vpAt > GOBLIN_STAY_MS &&
                now - s_beatAt >= s_beatMs + TURN_GAP_MS) {
                s_goblinByeId = s_goblinHost.id;
                s_goblinByeAt = now;
                s_vp = VisitPhase::LEAVING;
                visitBeat(now, Squachy::VisitMoment::PART);
                s_vpAt = now + s_beatMs;
                break;
            }
            // Emotes are handled above, before the switch: they interrupt from
            // any phase with two Squachys on screen rather than only this one.
            if (now - s_beatAt >= s_beatMs + TURN_GAP_MS) {
                // No timed exit. He used to say goodbye after a minute and
                // then -- with the other board still right there -- walk
                // straight back in on the next frame, which on hardware read
                // as a Squachy who kept trying to leave. He goes when the
                // other one does: the id check above sends him off, with his
                // goodbye, the moment the peer is gone.
                // Not while a crowd is on screen: every set piece puts the two
                // of them on marks on the ground, and in a crowd there is no
                // ground -- they are drifting. The conversation carries on.
                if (s_hangStep == 0 && s_nextPieceAt && !s_crowdDrawn &&
                    (int32_t)(now - s_nextPieceAt) >= 0)
                    pieceStart(now);
                else
                    visitBeat(now, Squachy::VisitMoment::HANGOUT);
            }
            break;
        case VisitPhase::LEAVING:
            if ((int32_t)(now - s_vpAt) >= (int32_t)(encGuestHere() ? ENC_RISE_MS + 200 : WALK_MS)) {
                s_vp = VisitPhase::GONE;
                s_visitGuestLine = nullptr;
            }
            break;
        default: break;
    }
}

// When a real peer was last in range, so the demo does not pounce on the
// slot the instant one walks away. Without this, unplugging the other device
// swapped the synthetic visitor straight in and the guest appeared to change
// outfit rather than to leave.
static uint32_t s_realSeenAt = 0;
static const uint32_t DEMO_COOLDOWN_MS = 15000;

// A goblin in range that may visit: the emulator's first, then the radio's,
// and not the one that just left.
static const Goblin::Visitor* goblinHere(uint32_t now) {
    const Goblin::Visitor* g = s_encGobOn ? &s_encGob : s_simGoblin ? s_simGoblin : Mesh::goblin();
    if (!g) return nullptr;
    if (s_goblinByeAt && g->id == s_goblinByeId && now - s_goblinByeAt < GOBLIN_AGAIN_MS) return nullptr;
    return g;
}

// Whoever WOULD be visiting right now, before the visit machine has decided
// what to do about it. Not what gets drawn -- see visitHosting().
static const SquachMesh::Peer* rawGuest(uint32_t now) {
    // The tutorial's DEMO visitor comes first: while it runs, it is the
    // lesson, and a real peer turning up must not walk onto the stage.
    if (const SquachMesh::Peer* d = MeshTutor::guest()) return d;
    if (s_guest) return s_guest;          // the emulator's --peer, when set
    // CROWD NONE: nobody in range visits. The dice's visitors are not in
    // range, they are the board's own, and they have a switch of their own
    // (ENCOUNTERS): with this one in the way the die sat there unkicked.
    if (Settings::meshCrowd() == 0) {
        if (s_encPeerOn) return &s_encPeer;
        if (s_encGobOn && goblinHere(now)) return &s_goblinPeer;
        return nullptr;
    }
    if (const SquachMesh::Peer* p = Mesh::peer()) { s_realSeenAt = now; return p; }
    // A random encounter's visitor: after a real one, who always wins.
    if (s_encPeerOn) return &s_encPeer;
    // A goblin only when no Squachy is here: the visit is one guest at a time
    // and a Squachy is the one this screen was built for.
    if (goblinHere(now)) return &s_goblinPeer;
#if SQUACH_MESH_DEMO
    // The synthetic visitor is a FALLBACK, not the feature: it fills an empty
    // room in a lab build and stands aside for anybody real. The cooldown is
    // what stops it treading on a real guest's exit.
    if (s_demoUp && (s_realSeenAt == 0 || now - s_realSeenAt > DEMO_COOLDOWN_MS))
        return &s_demo;
#endif
    return nullptr;
}

// Who that is, as a value the visit machine can compare against last frame.
// A real peer is its MAC folded down; the emulator's and the demo's are
// constants, because there is only ever one of each.
static uint32_t rawGuestId(uint32_t now) {
    s_rawGoblin = nullptr;
    if (MeshTutor::guest()) return 0x7070u;
    if (s_guest) return 0x5111u;
    if (Settings::meshCrowd() == 0) {     // the dice's own still come: see rawGuest()
        if (s_encPeerOn) return 0xE6C00000u | (s_encSeq & 0xFFFFu);
        if (s_encGobOn) if (const Goblin::Visitor* g = goblinHere(now)) {
            s_rawGoblin = g;
            return (g->id ^ 0x60B11000u) | 1u;
        }
        return 0;
    }
    if (Mesh::peer()) {
        const uint8_t* m = Mesh::peerMac();
        uint32_t h = 2166136261u;                  // FNV-1a, plenty for six bytes
        for (int i = 0; i < 6; i++) { h ^= m[i]; h *= 16777619u; }
        return h ? h : 1u;                         // 0 means "nobody"
    }
    if (s_encPeerOn) return 0xE6C00000u | (s_encSeq & 0xFFFFu);
    if (const Goblin::Visitor* g = goblinHere(now)) {
        s_rawGoblin = g;
        return (g->id ^ 0x60B11000u) | 1u;
    }
#if SQUACH_MESH_DEMO
    if (s_demoUp && (s_realSeenAt == 0 || now - s_realSeenAt > DEMO_COOLDOWN_MS))
        return 0xDE1140u;
#endif
    (void)now;
    return 0;
}
#endif
#include "theme.h"
#include "squachy.h"
#include "pet.h"
#include "settings.h"
#include "idle_events.h"
#include <Arduino.h>

// The pets at double size on the large panels. They are drawn, unchanged,
// into a layer half the screen's width and height (Pet::setZoom says which
// world they are in), and every pixel they drew is laid onto the screen as
// a 2x2 block. 38 KB at 480x320, in PSRAM: only boards that have it, so the
// 3.5" CYDs without PSRAM keep their pets at the old size.
static TFT_eSprite* s_petLayer = nullptr;
// The see-through colour: a dark pure green that no pet, balloon or chain is
// drawn in (every colour in their code checked, 2026-10-09). It was 0x2125
// first -- which is C1iPPY's outline, so he came out with none.
static const uint16_t PET_KEY16 = 0x06C0;

static bool petLayerFor(TFT_eSPI& t, int w, int h) {
    if (!Theme::large()) return false;
#if defined(ESP_PLATFORM)
    if (!psramFound()) return false;
#endif
    const int lw = w / 2, lh = h / 2;
    if (s_petLayer && (s_petLayer->width() != lw || s_petLayer->height() != lh)) {
        s_petLayer->deleteSprite();
        delete s_petLayer;
        s_petLayer = nullptr;
    }
    if (!s_petLayer) {
        s_petLayer = new TFT_eSprite(&t);
        s_petLayer->setColorDepth(8);
#if defined(ESP_PLATFORM)
        s_petLayer->setAttribute(PSRAM_ENABLE, true);
#endif
        if (!s_petLayer->createSprite(lw, lh)) {
            delete s_petLayer;
            s_petLayer = nullptr;
            return false;
        }
    }
    return true;
}

static void drawPets(TFT_eSPI& t, uint32_t now, int w, int h, int bandTop, int petFloor) {
#if SQUACH_MESH
    Pet::setCrowd(s_crowdDrawn);
#endif
    if (!Pet::anyOut(now) || !petLayerFor(t, w, h)) {
        Pet::setZoom(1);
        Pet::tick(t, now, w, bandTop, petFloor);
        return;
    }
    Pet::setZoom(2);
    TFT_eSprite& L = *s_petLayer;
    L.fillRect(0, 0, L.width(), L.height(), PET_KEY16);   // fillSprite, which the emulator lacks
#if defined(ESP_PLATFORM)
    // The key as the layer stores it, whatever 8 bits the 16 came down to.
    const uint8_t PET_KEY8 = L.getPointer() ? ((const uint8_t*)L.getPointer())[0] : 0;
#endif
    // ...and as the layer reads it back, which is not always the 16 bits
    // that went in: the sprite stores fewer, and gives back what it kept.
    const uint16_t petKey = L.readPixel(0, 0);
    Pet::tick(L, now, w / 2, bandTop / 2, petFloor / 2);
    const int lw = L.width(), lh = L.height();
#if defined(ESP_PLATFORM)
    // The layer's own bytes on a board: 38,400 of them, a run at a time.
    const uint8_t* buf = (const uint8_t*)L.getPointer();
    if (buf && L.getColorDepth() == 8) {
        for (int y = 0; y < lh; y++) {
            const uint8_t* row = buf + (size_t)y * lw;
            int x = 0;
            while (x < lw) {
                const uint8_t c = row[x];
                int e = x + 1;
                while (e < lw && row[e] == c) e++;
                if (c != PET_KEY8) t.fillRect(x * 2, y * 2, (e - x) * 2, 2, L.color8to16(c));
                x = e;
            }
        }
        Pet::drawHeldBalloons(t);
        return;
    }
#endif
    // The emulator's sprites keep 16 bits a pixel: read them back instead.
    for (int y = 0; y < lh; y++) {
        int x = 0;
        while (x < lw) {
            const uint16_t c = L.readPixel(x, y);
            int e = x + 1;
            while (e < lw && L.readPixel(e, y) == c) e++;
            if (c != petKey) t.fillRect(x * 2, y * 2, (e - x) * 2, 2, c);
            x = e;
        }
    }
    Pet::drawHeldBalloons(t);
}

void uiClearInit(TFT_eSPI& t) {
    // fillScreen() relies on TFT_eSPI's base-class width/height, which
    // TFT_eSprite::createSprite() never updates — it leaves stale
    // remnants of whatever screen was drawn before when t is a sprite.
    t.fillRect(0, 0, t.width(), t.height(), Theme::BG);
}

static const char* counterLabel(DetectionType t) {
    switch (t) {
        case DetectionType::FLOCK:       return "FLOCK";
        case DetectionType::AXON:        return "AXON";
        // Not just Meta any more -- Snap Spectacles and Luxottica land here too.
        case DetectionType::META:        return "GLASS";
        case DetectionType::SKIMMER:     return "SKIM";
        // AIRTAG doubles as the combined "TRACKER" bucket here -- see
        // counterCount() below. GOOGLE_TAG/TILE/SAMSUNG_TAG keep their
        // own labels everywhere else (LOG screen, colors, vendor
        // names); this is just the compact main-screen row folding all
        // four BLE-tracker types into one column to save space. RING
        // folds into CAM the same way.
        case DetectionType::AIRTAG:      return "TRACKER";
        case DetectionType::DRONE:       return "DRONE";
        case DetectionType::RAVEN:       return "RAV";
        case DetectionType::ALPR:        return "ALPR";
        case DetectionType::CAMERA:      return "CAM";
        case DetectionType::SAMSUNG_TAG: return "STAG";
        case DetectionType::GOOGLE_TAG:  return "GTAG";
        case DetectionType::TILE:        return "TILE";
        case DetectionType::RING:        return "RING";
        // Four letters, like HACK: this row is packed six-across.
        case DetectionType::IBEACON:     return "BCON";
        case DetectionType::DEAUTH:      return "DEAUTH";
        case DetectionType::EVILTWIN:    return "EVIL";
        // Four letters, not "HACKER": this row is packed six-across in
        // landscape and four-across on a 240px portrait panel. EVILTWIN
        // keeps its own label everywhere else -- it just does not get its
        // own column here, because its count is inside this one.
        case DetectionType::HACKER:      return "HACK";
        case DetectionType::LORAWAN:     return "LWAN";
        case DetectionType::MINE:        return "MINE";
        default:                         return "?";
    }
}

// GOOGLE_TAG, TILE and SAMSUNG_TAG's counts fold into AIRTAG's here
// (see counterLabel's "TRACKER" case above) -- they're still tracked
// and displayed as their own distinct types everywhere else (LOG
// screen, colors, vendor labels), just combined into one number/column
// on this compact row to free up space, especially under the 4-per-row
// portrait cap.
//
// SAMSUNG_TAG joined the fold when EVILTWIN was added: they're all
// "something is quietly tracking you" and read fine as one number,
// whereas a rogue AP is a different kind of problem and had nowhere to
// go. It has somewhere to go now -- HACKER, below. Row stays at 12
// columns either way, so the layout has never moved.
// MY ALERTS takes the twelfth column while its list has anything in it, and
// beacons (which otherwise have that column) fold into TRACKER for as long as
// it does -- they are filed with the trackers everywhere else already. Twelve
// columns either way, so the row never grows into Squachy's band.
static bool mineColumn()   { return MyAlerts::count() > 0; }
static bool beaconFolded() { return mineColumn() && Settings::typeEnabled(DetectionType::IBEACON); }

static uint16_t counterCount(const DetectionEngine& eng, DetectionType t) {
    uint16_t n = eng.countByType(t);
    if (t == DetectionType::AIRTAG && beaconFolded()) n += eng.countByType(DetectionType::IBEACON);
    if (t == DetectionType::AIRTAG) {
        n += eng.countByType(DetectionType::GOOGLE_TAG)
           + eng.countByType(DetectionType::TILE)
           + eng.countByType(DetectionType::SAMSUNG_TAG);
    }
    // A Ring doorbell is a camera, and on a row this tight that is the
    // level the column needs to work at. It keeps its own type, colour,
    // icon and name everywhere else -- the LOG still says RING -- and the
    // column it gives up is what makes room for beacons below.
    if (t == DetectionType::CAMERA) {
        n += eng.countByType(DetectionType::RING);
        // A smart speaker is a microphone in the room, which is the question
        // the CAM column answers in a hotel: what in here is recording?
        n += eng.countByType(DetectionType::LISTENER);
    }
    // EVILTWIN folds into HACKER the same way, and for a better reason than
    // saving a column: a rogue AP is not a category of hardware, it is a
    // thing pentest hardware DOES. A Pineapple running PineAP karma is an
    // evil twin -- the same box, seen by its behaviour instead of by its
    // signature. Counting them apart would split one device across two
    // columns and read as two problems.
    if (t == DetectionType::HACKER) {
        n += eng.countByType(DetectionType::EVILTWIN);
    }
    return n;
}

// A counter tile drawn as a Windows XP taskbar button: a rounded box with a
// glossy blue gradient, a small square "tray icon" in the device's own
// colour, and white bold text. state 0 lit, 1 flashing for attention (XP's
// orange), 2 greyed out.
static const int XP_ICON_W = 9;     // the icon and the gap after it
static const int XP_ADV    = 7;     // per letter: 5 wide, 1 bolder, 1 gap
static void drawXpTile(TFT_eSPI& t, int x, int y, int w, int h, const char* txt,
                       uint16_t iconCol, uint8_t state, int pad, int k = 1) {
    // Top and bottom of the gradient, then the edge, per state.
    // The blue is the channel's #0099FF at the bottom, lighter up top.
    static const uint16_t TOP[3]  = { 0x661F /* #66C2FF */, 0xFD8A /* #FFB050 */, 0x8C77 /* #8C8EB8 */ };
    static const uint16_t BOT[3]  = { 0x04DF /* #0099FF */, 0xDB02 /* #DC6010 */, 0x4A90 /* #4A5282 */ };
    static const uint16_t EDGE[3] = { 0x0AF5 /* #0A5CA8 */, 0x8A00 /* #884000 */, 0x2929 /* #282448 */ };
    // Black text (asked for 2026-10-05: white on the blue was hard to read).
    // No drop shadow: at this size it only muddied the letters.
    static const uint16_t INK[3]  = { 0x0000, 0x0000, 0x2104 };
    if (state > 2) state = 2;
    for (int r = 1; r < h - 1; r++) {
        // A bright gloss line under the top edge, then the gradient down.
        uint16_t c = Theme::blend(TOP[state], BOT[state], (uint16_t)(r * 256 / (h - 1)));
        if (r == 1) c = Theme::blend(c, 0xFFFF, 110);
        const int inset = (r == 1 || r == h - 2) ? 1 : 0;
        t.drawFastHLine(x + 1 + inset, y + r, w - 2 - 2 * inset, c);
    }
    t.drawRoundRect(x, y, w, h, 3 * k, EDGE[state]);
    // The tray icon: the device's colour in a little bevelled square.
    const int is = 6 * k;
    const int ix = x + pad, iy = y + (h - is) / 2;
    t.fillRect(ix, iy, is, is, state == 2 ? Theme::blend(iconCol, 0x8410, 160) : iconCol);
    t.drawFastHLine(ix, iy, is, 0xFFFF);
    t.drawFastVLine(ix, iy, is, 0xFFFF);
    t.drawFastHLine(ix, iy + is - 1, is, EDGE[state]);
    t.drawFastVLine(ix + is - 1, iy, is, EDGE[state]);
    // Tahoma Bold, near enough: each letter twice, a pixel apart. One
    // letter at a time on a 7-pixel advance: the font's own 6 leaves no gap
    // once the letters are a pixel wider, and they ran together (2026-10-05).
    // k: 2 on the large panels, the whole tile drawn at double size.
    const int ty = y + (h - 8 * k) / 2;
    int tx = ix + XP_ICON_W * k;
    t.setTextColor(INK[state]);
    t.setTextSize(k);
    for (const char* c = txt; *c; c++, tx += XP_ADV * k) {
        t.setCursor(tx, ty);         t.write(*c);
        t.setCursor(tx + 1, ty);     t.write(*c);
    }
    t.setTextSize(1);
}

// The FONTS page's preview: one tile, lit, at the scale the main screen
// draws them. Returns its width.
int uiClearSampleTile(TFT_eSPI& t, int x, int y, const char* txt, uint16_t iconCol) {
    const int k = Theme::xpTileScale();
    const int w = ((int)strlen(txt) * XP_ADV + 10 + XP_ICON_W) * k;
    drawXpTile(t, x, y, w, 12 * k, txt, iconCol, 0, 5 * k, k);
    return w;
}

// CLASSIC's counters: one plain line of LABEL:COUNT pairs.
static void drawCounterLine(TFT_eSPI& t, int w, int y, const DetectionEngine& eng,
                            const DetectionType* types, uint8_t n) {
    // 80, not 56: worst case is 7 entries x up to "XXXXX:999  " (11
    // chars) = 77 -- the old 56-byte buffer was already marginal for
    // 6 entries at high counts and would silently truncate (snprintf
    // is bounds-safe, just visually cuts off) once TILE/RING pushed a
    // line to 7.
    char buf[80] = "";
    int off = 0;
    for (uint8_t i = 0; i < n; i++) {
        off += snprintf(buf + off, sizeof(buf) - off, "%s:%u  ",
                        counterLabel(types[i]), counterCount(eng, types[i]));
    }
    // The trailing gap is spacing between entries, not part of the last one.
    while (off > 0 && buf[off - 1] == ' ') buf[--off] = '\0';
    int tw = t.textWidth(buf);
    const int x = (w - tw) / 2;
    // A dark plate a few pixels past the text, not just the character cells:
    // tight to the glyphs, the numbers read as cut out of whatever the
    // background is doing behind them.
    const int PAD_X = 4, PAD_Y = 2;
    t.fillRect(x - PAD_X, y - PAD_Y, tw + 2 * PAD_X, t.fontHeight() + 2 * PAD_Y, Theme::BG);
    t.setCursor(x, y);
    t.print(buf);
}

// The same folds, asked the other way round: which column a device lands in.
bool uiClearColumnHolds(DetectionType column, DetectionType d) {
    if (d == column) return true;
    switch (column) {
        case DetectionType::AIRTAG:
            return d == DetectionType::GOOGLE_TAG || d == DetectionType::TILE || d == DetectionType::SAMSUNG_TAG ||
                   (d == DetectionType::IBEACON && beaconFolded());
        case DetectionType::CAMERA: return d == DetectionType::RING || d == DetectionType::LISTENER;
        case DetectionType::HACKER: return d == DetectionType::EVILTWIN;
        default:                    return false;
    }
}

// All types in one place, chunked into rows of at most MAX_PER_ROW at
// draw time (see uiClearTick()) instead of two hand-split arrays --
// the old 7-and-6 split ran wide enough on a narrow 240px portrait
// screen that FLOCK (first on the line) got clipped off the left edge
// entirely. A hard per-row cap fixes that on both boards, not just
// AWOK's narrower panel.
//
// THE RULE THIS ROW KEEPS: every type that can be detected is counted in
// one of these columns, whether or not it has one of its own. Three folds
// do that (see counterLabel/counterCount above) -- GOOGLE_TAG, TILE and
// SAMSUNG_TAG into TRACKER, EVILTWIN into HACK, RING into CAM -- which is
// what keeps the row at eleven fixed columns on a screen that has room for
// twelve.
static const DetectionType FIXED_COUNTER_TYPES[] = {
    DetectionType::FLOCK,   DetectionType::AXON,     DetectionType::META,   DetectionType::SKIMMER,
    DetectionType::RAVEN,   DetectionType::AIRTAG,   DetectionType::DRONE,  DetectionType::ALPR,
    DetectionType::CAMERA,  DetectionType::HACKER,   DetectionType::DEAUTH,
};
static const uint8_t FIXED_COUNTER_TYPES_N =
    sizeof(FIXED_COUNTER_TYPES) / sizeof(FIXED_COUNTER_TYPES[0]);
// ...and the twelfth column goes to beacons, but only while the type is
// switched on. It ships off (DEFAULT_OFF in settings.cpp) because one shop
// can put dozens of them in range, so most boards never see this column;
// the one that asked for the type gets it, and the rule above stays true
// without anybody having to remember it. Twelve is what this row has
// always drawn, so the layout never grows.
//
// A board with a LoRa radio has one more thing it can catch, a LoRaWAN
// operator's network, and the rule above gives it a column: LWAN, after the
// rest. Nothing folds it in -- it is neither a tracker, a camera nor an
// attack. A column with nothing in it is not drawn, so the row only grows
// while an operator is in earshot.
#if SQUACH_LORA
static const uint8_t LORA_COUNTER_TYPES = 1;
#else
static const uint8_t LORA_COUNTER_TYPES = 0;
#endif
static const uint8_t MAX_COUNTER_TYPES = FIXED_COUNTER_TYPES_N + 1 + LORA_COUNTER_TYPES;
static uint8_t activeCounterTypes(DetectionType* out) {
    uint8_t n = 0;
    for (; n < FIXED_COUNTER_TYPES_N; n++) out[n] = FIXED_COUNTER_TYPES[n];
    if (mineColumn())                                       out[n++] = DetectionType::MINE;
    else if (Settings::typeEnabled(DetectionType::IBEACON)) out[n++] = DetectionType::IBEACON;
#if SQUACH_LORA
    out[n++] = DetectionType::LORAWAN;
#endif
    return n;
}
// Portrait (narrow) caps at 4 per row -- see the comment above. Landscape
// has plenty of width for the original 7-and-6 two-row split (that's
// exactly what this produces: 13 types / 2 rows), so row count is
// picked dynamically off the live orientation in uiClearTick() rather
// than fixed at compile time -- it changes every time the screen
// rotates, not just once per board.
static const uint8_t MAX_PER_ROW_PORTRAIT   = 4;
static const uint8_t COUNTER_ROWS_LANDSCAPE = 2;
// Three rows in portrait at eleven columns and at twelve alike, so turning
// beacons on never moves the counters up into Squachy's band.
// The LoRa column is left out of this sum on purpose: counting it would
// reserve a fourth row on every portrait LoRa board for the one day all
// thirteen are live at once, and Squachy would pay for it every other day.
static const uint8_t COUNTER_ROWS_PORTRAIT  =
    (FIXED_COUNTER_TYPES_N + 1 + MAX_PER_ROW_PORTRAIT - 1) / MAX_PER_ROW_PORTRAIT;  // ceil

#if SQUACH_MESH
// The ordinary visit: ours on the left at SMALL, the guest walking in on the
// right, the high five, the conversation and the set pieces. Drawn into the
// band from `titleBottom` to `squachyBottom`, which is the main screen's
// Squachy band there and the room under the clock on the desk.
// ---- the 3.5" boards ------------------------------------------------------------
// Standing up, the 3.5" has a tall band and he filled little of it. These are
// the sizes picked for it on two slider pages (2026-10-10), as multiples of
// what he was. Portrait: SMALL 1.5, MEDIUM 1.4, LARGE 1.3 on his own, as big
// as fits with a visitor (below), a crowd of three 1.1, of four or five 1.2.
// Landscape: alone as he was, 1.2 with a visitor and in a crowd of up to
// five. Every 480x320 board; every smaller panel is as it was.
static bool tallBig(TFT_eSPI& t) { return Theme::large() && t.height() > t.width(); }
static bool wideBig(TFT_eSPI& t) { return Theme::large() && t.width() > t.height(); }
static float soloBoost(TFT_eSPI& t, uint8_t sizePct) {
    if (!tallBig(t)) return 1.0f;
    return sizePct <= 70 ? 1.5f : sizePct <= 85 ? 1.4f : 1.3f;
}
// With a visitor: as big as fits once they stand closer -- each may take 60%
// of the width, so arms overlap in the middle -- which is past the 1.5 the
// slider stopped at with half each (asked for bigger still, 2026-10-10).
// The 320x240 boards -- the 2.4", 2.8" and 3.2" CYDs and the 2.8" Freenove,
// one picture for all of them -- picked on a third page: with a visitor 1.2
// in landscape (portrait already overlaps them), crowds of three 1.1, four or
// five 1.2, six to eight 1.2 in landscape and 1.1 in portrait. Exactly those
// two shapes: the watch and the StickS3 panels get pages of their own.
static bool qvga(TFT_eSPI& t) {
    return (t.width() == 320 && t.height() == 240) || (t.width() == 240 && t.height() == 320);
}
static float visitBoost(TFT_eSPI& t) {
    if (tallBig(t)) return 2.0f;
    if (wideBig(t)) return 1.2f;
    if (qvga(t) && t.width() > t.height()) return 1.2f;
    return 1.0f;
}
static const float VISIT_SHARE = 0.6f;
static float crowdGrow(TFT_eSPI& t, uint8_t bodies) {
    if (tallBig(t)) return bodies <= 3 ? 1.1f : bodies <= 5 ? 1.2f : 1.0f;
    if (wideBig(t)) return bodies <= 5 ? 1.2f : 1.0f;
    if (qvga(t)) {
        if (bodies <= 3) return 1.1f;
        if (bodies <= 5) return 1.2f;
        return t.width() > t.height() ? 1.2f : 1.1f;
    }
    // The T-Watch's 240x240, picked on its own page: 1.2 up to five, 1.1 past.
    if (t.width() == 240 && t.height() == 240) return bodies <= 5 ? 1.2f : 1.1f;
    return 1.0f;
}

// A goblin, our own drawing (Scout drew its own Bigfoot for its scene, so we
// draw our own goblin for ours). Flat shapes, the way the costumes are: his
// skin is his hue, a pointed cap when he wears a hat, a sack of loot beside
// him that grows with his hoard. About three quarters of a Squachy tall.
static void gobLimb(TFT_eSPI& t, int x0, int y0, int x1, int y1, int r, uint16_t c) {
    const int dx = x1 - x0, dy = y1 - y0;
    const int n = (abs(dx) > abs(dy) ? abs(dx) : abs(dy)) / (r > 1 ? r : 1) + 1;
    for (int i = 0; i <= n; i++) t.fillCircle(x0 + dx * i / n, y0 + dy * i / n, r, c);
}
static void drawGoblin(TFT_eSPI& t, int cx, int bottom, float u, uint32_t now,
                       const Goblin::Visitor& g, bool walking, bool armUp, int handX, int handY,
                       bool waving, bool laughing, const char* line) {
    auto U = [u](float v) { const int r = (int)(v * u + 0.5f); return r < 1 ? 1 : r; };
    const uint16_t skin  = Goblin::skin565(g.hue, 205);
    const uint16_t dark  = Goblin::skin565(g.hue, 120);
    const uint16_t tunic = 0x6A44, tunicLo = 0x4182, ink = 0x18C3, gold = 0xFEA0;
    // He is a head shorter than the host, so the high five is a jump: up and
    // in toward the hand instead of an arm as long as he is.
    if (armUp) { bottom -= U(13); cx -= U(9); }
    const int b = bottom - (laughing ? (int)((now / 90) % 2) * U(1.5f) : 0);
    const int step = walking ? (((now / 140) % 2) ? U(2) : -U(2)) : 0;

    // The sack first, so he stands in front of it.
    if (g.hoard) {
        const int r = U(3.0f + 0.8f * g.hoard), sx = cx + U(13) + r / 2, sy = b - r;
        t.fillCircle(sx, sy, r, 0xA4E8);
        t.fillTriangle(sx - U(2), sy - r - U(3), sx + U(2), sy - r - U(3), sx, sy - r + U(1), 0xA4E8);
        t.fillRect(sx - U(2), sy - r - U(1), U(4), U(1), tunicLo);
        for (int i = 0; i < g.hoard && i < 5; i++)
            t.fillCircle(sx - r + U(1) + i * U(2.4f), b - U(1), U(1.3f), gold);
    }
    // Feet and legs.
    t.fillRect(cx - U(5) + step, b - U(9), U(2.5f), U(8), dark);
    t.fillRect(cx + U(3) - step, b - U(9), U(2.5f), U(8), dark);
    t.fillEllipse(cx - U(5) + step, b - U(1), U(3), U(1.5f), dark);
    t.fillEllipse(cx + U(5) - step, b - U(1), U(3), U(1.5f), dark);
    // The far arm, hanging.
    gobLimb(t, cx + U(7), b - U(21), cx + U(11), b - U(12), U(1.4f), skin);
    t.fillCircle(cx + U(11), b - U(12), U(2), skin);
    // A ragged tunic with a belt.
    t.fillRoundRect(cx - U(8), b - U(24), U(16), U(15), U(4), tunic);
    for (int i = 0; i < 4; i++)
        t.fillTriangle(cx - U(8) + i * U(4), b - U(10), cx - U(4) + i * U(4), b - U(10),
                       cx - U(6) + i * U(4), b - U(7), tunic);
    t.fillRect(cx - U(8), b - U(15), U(16), U(1.5f), tunicLo);
    t.fillRect(cx - U(1), b - U(15) - 1, U(2), U(1.5f) + 2, gold);
    // The near arm: up for the high five, waving hello, or hanging.
    int hx = cx - U(11), hy = b - U(12);
    if (armUp)       { hx = handX; hy = handY; }
    else if (waving) { hx = cx - U(13) + (((now / 220) % 2) ? U(2) : -U(1)); hy = b - U(36); }
    gobLimb(t, cx - U(7), b - U(21), hx, hy, armUp ? U(1.8f) : U(1.4f), skin);
    t.fillCircle(hx, hy, U(2), skin);
    // Head and the ears that do the listening.
    const int hc = b - U(32);
    t.fillTriangle(cx - U(7), hc - U(4), cx - U(7), hc + U(3), cx - U(20), hc - U(7), skin);
    t.fillTriangle(cx + U(7), hc - U(4), cx + U(7), hc + U(3), cx + U(20), hc - U(7), skin);
    t.fillTriangle(cx - U(8), hc - U(2), cx - U(8), hc + U(1), cx - U(15), hc - U(4), dark);
    t.fillTriangle(cx + U(8), hc - U(2), cx + U(8), hc + U(1), cx + U(15), hc - U(4), dark);
    t.fillCircle(cx, hc, U(9), skin);
    // Eyes, looking at the host on his left.
    t.fillCircle(cx - U(3.5f), hc - U(2), U(2.6f), Theme::WHITE);
    t.fillCircle(cx + U(3.5f), hc - U(2), U(2.6f), Theme::WHITE);
    t.fillCircle(cx - U(4.3f), hc - U(2), U(1.3f), ink);
    t.fillCircle(cx + U(2.7f), hc - U(2), U(1.3f), ink);
    // A long nose, and a grin with two fangs.
    t.fillTriangle(cx, hc - U(1), cx, hc + U(2), cx - U(4), hc + U(2), dark);
    t.fillRect(cx - U(4), hc + U(4), U(8), laughing ? U(3) : U(1.6f), ink);
    t.fillRect(cx - U(3), hc + U(4), U(1), U(1.2f), Theme::WHITE);
    t.fillRect(cx + U(2), hc + U(4), U(1), U(1.2f), Theme::WHITE);
    // His hat, if he wears one. Scout has its own hats; this is a cap in a
    // colour picked from which one, so two goblins in different hats differ.
    int top = hc - U(9);
    if (g.hat) {
        const uint16_t hcol = Goblin::skin565((uint8_t)(g.hat * 53), 230);
        t.fillTriangle(cx - U(9), hc - U(6), cx + U(9), hc - U(6), cx + U(4), hc - U(22), hcol);
        t.fillRect(cx - U(10), hc - U(7), U(20), U(2), Goblin::skin565((uint8_t)(g.hat * 53), 130));
        t.fillCircle(cx + U(4), hc - U(22), U(2), Theme::WHITE);
        top = hc - U(24);
    }
    // His name and level over his head, in his own colour -- once he is on
    // the screen. Pinned to the edge, it walked in ahead of him.
    if (cx < 0 || cx > t.width()) return;
    if (armUp) return;   // mid-jump it would sit under the counter tiles
    char plate[24];
    if (g.name[0]) snprintf(plate, sizeof plate, "%s LV%u", g.name, (unsigned)g.level);
    else           snprintf(plate, sizeof plate, "GOBLIN LV%u", (unsigned)g.level);
    t.setTextFont(1);
    t.setTextSize(1);
    const int pw = t.textWidth(plate);
    int px = cx - pw / 2;
    if (px + pw > t.width() - 2) px = t.width() - 2 - pw;
    if (px < 2) px = 2;
    const int py = top - 11;
    t.setTextColor(skin, Theme::BG);
    t.setCursor(px, py);
    t.print(plate);
    if (line) Squachy::drawGuestBubble(t, cx, py - 2, line, now, cx);
}

static void drawVisit(TFT_eSPI& t, uint32_t now, const SquachMesh::Peer* guest,
                      int titleBottom, int squachyBottom, bool advance, bool msgFresh) {
    const int w = t.width();
    const int SMALL_PCT = 70;
    const float share = tallBig(t) ? VISIT_SHARE : 0.5f;
    // On the 3.5" in portrait the two of them stand in the middle of the
    // band rather than on its floor: at the size they reach there -- their
    // share of the width each, 74 rows of body per unit of scale -- the floor left
    // most of the screen above them empty. The whole band moves up rather
    // than shrinking, so they keep the size it gives them; everything below
    // works from squachyBottom, so the pair, the slap and the bubbles follow.
    int lift = 0;
    if (tallBig(t)) {
        const int pair = (int)(74.0f * ((float)w * share - 8.0f) / 56.0f);
        const int band = squachyBottom - titleBottom;
        if (band > pair) lift = (band - pair) / 2;
    }
    const int bandH = squachyBottom - titleBottom;
    titleBottom   -= lift;
    squachyBottom -= lift;
    // Closer together there, so the bigger pair still stands on the screen.
    const int gap  = tallBig(t) ? (int)((float)w * 0.21f) : w / 4;
    const int homeX = w / 2 + gap;     // where the guest stands
    const int offX  = w + 40;          // off the right edge

    Squachy::setCompany(true);
    Squachy::setCompanyShare(share);
    Squachy::setSizeBoost(visitBoost(t));
    Squachy::tick(t, w / 2 - gap + hostLeanPx(), titleBottom,
                  bandH,
                  now, advance, 1.0f, false, 0, SMALL_PCT);
    Squachy::setSizeBoost(1.0f);
    Squachy::setCompanyShare(0.5f);
    Squachy::setCompany(false);

    // The visitor is drawn, not ticked: tick() owns mood, quip timers
    // and the walk state, all of which are file-static singletons
    // describing OUR Squachy. Calling it twice would have the guest
    // driving the host's animation. drawWaving is the same body with
    // none of that -- it is what the alert screen's cameo already uses.
    //
    // Both previews are the existing overrides the unlock popup uses
    // to show a costume nobody owns yet. Set them, draw, clear them:
    // left set they would silently redress our own Squachy everywhere.
    if (!s_hostingGoblin) {
        Squachy::setOutfitPreview((int8_t)guest->outfit);
        Squachy::setShadesPreview((int8_t)guest->shade);
        Squachy::setAuraPreview(guest->aura ? 1 : 0);
    }
    // The scale tick() actually used, not SMALL_PCT again: tick
    // derives its scale from the height it was given, so the two
    // numbers are different units and passing 0.7 here drew a
    // visitor less than half the host's size.
    const float gs = Squachy::lastScale();
    // The lean rides on top of the walk rather than replacing it:
    // visitGuestX returns homeX once he has arrived, and it is only
    // then that the lean is non-zero.
    // Where he stands for the high five: close enough that the two
    // reaching hands meet. The host is not leaning yet -- that only
    // starts once the talking does -- so his centre is where it sits.
    const int   meetX = (w / 2 - gap) + (int)(REACH_K * gs);
    const int   gx = visitGuestX(now, homeX, offX, meetX, (int)(8.0f * gs), gs) + guestLeanPx();
    // One of the dice's: the hole he comes and goes by, behind him, and how
    // far up it he is. Its top half sits under the counters, drawn after this,
    // so he comes out from under them.
    int encLift = 0, encHoleY = 0;
    float encOpen = 0.0f;
    bool saved = false;
    if (encGuestHere()) {
        const int u = t.height() >= 400 ? 2 : 1;
        const int holeY = titleBottom + 9 * u;
        encOpen = encHoleOpen(now);
        encHoleY = holeY;
        drawEncHole(t, gx, holeY, u, encOpen, now);
        encLift = encGuestLift(now, squachyBottom - holeY + 14 * u);
        // Where he is, for a shiny one's sparkles (encounterPlay).
        s_encGX = gx; s_encGY = squachyBottom - encLift - (int)(30.0f * gs); s_encGR = (int)(32.0f * gs);
        // Part-way through it: keep what is above the hole, to put back
        // over the part of him that is not here yet (or has already gone).
        if (encLift > 0) saved = stripSave(t, gx - (int)(38.0f * gs), 0, 2 * (int)(38.0f * gs), holeY);
        else if (s_vp != VisitPhase::ARRIVING && s_vp != VisitPhase::LEAVING) stripFree();
    }
    // wanderRangePx is what animates his legs. Walking in with it at 0
    // slid him across the floor like furniture; a couple of pixels of
    // wander is enough to put a walk cycle under the movement without
    // reading as a stagger.
    // Waves while walking in and through the hellos, then settles.
    // A guest who never stops waving reads as a stuck frame rather
    // than as a greeting once he has been there half a minute.
    const bool stillGreeting = guestGreeting();
    // His name, on a sticker on his chest. It used to take turns with
    // his bubble in the row above his head; on the chest the two never
    // meet, so he is named for the whole visit, talking or not.
    if (s_hostingGoblin) {
        // The slap point, as the spark below works it out.
        const uint8_t lvl = 0;
        const int hx = meetX - (int)(REACH_K * 0.5f * gs);
        const int hy = squachyBottom - (int)(58.0f * gs) + (int)(REACH_Y[lvl] * gs);
        drawGoblin(t, gx, squachyBottom - encLift, gs, now, s_goblinHost,
                   visitWalking() || fiveWalking(now), s_vp == VisitPhase::HIGH_FIVE, hx, hy,
                   stillGreeting && s_vp != VisitPhase::HIGH_FIVE, now < s_guestLaughUntil,
                   msgFresh ? nullptr : s_visitGuestLine);
        s_msgGbOn = false;
    } else {
        Squachy::setNameTag((guest->custom && guest->name[0]) ? guest->name
                                                              : Squachy::nicknameAt(guest->nick));
        Squachy::setHeadset(guest->phone);   // a phone wears the headset, as in the crowd
        Squachy::drawWaving(t, gx, squachyBottom - encLift - scriptGuestLift(now, gs,
                                squachyBottom - (int)(58.0f * gs) - 20), now, gs,
                            msgFresh ? nullptr : s_visitGuestLine,
                            // Mouthing it while the red bubble is up.
                            msgFresh || s_visitGuestLine != nullptr,
                            (visitWalking() || fiveWalking(now) || scriptWalking(now)) ? 2 : 0,
                            stillGreeting,
                            // Just above his own head, not the boot
                            // splash's 34 -- that lands in the host's
                            // bubble row and the two paint over each
                            // other. Higher by a hat, when he has one. On
                            // the 3.5" in portrait, clear of both heads: its
                            // two size-2 lines would otherwise cover the
                            // host's face beside him.
                            (tallBig(t) ? 78 : 20) + scriptHatPx(gs),
                            // Cracking up at whatever the host just said.
                            // The host gets the same thing through his
                            // mood machine; this cameo has none.
                            now < s_guestLaughUntil,
                            // Nodding along while the host has the floor.
                            !s_guestTurn && (s_vp == VisitPhase::MEETING ||
                                             s_vp == VisitPhase::HANGING),
                            // And his bubble says so.
                            true,
                            guestPose(now));
        s_msgGbOn = Squachy::cameoBubble(s_msgGbX, s_msgGbY, s_msgGbW, s_msgGbH);
        Squachy::setNameTag(nullptr);
        Squachy::setHeadset(false);
        Squachy::setAuraPreview(-1);
        Squachy::setShadesPreview(-1);
        Squachy::setOutfitPreview(-1);
    }
    // The dice's visitor, part-way through his hole: the sky back over what
    // is above it, then the hole's far rim over the sky.
    if (saved) {
        stripRestore(t);
        drawEncHole(t, gx, encHoleY, t.height() >= 400 ? 2 : 1, encOpen, now, true);
    }

    // The slap. Where the two hands meet -- S(30) in from each of them
    // -- at the height the HIGHFIVE arm ends, for a flash either side
    // of the moment they touch.
    if (s_vp == VisitPhase::HIGH_FIVE) {
        const uint32_t e = now - s_vpAt, st = e / s_fiveStepMs, se = e % s_fiveStepMs;
        if (st < s_fiveSteps && se >= s_fiveHitMs && se < s_fiveHitMs + 320) {
            const float   k   = (float)(se - s_fiveHitMs) / 320.0f;
            const uint8_t lvl = (s_fiveSteps < 3 || st == 0) ? 0 : (st == 1 ? 1 : 2);
            const int     sx  = meetX - (int)(REACH_K * 0.5f * gs);
            const int     sy  = squachyBottom - (int)(58.0f * gs) + (int)(REACH_Y[lvl] * gs);
            drawSpark(t, sx, sy, k, gs);
        }
    }
    drawPieceFx(t, now, w / 2 - gap + hostLeanPx(), gx,
                squachyBottom - (int)(58.0f * gs), gs);
    // Our Squachy's words once more, on top: tick() drew them before the
    // visitor existed in this frame, and a visitor walking in, leaning in or
    // standing close on a big panel stood right over them.
    Squachy::redrawOwnBubble(t, now);

    // Where he is this frame, for the message bubble and its button.
    s_msgGuestOn = true;
    s_msgGx      = gx;
    s_msgHeadTop = squachyBottom - (int)(58.0f * gs);
    s_msgGs      = gs;
}

void uiClearVisitTick(uint32_t now) {
    visitTick(now);
    // Whoever draws this frame says again whether a crowd is up; until then,
    // nobody has.
    s_crowdDrawn = false;
}

bool uiClearDrawVisit(TFT_eSPI& t, uint32_t now, int top, int floorY, bool advance) {
    const SquachMesh::Peer* guest = visitHosting();
    if (!guest) return false;
    s_msgGuestOn = false;
    drawVisit(t, now, guest, top, floorY, advance, false);
    return true;
}
#endif

static uint32_t s_mascotStepMs = 0;   // 0: not yet read from settings
void     uiMascotStepSet(uint32_t ms) { Settings::setMascotPaceMs((uint16_t)ms); s_mascotStepMs = Settings::mascotPaceMs(); }
uint32_t uiMascotStepMs()            { if (!s_mascotStepMs) s_mascotStepMs = Settings::mascotPaceMs(); return s_mascotStepMs; }

bool uiMascotStep(uint32_t now, bool advance) {
    static uint32_t s_lastStep = 0;
    if (!advance) return false;
    if ((uint32_t)(now - s_lastStep) < uiMascotStepMs()) return false;
    s_lastStep = now;
    return true;
}

#include "ui_terminal.inc"

static bool s_fob = false;
void uiClearSetFob(bool on) { s_fob = on; }
// The Cardputer ADV with the EXT screen on: the built-in screen is the main
// screen's buttons, tiles and menu with no scene, and in the middle the
// counts and the hotkeys (Squachy is on the big one).
static bool s_infoOnly = false;
void uiClearInfoOnly(bool on) { s_infoOnly = on; }

#if SQUACH_MESH
// ==== random encounters: the player ================================================
// The dice and the journal are encounters.cpp; this plays whichever one they
// picked. Three come as visitors through the visit machine above (the
// Drifter, the goblin, the evil twin). The rest are drawn: encounterBack()
// after the background and before Squachy (the Elder and the Kaiju stand
// behind him and behind the horizon), encounterFront() where the idle events
// would go (the icons, the Lost Signal, the UFO, the Elder's photo).
//
// Each one is a short script on the clock since it began: what moves where,
// and the lines Squachy says, once each, through his own bubble.
namespace {
using Encounters::Kind;

uint32_t encMs(uint32_t now) { return now - s_encAt; }
// Line i at `atMs`: true the first frame it is due, never again.
bool cue(uint8_t i, uint32_t atMs, uint32_t now) {
    if (encMs(now) < atMs || (s_encSaid & (1u << i))) return false;
    s_encSaid |= 1u << i;
    return true;
}
void say(uint8_t i, uint32_t atMs, uint32_t now, const char* line) {
    if (cue(i, atMs, now)) Squachy::announce(line);
}
void reachUp(uint8_t i, uint32_t atMs, uint32_t now) {
    if (cue(i, atMs, now)) Squachy::visitReach(now, 1400, Squachy::Reach::UP);
}
void laugh(uint8_t i, uint32_t atMs, uint32_t now) {
    if (cue(i, atMs, now)) Squachy::visitLaugh(now);
}

// The goblin names the dice pick from.
const char* const GOBLIN_NAMES[] = { "Grizzlewick", "Snagtooth", "Mold", "Pocketlint", "Bramble", "Nib",
                                     "Skrunk", "Tinbit", "Wort", "Fizzlegob", "Krinkle", "Dross",
                                     "Sprocket", "Mugwump", "Scrap", "Gubbins" };

bool drawn(Kind k) {
    return k == Kind::ICONS || k == Kind::ZOOMIES || k == Kind::KAIJU || k == Kind::LOST ||
           k == Kind::ABDUCT || k == Kind::ELDER || k == Kind::MIMIC;
}

void encStart(Kind k, uint32_t now) {
    s_encK = k; s_encAt = now; s_encSaid = 0; s_encTapped = false; s_encTapAt = 0; s_encSeq++;
    s_encHitR = 0;
    switch (k) {
        case Kind::DRIFTER:
            s_encPeer = SquachMesh::Peer{};
            s_encPeer.nick   = (uint8_t)random(0, SquachMesh::NICK_N);
            s_encPeer.outfit = (uint8_t)random(0, SquachMesh::OUTFIT_N);
            s_encPeer.shade  = (uint8_t)random(0, SquachMesh::SHADE_N);
            s_encPeerOn = true;
            break;
        case Kind::TWIN:
            // Him, in his own outfit -- with the pink shades and the name.
            s_encPeer = SquachMesh::Peer{};
            s_encPeer.nick   = Squachy::nicknameIndex();
            s_encPeer.outfit = Squachy::outfitIndex();
            s_encPeer.shade  = 1;   // PINK, the nearest the shades come to red
            s_encPeer.custom = true;
            snprintf(s_encPeer.name, sizeof s_encPeer.name, "EVIL TWIN");
            s_encPeerOn = true;
            break;
        case Kind::GOBLIN: {
            s_encGob = Goblin::Visitor{};
            s_encGob.id    = 0x60B00000u | (uint32_t)random(1, 0xFFFFF);
            s_encGob.level = (uint16_t)random(2, 61);
            s_encGob.hue   = (uint8_t)random(0, 256);
            s_encGob.hat   = (uint8_t)random(0, 9);
            s_encGob.hoard = (uint8_t)random(0, 8);
            snprintf(s_encGob.name, sizeof s_encGob.name, "%s",
                     GOBLIN_NAMES[random(0, sizeof GOBLIN_NAMES / sizeof GOBLIN_NAMES[0])]);
            s_encGobOn = true;
            break;
        }
        case Kind::ZOOMIES:
            Pet::zoomies(now, 5200);
            break;
        case Kind::MIMIC: {
            // The die that was sitting there, if one was (touched, or called
            // away this very frame); otherwise one rolls in to be it.
            const bool there = s_mimTapped || (s_freeOutAt && now - s_freeOutAt < 60);
            s_mimX    = there && s_freeCx > -500 ? (float)s_freeCx : (float)s_freeX;
            s_mimWake = there ? 0 : 700;
            s_mimTapped = false; s_mimBiteAt = 0; s_mimLast = now; s_mimFace = -1;
            s_freeOutAt = 0; s_freeShown = false;
            break;
        }
        default: break;
    }
    // A shiny one: the Legend's aura on a Squachy, gold skin on a goblin. The
    // rest is sparkles, where each is drawn.
    if (Encounters::activeShiny()) {
        if (k == Kind::DRIFTER || k == Kind::TWIN) s_encPeer.aura = true;
        if (k == Kind::GOBLIN) s_encGob.hue = 30;
    }
    s_encGR = 0;
    for (auto& sp : s_trail) sp.at = 0;
    // The die hushed him while it rolled; a visit is its own kind of quiet.
    Squachy::setHush(drawn(k));
}

// Stops what is playing without telling the dice: for when they have already
// picked the next one.
void encStop() {
    s_encPeerOn = false;
    s_encGobOn  = false;
    s_encHitR   = 0;
    s_encGR     = 0;
    Squachy::setHush(false);
    s_encK = Kind::NONE;
}
void encEnd() {
    encStop();
    Encounters::finish();
}

// ---- the die -------------------------------------------------------------------
const uint32_t DIE_READ_MS = 1500;      // at rest, showing its number, before anything comes of it
int dieRestX(int w) { return w / 2 + w / 20; }     // by his foot, in the gap a visit leaves

// Where it is `ms` after it dropped: a fall from above the screen, three
// bounces, rest. Stepped a thirtieth of a second at a time, as it was drawn
// for the lab, so the bounce is the same on every board. True at rest.
bool dieAt(uint32_t ms, int restX, int floorY, int u, float& x, float& y, float& ang, uint32_t& restMs) {
    const float g = 1.15f * u, floor = (float)(floorY - 15 * u);
    float vy = 0, spin = 17.0f, yy = -20.0f * u, aa = 40.0f;
    int n = 0, landed = -1;
    const int steps = (int)(ms / 33);
    float sy = yy, sa = aa;                      // as of `steps`
    for (; n < 120 && landed < 0; n++) {
        if (n == steps) { sy = yy; sa = aa; }
        vy += g; yy += vy; aa += spin;
        if (yy >= floor) {
            yy = floor; vy = -vy * 0.5f; spin *= 0.6f;
            if (fabsf(vy) < 2.2f * u) landed = n + 1;
        }
    }
    if (landed < 0) landed = n;
    restMs = (uint32_t)landed * 33u;
    if (steps >= landed) { x = (float)restX; y = floor; ang = 0; return true; }
    x = (float)restX + 0.42f * u * (float)(landed - steps); y = sy; ang = sa;
    return false;
}

const uint32_t DIE_READ20_MS = 2700;    // a 20 is looked at for longer
const uint32_t DIE_TOSS_MS   = 600;     // the free roll's die, thrown: up and off the screen
const uint32_t BONK_HIT_MS   = 420;     // a 1: from the floor to the top of his head
uint32_t dieRead() { return s_dieNum == 20 ? DIE_READ20_MS : DIE_READ_MS; }

// A four-pointed twinkle.
void star4(TFT_eSPI& t, int x, int y, int r, uint16_t col) {
    if (r < 1) return;
    const int q = r / 3 > 0 ? r / 3 : 1;
    t.fillTriangle(x, y - r, x - q, y, x + q, y, col);
    t.fillTriangle(x, y + r, x - q, y, x + q, y, col);
    t.fillTriangle(x - r, y, x, y - q, x, y + q, col);
    t.fillTriangle(x + r, y, x, y - q, x, y + q, col);
}

// Something landing, or leaving in a hurry: eight short lines, spreading.
void drawBurst(TFT_eSPI& t, int cx, int cy, uint32_t ms, int u) {
    const int q = (5 + (int)(ms / 11)) * u;
    for (int d = 0; d < 8; d++) {
        const float aa = d * 0.785398f;
        t.drawLine(cx + (int)((q - 4 * u) * cosf(aa)), cy + (int)((q - 4 * u) * sinf(aa)),
                   cx + (int)(q * cosf(aa)), cy + (int)(q * sinf(aa)), Theme::WHITE);
    }
}

// A natural 20 at rest: rays turning behind it, like a sun coming up off the
// floor (nothing is drawn below `floorY`, where the buttons are), and
// twinkles round it. `ms` since it stopped.
void drawGlory(TFT_eSPI& t, int cx, int cy, int r, uint32_t ms, int u, int floorY) {
    const float grow = ms < 220 ? (float)ms / 220.0f : 1.0f;
    const float base = (float)ms / 700.0f;
    const int r0 = r - 2 * u, r1 = r + (int)(24.0f * u * grow);
    for (int k = 0; k < 10; k++) {
        const float a = base + k * 0.628319f, hw = 0.13f;
        const int tipY = cy + (int)(r1 * sinf(a));
        if (tipY > floorY) continue;
        t.fillTriangle(cx + (int)(r0 * cosf(a - hw)), cy + (int)(r0 * sinf(a - hw)),
                       cx + (int)(r0 * cosf(a + hw)), cy + (int)(r0 * sinf(a + hw)),
                       cx + (int)(r1 * cosf(a)), tipY, (k & 1) ? Theme::WHITE : (uint16_t)0xFFE0);
    }
    static const int8_t TW[5][2] = { { -31, -20 }, { 27, -29 }, { 35, -6 }, { -37, -2 }, { 3, -41 } };
    for (int k = 0; k < 5; k++) {
        const float ph = sinf((float)ms / 130.0f + k * 1.9f);
        if (ph > 0) star4(t, cx + TW[k][0] * u, cy + TW[k][1] * u, (int)((2.0f + 4.0f * ph) * u), Theme::WHITE);
    }
}

// A d20, face on: a hexagon, the three faces round the middle one, and the
// number on it when it has stopped. `glow` is the natural 20's gold: 1, or 2
// for the frames it flashes.
void drawD20(TFT_eSPI& t, float cx, float cy, int r, float angDeg, uint8_t num, int u, uint8_t glow = 0) {
    const uint16_t BODY  = glow ? 0xFFE0 : 0xFDA0, SHADE = glow ? 0xFD20 : 0x9240, INK = 0x0000;
    const uint16_t FACE  = glow == 2 ? 0xFFFF : glow ? 0xFFF4 : num ? 0xFF75 : 0xFED5;
    const float f = num ? 0.66f : 0.55f;
    int hx[6], hy[6], tx[3], ty[3];
    for (int k = 0; k < 6; k++) {
        const float a = (angDeg - 90.0f + 60.0f * k) * 0.0174533f;
        hx[k] = (int)lroundf(cx + r * cosf(a)); hy[k] = (int)lroundf(cy + r * sinf(a));
    }
    for (int k = 0; k < 3; k++) {
        const float a = (angDeg - 90.0f + 120.0f * k) * 0.0174533f;
        tx[k] = (int)lroundf(cx + r * f * cosf(a)); ty[k] = (int)lroundf(cy + r * f * sinf(a));
    }
    for (int k = 1; k < 5; k++) t.fillTriangle(hx[0], hy[0], hx[k], hy[k], hx[k + 1], hy[k + 1], BODY);
    t.fillTriangle(hx[2], hy[2], hx[3], hy[3], hx[4], hy[4], SHADE);      // the lower faces, a shade down
    t.fillTriangle(hx[2], hy[2], hx[4], hy[4], tx[2], ty[2], SHADE);
    t.fillTriangle(hx[2], hy[2], tx[2], ty[2], tx[1], ty[1], SHADE);
    t.fillTriangle(tx[0], ty[0], tx[1], ty[1], tx[2], ty[2], FACE);
    static const uint8_t TH[9][2] = { {0,0},{1,2},{2,4},{0,1},{0,5},{1,1},{1,3},{2,3},{2,5} };
    for (const auto& e : TH) t.drawLine(tx[e[0]], ty[e[0]], hx[e[1]], hy[e[1]], INK);
    for (int k = 0; k < 3; k++) t.drawLine(tx[k], ty[k], tx[(k + 1) % 3], ty[(k + 1) % 3], INK);
    for (int k = 0; k < 6; k++) t.drawLine(hx[k], hy[k], hx[(k + 1) % 6], hy[(k + 1) % 6], INK);
    if (num) {
        char b[4]; snprintf(b, sizeof b, "%u", (unsigned)num);
        const uint8_t callerFont = t.textfont;          // the counters are drawn after this, in theirs
        t.setTextFont(2); t.setTextSize(u); t.setTextColor(INK);
        const int x = (int)cx - t.textWidth(b) / 2, y = (int)cy - 6 * u;
        t.setCursor(x, y); t.print(b);
        t.setCursor(x + 1, y); t.print(b);
        t.setTextSize(1); t.setTextFont(callerFont);
    }
}

// A shiny one (Encounters::activeShiny()): twinkles round whatever it is, and
// a trail of them where it has been. Once a frame, with where it is now.
void shinyAt(TFT_eSPI& t, int cx, int cy, int r, uint32_t now, int u) {
    if (now - s_trailAt >= 80) {
        s_trailAt = now;
        const uint32_t hsh = (uint32_t)(s_trailN + 1) * 2654435761u;
        ShinySpark& sp = s_trail[s_trailN % 10];
        sp.x = (int16_t)(cx + (int)(hsh >> 8) % (r + 1) - r / 2);
        sp.y = (int16_t)(cy + (int)(hsh >> 18) % (r + 1) - r / 2);
        sp.at = now ? now : 1;
        s_trailN++;
    }
    for (int i = 0; i < 10; i++) {
        const ShinySpark& sp = s_trail[i];
        if (!sp.at || now - sp.at > 800) continue;
        const float k = 1.0f - (float)(now - sp.at) / 800.0f;
        star4(t, sp.x, sp.y, (int)(4.0f * u * k) + 1, (i & 1) ? Theme::WHITE : (uint16_t)0xFFE0);
    }
    for (int k = 0; k < 4; k++) {
        const float a = (float)(now % 6283) / 1000.0f + k * 1.5708f;
        const float ph = sinf((float)(now % 8796) / 140.0f + k * 1.7f);
        if (ph > 0) star4(t, cx + (int)(r * cosf(a)), cy + (int)(r * 0.8f * sinf(a)), (int)((2.0f + 3.0f * ph) * u), (uint16_t)0xFFE0);
    }
}

// The mimic: the die, with a mouth across it full of teeth, eyes on top, and
// two feet under it. `open` 0 is a die that is only a die (`eyes`: or one
// that has opened them); `face` is the way it looks, `hop` how far off the
// floor it is.
void drawMimic(TFT_eSPI& t, int x, int footY, int r, float open, int face, bool eyes, int hop, float tilt, int u) {
    const bool awake = open > 0.05f;
    const int leg = awake ? 4 * u : 0;
    const int cy = footY - r - leg - hop;
    if (awake) {
        const int st = hop > 2 * u ? 2 * u : 0;                     // feet apart in the air
        t.fillRoundRect(x - 9 * u - st, footY - 4 * u - hop / 2, 7 * u, 4 * u, u, 0x9240);
        t.fillRoundRect(x + 2 * u + st, footY - 4 * u - hop / 2, 7 * u, 4 * u, u, 0x9240);
    }
    drawD20(t, (float)x, (float)cy, r, tilt, 0, u);
    if (awake) {
        const int mw = r * 8 / 10, my = cy + r / 4;
        int mh = (int)(open * r * 0.5f); if (mh < 2 * u) mh = 2 * u;
        t.fillRoundRect(x - mw, my - mh, 2 * mw, 2 * mh, 3 * u, 0x3000);        // the dark inside
        if (mh > 4 * u) t.fillCircle(x + face * 2 * u, my + mh - 3 * u, 3 * u, 0xF9A6);   // its tongue
        const int th = mh < 5 * u ? mh : 5 * u, tw = 2 * mw / 5;
        for (int i = 0; i < 5; i++) {                               // top teeth
            const int tx = x - mw + i * tw;
            t.fillTriangle(tx, my - mh, tx + tw, my - mh, tx + tw / 2, my - mh + th, Theme::WHITE);
        }
        for (int i = 0; i < 4; i++) {                               // bottom ones, set between them
            const int tx = x - mw + tw / 2 + i * tw;
            t.fillTriangle(tx, my + mh, tx + tw, my + mh, tx + tw / 2, my + mh - th, Theme::WHITE);
        }
        t.drawRoundRect(x - mw, my - mh, 2 * mw, 2 * mh, 3 * u, 0x0000);
    }
    if (eyes) {
        const int ey = cy - r * 48 / 100, er = 3 * u;
        for (int sd = -1; sd <= 1; sd += 2) {
            const int ex = x + sd * 5 * u;
            t.fillCircle(ex, ey, er, Theme::WHITE);
            t.fillCircle(ex + face * u, ey, er / 2 + 1, 0x0000);
            // A brow, slanting in: it means it.
            for (int d = 0; d < 2; d++)
                t.drawLine(ex + sd * 4 * u, ey - er - 2 * u + d, ex - sd * 2 * u, ey - er + u + d, 0x0000);
        }
    }
}

// What it says it rolled. A 20 says so in gold and a 1 in red.
void drawRolled(TFT_eSPI& t, int x, int y, uint8_t num, int u, uint32_t now) {
    char b[14];
    if (num == 20) snprintf(b, sizeof b, "NATURAL 20!");
    else           snprintf(b, sizeof b, "ROLLED %u", (unsigned)num);
    const uint8_t callerFont = t.textfont;
    t.setTextFont(2); t.setTextSize(u);
    const int bw = t.textWidth(b) + 10 * u, bh = 22 * u;
    if (x + bw > t.width() - 2) x = t.width() - 2 - bw;
    const bool flash = num == 20 && ((now / 110) & 1);
    t.fillRoundRect(x, y, bw, bh, 3 * u, Theme::BG);
    t.drawRoundRect(x, y, bw, bh, 3 * u, num == 20 ? (uint16_t)0xFFE0 : Theme::VAPOR_PINK);
    t.setTextColor(num == 1 ? Theme::RED : flash ? Theme::WHITE : Theme::AMBER);
    t.setCursor(x + 5 * u, y + 3 * u); t.print(b);
    t.setTextSize(1); t.setTextFont(callerFont);
}

void drawPuff(TFT_eSPI& t, int cx, int cy, float k, int u) {
    const uint16_t LO = Theme::blend(Theme::BG, Theme::WHITE, 200);
    for (int j = 0; j < 8; j++) {
        const float a = (j * 45 + 15) * 0.0174533f;
        const float dist = (6.0f + 26.0f * k) * u * ((j & 1) ? 1.0f : 0.7f);
        const int r = (int)((9.0f * (1.0f - fabsf(2.0f * k - 0.7f)) + 3.0f) * u);
        if (r < 1) continue;
        t.fillCircle(cx + (int)(dist * cosf(a)), cy + (int)(dist * 0.75f * sinf(a)) - (int)(8.0f * u * k), r, (j % 3) ? Theme::WHITE : LO);
    }
    if (k < 0.35f) t.fillCircle(cx, cy, (int)(16.0f * u * (1.0f - k)), Theme::WHITE);
}

void dieStart(Kind k, uint32_t now) {
    s_die = Die::DROP; s_dieAt = now; s_dieFor = k; s_dieSaid = 0;
    if (Encounters::activeShiny()) s_dieNum = 20;
    else switch (Encounters::tierOf(k)) {
        case Encounters::Tier::COMMON:    s_dieNum = (uint8_t)random(6, 13);  break;
        case Encounters::Tier::UNCOMMON:  s_dieNum = (uint8_t)random(13, 17); break;
        case Encounters::Tier::RARE:      s_dieNum = (uint8_t)random(17, 19); break;
        default:                          s_dieNum = 19;                      break;
    }
    Squachy::setHush(true);
}
void missStart(uint32_t now) {
    s_die = Die::MISS; s_dieAt = now; s_dieFor = Kind::NONE; s_dieSaid = 0;
    s_dieNum = Encounters::missRolled();
    if (s_dieNum < 1 || s_dieNum > 5) s_dieNum = 3;
    Squachy::setHush(true);
}

// ---- the free roll's die --------------------------------------------------------
// What he says when one turns up: how to throw it on this board.
#if defined(CARDPUTER_ADV)
const char* const FREE_LINE = "A die! Press P to roll it.";
#elif defined(STICKS3)
const char* const FREE_LINE = "A die! FREE ROLL, in the journal.";
#else
const char* const FREE_LINE = "A die! Tap it to roll.";
#endif

// Thrown: up it goes, from wherever it had got to. What it rolls is decided
// when it has left the screen (encounterBack), and it comes back down as any
// roll does.
void freeToss(uint32_t now) {
    s_die = Die::TOSS; s_dieAt = now; s_dieSaid = 0; s_dieFor = Kind::NONE;
    const bool drawnYet = s_freeCx > -500;
    s_tossX = drawnYet ? s_freeCx : s_freeX; s_tossY = drawnYet ? s_freeCy : s_freeY;
    s_freeStayMs = 0;
    s_freeShown = false; s_freeOutAt = 0; s_playAt = 0;
    s_freeWant = false; s_freeWantAt = 0;
    Squachy::setHush(true);
}
// Touched. Now and then it was never a die (Encounters::takeMimic()): then
// there is no throw, and what was sitting there wakes up where it is.
void freeThrow(uint32_t now) {
    if (Encounters::takeMimic()) {
        s_mimTapped = true;
        s_freeStayMs = 0;
        s_freeShown = false; s_freeOutAt = 0; s_playAt = 0;
        s_freeWant = false; s_freeWantAt = 0; s_freeSaid = false;
        return;
    }
    freeToss(now);
}

// ---- Squachy and the waiting die --------------------------------------------------
// A game, on the clock since it began: the die rolls over to him, nudges his
// foot twice, and he boots it -- to the edge of the screen, off it, and back
// to where it sits. If he has wandered off by the time it gets there it
// rolls home again.
const uint32_t PLAY_BUMP_MS = 450, PLAY_WIND_MS = 130, PLAY_BACK_MS = 750;
uint32_t playRollMs(int u) {
    const int d = abs(s_freeX - s_playTx);
    const uint32_t ms = (uint32_t)((float)d / (0.14f * u));
    return ms < 500 ? 500 : ms > 1600 ? 1600 : ms;
}
uint32_t playFlyMs(int w, int u) {
    const int d = (w - s_freeR - 1) - s_playTx;
    return (uint32_t)((float)(d > 0 ? d : 1) / (0.5f * u)) + 1;
}
// Where it is `pe` into a game, and how long ago it hit the edge (`burst`,
// -1 for not just now). False when the game is over.
bool playPos(uint32_t pe, int w, int u, float& x, float& y, float& ang, int& burst) {
    const float home = (float)s_freeX, tx = (float)s_playTx, r = (float)s_freeR;
    const uint32_t d1 = playRollMs(u);
    burst = -1; y = (float)s_freeY;
    if (pe < d1) {                                    // over to him, rolling
        const float k = (float)pe / (float)d1, e = k * k * (3.0f - 2.0f * k);
        x = home + (tx - home) * e; ang = (x - home) / r * 57.3f;
        return true;
    }
    pe -= d1;
    const float at = (tx - home) / r * 57.3f;
    if (!s_playKick) {                                // he had moved on: home again
        if (pe >= d1) return false;
        const float k = (float)pe / (float)d1, e = k * k * (3.0f - 2.0f * k);
        x = tx + (home - tx) * e; ang = (x - home) / r * 57.3f;
        return true;
    }
    if (pe < PLAY_BUMP_MS + PLAY_WIND_MS) {           // two nudges at his foot, and it waits
        const float k = pe < PLAY_BUMP_MS ? (float)pe / (float)PLAY_BUMP_MS : 1.0f;
        x = tx + fabsf(sinf(k * 6.2831853f)) * 3.0f * u; ang = at;
        return true;
    }
    pe -= PLAY_BUMP_MS + PLAY_WIND_MS;
    const uint32_t fly = playFlyMs(w, u);
    const float edge = (float)(w - s_freeR - 1);
    if (pe < fly) {                                   // booted: low and fast, to the edge
        const float k = (float)pe / (float)fly;
        x = tx + (edge - tx) * k; y -= 4.0f * k * (1.0f - k) * 16.0f * u; ang = at + (float)pe * 1.3f;
        return true;
    }
    pe -= fly;
    if (pe >= PLAY_BACK_MS) return false;
    const float k = (float)pe / (float)PLAY_BACK_MS;  // off the edge, two small bounces, back where it sits
    x = edge + (home - edge) * k; y -= fabsf(sinf(k * 6.2831853f)) * 7.0f * u * (1.0f - k);
    ang = 360.0f * (1.0f - k) * (1.0f - k);
    if (pe < 130) burst = (int)pe;
    return true;
}
// Starts a game when the two of them have nothing else on, and gives him
// his cues in it.
void playTick(uint32_t now, int w, int u) {
    int cx = 0, hw = 0, top = 0, bot = 0;
    const bool there = Squachy::lastFootprint(cx, hw, top, bot);
    if (there && abs(cx - s_sqAnchor) > 3 * u) { s_sqAnchor = cx; s_sqStillAt = now; }
    if (!s_freeShown) { s_playAt = 0; return; }
    if (!s_playAt) {
        if (!there || (int32_t)(now - s_playNext) < 0 || now - s_freeAt < 4000 || now - s_sqStillAt < 1500) return;
        if (Squachy::thrown() || Squachy::isHeld() || Squachy::showOffActive() || Squachy::dancing()) return;
        const int tx = cx + hw * 55 / 100 + s_freeR;
        if (tx > s_freeX - 20 * u) return;            // he is standing over it already
        s_playAt = now ? now : 1; s_playSaid = 0; s_playTx = tx; s_playCx0 = cx; s_playKick = true;
        return;
    }
    const uint32_t pe = now - s_playAt, d1 = playRollMs(u);
    if (!(s_playSaid & 1) && pe >= d1) {
        s_playSaid |= 1;
        s_playKick = there && abs(cx - s_playCx0) <= 6 * u && !Squachy::thrown() && !Squachy::isHeld();
        if (s_playKick) Squachy::visitPose(now, PLAY_BUMP_MS, Squachy::VisitPose::CROUCH);   // he looks down at it
    }
    if (s_playKick && !(s_playSaid & 2) && pe >= d1 + PLAY_BUMP_MS) {
        s_playSaid |= 2;
        static const char* const BOOT[] = { "Go long!", "Fetch.", "Not yet, little die.", "Shoo." };
        Squachy::visitPose(now, 380, Squachy::VisitPose::KICK);
        if (!Squachy::reacting()) Squachy::announce(BOOT[(now / 7) % 4]);   // when he is not in the middle of saying something
    }
    float x, y, a; int b;
    if (!playPos(pe, w, u, x, y, a, b)) {
        s_playAt = 0;
        s_playNext = now + 38000 + ((now * 2654435761u) >> 17) % 30000;
    }
}

// On the floor, waiting. It rolls in from the edge, hops now and then so it
// reads as something to tap, plays with him (playPos), and rolls off again
// if the screen is wanted for something else.
void freeDraw(TFT_eSPI& t, uint32_t now, int w, int h) {
    if (s_freeR <= 0) return;
    const int u = h >= 400 ? 2 : 1;
    if (!s_freeShown) {
        if (!s_freeOutAt || now - s_freeOutAt >= 420) return;
        const float k = (float)(now - s_freeOutAt) / 420.0f;
        const float far = (float)(w + s_freeR + 2 - s_outX);
        drawD20(t, (float)s_outX + far * k * k, (float)s_freeY, s_freeR, 300.0f * k * k, 0, u);
        return;
    }
    const uint32_t ms = now - s_freeAt;
    float x = (float)s_freeX, y = (float)s_freeY, ang = 0;
    int burst = -1, glint = -1;
    if (s_playAt && playPos(now - s_playAt, w, u, x, y, ang, burst)) {
        // in the middle of a game
    } else if (ms < 620) {
        const float q = 1.0f - (float)ms / 620.0f;
        x += (float)(w + s_freeR + 2 - s_freeX) * q * q; ang = 360.0f * q * q;
    } else {
        const uint32_t c = (ms - 620) % 8000;                       // a hop every eight seconds, and a glint between
        if (c >= 7600) {
            const float q = (float)(c - 7600) / 400.0f;
            y -= 4.0f * q * (1.0f - q) * 9.0f * u; ang = 120.0f * q;
        } else if (c >= 3600 && c < 4000) {                         // a glint off its corner
            glint = (int)(c - 3600) < 200 ? (int)(c - 3600) : 400 - (int)(c - 3600);
        }
    }
    s_freeCx = (int)x; s_freeCy = (int)y;
    drawD20(t, x, y, s_freeR, ang, 0, u);
    if (glint >= 0) star4(t, s_freeX + s_freeR * 6 / 10, s_freeY - s_freeR * 7 / 10, (2 + glint / 50) * u, Theme::WHITE);
    if (burst >= 0) drawBurst(t, w - 3 * u, (int)y, (uint32_t)burst, u);
}
bool dieCue(uint8_t bit, uint32_t ms, uint32_t at) {
    if (ms < at || (s_dieSaid & (1u << bit))) return false;
    s_dieSaid |= 1u << bit;
    return true;
}

// The die's own script. A hit: it lands (he jumps), it is read, and then
// either the visitor it rolled is let in to kick it, or it goes in a puff and
// the encounter starts. A miss: it lands, he looks, he shrugs, it rolls off.
void dieTick(uint32_t now, int w, int floorY, int u) {
    if (s_die == Die::NONE) return;
    const uint32_t ms = now - s_dieAt;
    float x, y, a; uint32_t rest;
    dieAt(0, 0, floorY, u, x, y, a, rest);
    switch (s_die) {
        case Die::DROP:
            if (dieCue(0, ms, rest)) Squachy::visitPose(now, 900, Squachy::VisitPose::STARTLED);
            if (s_dieNum == 20 && dieCue(1, ms, rest + 750)) {   // the best there is
                Squachy::announce("NATURAL TWENTY!");
                Squachy::visitPose(now, 1800, Squachy::VisitPose::CHEER);
            }
            if (ms < rest + dieRead()) break;
            if (Encounters::active() != s_dieFor) {             // called off while it rolled
                s_dieFor = Kind::NONE; s_die = Die::PUFF; s_dieAt = now;
            } else if (drawn(s_dieFor)) {
                s_die = Die::PUFF; s_dieAt = now;
            } else {
                encStart(s_dieFor, now);
                s_dieFor = Kind::NONE; s_die = Die::WAIT; s_dieAt = now;
            }
            break;
        case Die::PUFF:
            if (ms < 350) break;
            s_die = Die::NONE;
            if (s_dieFor != Kind::NONE && Encounters::active() == s_dieFor) encStart(s_dieFor, now);
            else Squachy::setHush(false);
            s_dieFor = Kind::NONE;
            break;
        case Die::WAIT:
            if (s_vp == VisitPhase::ARRIVING && encGuestHere() && now - s_vpAt >= ENC_KICK_MS) {
                s_die = Die::FLY; s_dieAt = now;
                Squachy::visitPose(now, 650, Squachy::VisitPose::CROUCH);    // it goes over his head
            } else if (ms > 6000) {                             // nobody came for it
                Serial.printf("[encounter] nobody came for the die (crowd %u, visit phase %u)\n",
                              (unsigned)Settings::meshCrowd(), (unsigned)s_vp);
                s_die = Die::PUFF; s_dieAt = now;
            }
            break;
        case Die::FLY:
            if (ms > 1200) s_die = Die::NONE;
            break;
        case Die::MISS: {
            static const char* const WHAT[] = { "Huh?", "What's this?", "Uh. Hello?" };
            static const char* const NOPE[] = { "Nothing? Weird.", "Guess not.", "Okay then." };
            if (s_dieNum == 1) {
                // The worst there is. He has just time to wonder about it.
                if (dieCue(0, ms, rest))        Squachy::visitPose(now, 900, Squachy::VisitPose::STARTLED);
                if (dieCue(1, ms, rest + 1150)) {
                    Squachy::announce("A one?!");
                    Squachy::visitPose(now, 1100, Squachy::VisitPose::SCRATCH);
                }
                if (ms >= rest + 2500) {
                    s_die = Die::BONK; s_dieAt = now; s_dieSaid = 0;
                    int hw, top, bot;
                    if (!Squachy::lastFootprint(s_bonkX, hw, top, bot)) s_bonkX = w / 2;
                    s_bonkY = Squachy::crownY();
                }
                break;
            }
            if (dieCue(0, ms, rest))        Squachy::visitPose(now, 900, Squachy::VisitPose::STARTLED);
            if (dieCue(1, ms, rest + 1150)) {
                Squachy::announce(WHAT[s_dieNum % 3]);
                Squachy::visitPose(now, 1700, Squachy::VisitPose::SCRATCH);
            }
            if (dieCue(2, ms, rest + 3000)) Squachy::visitPose(now, 1500, Squachy::VisitPose::CROUCH);
            if (dieCue(3, ms, rest + 4700)) {
                Squachy::announce(NOPE[s_dieNum % 3]);
                Squachy::visitPose(now, 1300, Squachy::VisitPose::LOOK_AROUND);
            }
            if (ms >= rest + 6200) { s_die = Die::AWAY; s_dieAt = now; }
            break;
        }
        case Die::AWAY:
            if (ms > 1600) { s_die = Die::NONE; Squachy::setHush(false); }
            break;
        case Die::BONK: {
            static const char* const RUDE[] = { "Rude.", "Not my lucky day.", "I felt that." };
            if (ms < BONK_HIT_MS) {                     // he may shift his feet while it is in the air
                int cx, hw, top, bot;
                if (Squachy::lastFootprint(cx, hw, top, bot)) { s_bonkX = cx; s_bonkY = Squachy::crownY(); }
            }
            if (dieCue(0, ms, BONK_HIT_MS)) {
                Squachy::announce("OW!");
                Squachy::visitPose(now, 500, Squachy::VisitPose::CROUCH);
            }
            if (dieCue(1, ms, BONK_HIT_MS + 520))  Squachy::visitPose(now, 1700, Squachy::VisitPose::SAD);
            if (dieCue(2, ms, BONK_HIT_MS + 1500)) Squachy::announce(RUDE[(now / 7) % 3]);
            if (ms > BONK_HIT_MS + 2700) { s_die = Die::NONE; Squachy::setHush(false); }
            break;
        }
        default: break;      // TOSS: encounterBack rolls it when it has left the screen
    }
}

// Draws it, over everything on the floor. True while there is one.
bool dieDraw(TFT_eSPI& t, uint32_t now, int w, int h, int floorY) {
    if (s_die == Die::NONE) return false;
    const int u = h >= 400 ? 2 : 1;
    const uint32_t ms = now - s_dieAt;
    const int rx = dieRestX(w), R = 15 * u, big = 5 * u;
    // A natural 20 is gold, and flashes.
    const uint8_t glow = s_dieNum == 20 ? (((now / 110) & 1) ? 2 : 1) : 0;
    switch (s_die) {
        case Die::DROP: case Die::MISS: {
            float x, y, a; uint32_t rest;
            if (!dieAt(ms, rx, floorY, u, x, y, a, rest)) { drawD20(t, x, y, R, a, 0, u); break; }
            // It swells a touch as it settles, to show its face.
            const int grow = ms - rest < 165 ? (int)((ms - rest) * big / 165) : big;
            if (glow) drawGlory(t, rx, (int)y - grow, R + grow, ms - rest, u, floorY);
            drawD20(t, (float)rx, y - grow, R + grow, 0, s_dieNum, u, glow);
            if (ms >= rest + 130 && ms < rest + 130 + dieRead() - 100)
                drawRolled(t, rx + R + big + 6 * u, floorY - 32 * u, s_dieNum, u, now);
            break;
        }
        case Die::WAIT:
            drawD20(t, (float)rx, (float)(floorY - R - big), R + big, 0, s_dieNum, u, glow);
            break;
        case Die::FLY: {
            const float f = (float)ms / 33.0f;
            const float x = (float)rx - 8.0f * u * f, y = (float)(floorY - R - big) - 18.0f * u * f + 0.5f * u * f * f;
            if (x > -2 * R && y > -2 * R) drawD20(t, x, y, R, -31.0f * f, 0, u, glow ? 1 : 0);
            if (ms < 130) drawBurst(t, rx + R + big + 2 * u, floorY - 16 * u, ms, u);   // the boot landing
            break;
        }
        case Die::TOSS: {
            // Flicked up off the floor, turning, and out of the top of the screen.
            const float f = (float)ms / 33.0f;
            const float x = (float)s_tossX - 1.0f * u * f, y = (float)s_tossY - 24.0f * u * f + 0.35f * u * f * f;
            const int r = s_freeR + (int)(0.3f * u * f) > R ? R : s_freeR + (int)(0.3f * u * f);
            if (y > -2.0f * R) drawD20(t, x, y, r, 41.0f * f, 0, u);
            if (ms < 130) drawBurst(t, s_tossX, s_tossY + s_freeR - 2 * u, ms, u);
            break;
        }
        case Die::BONK: {
            const float sx = (float)rx, sy = (float)(floorY - R - big);
            const float tx = (float)s_bonkX, ty = (float)(s_bonkY - R + 2 * u);
            if (ms < BONK_HIT_MS) {                              // up off the floor in an arc, at his head
                const float k = (float)ms / (float)BONK_HIT_MS;
                drawD20(t, sx + (tx - sx) * k, sy + (ty - sy) * k - 4.0f * k * (1.0f - k) * 28.0f * u, R, -330.0f * k, 0, u);
                break;
            }
            const uint32_t since = ms - BONK_HIT_MS;
            const float f = (float)since / 33.0f;
            // Off his head and away, on over the side it did not come from.
            const float dir = tx <= sx ? -1.0f : 1.0f;
            const float x = tx + dir * 6.0f * u * f, y = ty - 11.0f * u * f + 0.12f * u * f * f;
            if (x > -2.0f * R && x < (float)(w + 2 * R) && y > -2.0f * R) drawD20(t, x, y, R, dir * 29.0f * f, 0, u);
            if (since < 130) drawBurst(t, (int)tx, (int)ty + R, since, u);
            if (since < 2400) {
                // He sees stars: three of them, round and round the top of
                // his head, wherever that is by now.
                int cx, hw, top, bot;
                if (Squachy::lastFootprint(cx, hw, top, bot)) {
                    const float gs = Squachy::lastScale();
                    const int hy = Squachy::crownY();
                    for (int j = 0; j < 3; j++) {
                        const float aa = (float)since / 190.0f + j * 2.0944f;
                        star4(t, cx + (int)(17.0f * gs * cosf(aa)), hy - (int)(2.0f * gs) + (int)(5.0f * gs * sinf(aa)),
                              (int)(2.6f * gs) + 1, (uint16_t)0xFFE0);
                    }
                }
            }
            break;
        }
        case Die::PUFF:
            drawPuff(t, rx, floorY - R, ms / 350.0f, u);
            break;
        case Die::AWAY: {
            const float j = (float)ms / 33.0f;
            const float x = (float)rx + (0.11f * j * j + 1.2f * j) * u;
            const int r = (R + big - (int)(j * u)) > R ? (R + big - (int)(j * u)) : R;
            if (x < (float)(w + 2 * R)) drawD20(t, x, (float)(floorY - r), r, j * 13.0f + j * j * 0.25f, 0, u);
            break;
        }
        default: break;
    }
    return true;
}

// The horizon the Elder and the Kaiju stand behind on the sunset: the
// mountains' line, about a third of the way down the band. The other
// backgrounds have no ridge there to stand behind -- see ridge().
int horizonY(int bandTop, int floorY) { return bandTop + (floorY - bandTop) * 36 / 100; }
// Only the sunset has mountains across that line. Anywhere else, cut off
// there, the two of them hung in the sky with a flat bottom: so the giant
// flies whole, and the Elder comes up out of the ground instead.
bool ridge() { return Settings::background() == Settings::Background::SYNTHWAVE; }

// ---- the Elder ----------------------------------------------------------------
// A Bigfoot outline in the dusk's darkest purple, its sun side picked out in
// orange, and his eyes where a Squachy's shades sit: two lit slits.
// Nothing of him is drawn at row `cut` or below: he stands behind the horizon,
// and no background can be asked to repaint only the ground in front of him.
void elderDisc(TFT_eSPI& t, int cx, int cy, int r, uint16_t col, int cut) {
    if (cy + r < cut) { t.fillCircle(cx, cy, r, col); return; }
    for (int dy = -r; dy <= r && cy + dy < cut; dy++) {
        const int half = (int)sqrtf((float)(r * r - dy * dy));
        t.drawFastHLine(cx - half, cy + dy, 2 * half + 1, col);
    }
}
void elderBox(TFT_eSPI& t, int x, int y, int w, int h, int r, uint16_t col, int cut) {
    if (y >= cut) return;
    if (y + h <= cut) { t.fillRoundRect(x, y, w, h, r, col); return; }
    // Its rounded top as far as that fits, then square down to the line.
    if (y + 2 * r + 1 <= cut) {
        t.fillRoundRect(x, y, w, 2 * r + 1, r, col);
        t.fillRect(x, y + r, w, cut - (y + r), col);
    } else {
        for (int yy = y; yy < cut; yy++) {
            const int d = r - (yy - y);
            const int in = d > 0 ? r - (int)sqrtf((float)(r * r - d * d)) : 0;
            t.drawFastHLine(x + in, yy, w - 2 * in, col);
        }
    }
}
void elderSpike(TFT_eSPI& t, int x1, int x2, int baseY, int ax, int ay, uint16_t col, int cut) {
    if (ay >= cut) return;
    if (baseY < cut) { t.fillTriangle(x1, baseY, x2, baseY, ax, ay, col); return; }
    const float k = (float)(cut - 1 - ay) / (float)(baseY - ay);
    t.fillTriangle(ax + (int)((x1 - ax) * k), cut - 1, ax + (int)((x2 - ax) * k), cut - 1, ax, ay, col);
}
void drawElderBody(TFT_eSPI& t, int cx, int feetY, int H, uint16_t col, bool waving, int dx, int dy, int cut) {
    const int top = feetY - H;
    auto R = [H](int pct) { return H * pct / 100; };
    cx += dx; const int y0 = top + dy;
    for (int i = -1; i <= 1; i++)   // the hair: three spikes
        elderSpike(t, cx + i * R(9) - R(7), cx + i * R(9) + R(7), y0 + R(10), cx + i * R(12), y0 - R(3), col, cut);
    elderDisc(t, cx - R(19), y0 + R(27), R(6), col, cut);      // ears
    elderDisc(t, cx + R(19), y0 + R(27), R(6), col, cut);
    elderDisc(t, cx, y0 + R(25), R(20), col, cut);             // head
    elderBox(t, cx - R(24), y0 + R(38), R(48), R(45), R(8), col, cut);   // body
    // arms: the near one (towards Squachy, on the left) up to wave
    if (waving) elderBox(t, cx - R(38), y0 + R(4), R(12), R(44), R(6), col, cut);
    else        elderBox(t, cx - R(34), y0 + R(42), R(12), R(38), R(6), col, cut);
    elderBox(t, cx + R(22), y0 + R(42), R(12), R(38), R(6), col, cut);
    elderBox(t, cx - R(18), y0 + R(80), R(14), R(20), R(4), col, cut);  // legs
    elderBox(t, cx + R(4),  y0 + R(80), R(14), R(20), R(4), col, cut);
}
void drawElder(TFT_eSPI& t, int cx, int feetY, int H, bool waving, float eyes, bool flare, uint32_t now, int cut = 32767) {
    const uint16_t BODY = 0x1044, RIM = 0xFB65, GLOW = 0xFDA0, CORE = 0xFF75;
    drawElderBody(t, cx, feetY, H, RIM, waving, -1, -1, cut);     // the rim first, a pixel up and left
    drawElderBody(t, cx, feetY, H, BODY, waving, 0, 0, cut);
    if (eyes <= 0.0f) return;
    const int top = feetY - H, ey = top + H * 25 / 100;
    if (ey + H / 14 >= cut) return;                              // his eyes are still under the ridge
    const int w = (int)(H * 11 / 100 * eyes), h = H / 40 > 1 ? H / 40 : 1;
    for (int side : { -1, 1 }) {
        const int ex = cx + side * H * 8 / 100;
        t.fillRect(ex - w / 2, ey - h, w, 2 * h + 1, GLOW);
        if (w > 4) t.fillRect(ex - w / 4, ey, w / 2, 1, CORE);
        if (flare) {   // the moment they light: a star on each
            t.drawFastHLine(ex - H / 8, ey, H / 4, CORE);
            t.drawFastVLine(ex, ey - H / 14, H / 7, CORE);
        }
    }
    (void)now;
}

// The Elder's script, on the clock since he began (ms):
//   800-3300 he rises; 3300 his eyes light; 6000 Squachy waves and he waves
//   back; tap him any time he is up for the photo; 12000-13500 he sinks.
const uint32_t EL_RISE0 = 800, EL_RISE1 = 3300, EL_SINK0 = 12000, EL_SINK1 = 13500, EL_END = 15500;
float elderUp(uint32_t ms) {
    if (ms < EL_RISE0) return 0;
    if (ms < EL_RISE1) return (float)(ms - EL_RISE0) / (EL_RISE1 - EL_RISE0);
    if (ms < EL_SINK0) return 1;
    if (ms < EL_SINK1) return 1.0f - (float)(ms - EL_SINK0) / (EL_SINK1 - EL_SINK0);
    return 0;
}

// ---- the small things that walk ----------------------------------------------------
void legs(TFT_eSPI& t, int x, int y, int len, bool walking, uint32_t now, uint16_t col) {
    const int k = walking ? (((now / 120) % 2) ? 2 : -2) : 0;
    t.drawLine(x - 4, y, x - 4 + k, y + len, col);
    t.drawLine(x - 3, y, x - 3 + k, y + len, col);
    t.drawLine(x + 3, y, x + 3 - k, y + len, col);
    t.drawLine(x + 4, y, x + 4 - k, y + len, col);
}
void eyesAt(TFT_eSPI& t, int x, int y, int r) {
    t.fillCircle(x - r - 1, y, r, Theme::WHITE); t.fillCircle(x + r + 1, y, r, Theme::WHITE);
    t.fillCircle(x - r - 2, y, r / 2 > 0 ? r / 2 : 1, 0x0841); t.fillCircle(x + r, y, r / 2 > 0 ? r / 2 : 1, 0x0841);
}

void drawLostSignal(TFT_eSPI& t, int x, int floorY, int u, uint8_t bars, bool walking, uint32_t now) {
    const uint16_t ON = Theme::CYAN, OFF = Theme::blend(Theme::BG, Theme::CYAN, 70);
    const int baseY = floorY - 8 * u;
    legs(t, x, baseY + 2 * u, 6 * u, walking, now, ON);
    t.fillRoundRect(x - 13 * u, baseY - 8 * u, 26 * u, 10 * u, 3 * u, 0x1132);
    t.drawRoundRect(x - 13 * u, baseY - 8 * u, 26 * u, 10 * u, 3 * u, ON);
    for (int i = 0; i < 4; i++) {
        const int h = (6 + 4 * i) * u;
        t.fillRect(x - 11 * u + i * 6 * u, baseY - 9 * u - h, 4 * u, h, i < bars ? ON : OFF);
    }
    t.fillRect(x - 6 * u, baseY - 6 * u, 3 * u, 3 * u, Theme::WHITE);
    t.fillRect(x + 4 * u, baseY - 6 * u, 3 * u, 3 * u, Theme::WHITE);
}

void drawUfo(TFT_eSPI& t, int cx, int cy, int u, uint32_t now) {
    t.fillEllipse(cx, cy, 24 * u, 8 * u, Theme::blend(Theme::BG, Theme::WHITE, 220));
    t.fillEllipse(cx, cy - 6 * u, 12 * u, 10 * u, Theme::blend(Theme::BG, Theme::CYAN, 200));
    const uint16_t lc = ((now / 150) % 2 == 0) ? Theme::RED : Theme::AMBER;
    for (int i = -1; i <= 1; i++) t.fillCircle(cx + i * 14 * u, cy + 6 * u - (i ? 2 * u : 0), 2 * u, lc);
}
void drawBeam(TFT_eSPI& t, int cx, int top, int bottom, int u, uint32_t now) {
    // Scanlines of the beam rather than a fill: it reads as light, and what
    // it carries still shows through it.
    const uint16_t c = Theme::blend(Theme::BG, Theme::CYAN, 120);
    for (int y = top; y < bottom; y += 3) {
        const int half = 14 * u + (y - top) * 16 * u / (bottom - top + 1);
        if (((y / 3) + now / 80) % 2) t.drawFastHLine(cx - half, y, 2 * half, c);
    }
}

void flash(TFT_eSPI& t, int w, int h) { t.fillRect(0, 0, w, h, Theme::WHITE); }

void polaroid(TFT_eSPI& t, int x, int y, int u, uint32_t now) {
    const int W = 60 * u, H = 64 * u;
    t.fillRect(x, y, W, H, 0xF79E);
    t.fillRect(x + 4 * u, y + 4 * u, W - 8 * u, 42 * u, 0x2124);
    drawElder(t, x + W / 2, y + 46 * u, 36 * u, false, 1.0f, false, now);
    t.setTextFont(1); t.setTextSize(u > 1 ? 2 : 1);
    t.setTextColor(0x3186, 0xF79E);
    const char* cap = "THE ELDER";
    t.setCursor(x + (W - t.textWidth(cap)) / 2, y + 50 * u);
    t.print(cap);
    t.setTextSize(1);
}
}  // namespace

// The tap a random encounter wants: the Lost Signal, or the Elder for his photo.
bool uiClearEncounterTap(int x, int y, uint32_t now) {
    if (s_freeShown) {
        // The free roll's die: a finger's width round it.
        const int dx = x - s_freeCx, dy = y - s_freeCy, r = s_freeR * 2 + 1;
        if (dx * dx + dy * dy <= r * r) { freeThrow(now); return true; }
    }
    if (s_encK == Kind::NONE || s_encTapped || s_encHitR <= 0) return false;
    const int dx = x - s_encHitX, dy = y - s_encHitY;
    if (dx * dx + dy * dy > s_encHitR * s_encHitR) return false;
    s_encTapped = true;
    s_encTapAt  = now;
    if (s_encK == Kind::ELDER) {
        Encounters::notePhoto();
        Theme::showToast("THE ELDER", "A photo for the journal", Theme::AMBER);
    }
    return true;
}

bool uiClearFreeRoll() {
    if (!Encounters::freeReady()) return false;
    s_freeWant = true; s_freeWantAt = 0;
    return true;
}
void uiClearDiePlayNow() { s_playNext = 0; s_sqStillAt = 0; }

// Rolls the dice, starts what they picked, and ends it. Then draws the parts
// that go behind Squachy.
static void encounterBack(TFT_eSPI& t, uint32_t now, const DetectionEngine& eng, int w, int h,
                          int bandTop, int floorY, bool eligible) {
    // The dice: only on the bare main screen, with nobody visiting.
    Encounters::Context c{};
    const int u = h >= 400 ? 2 : 1;
    {
        c.lastCatch = (uint8_t)Squachy::lastCaught();
        const uint32_t rate = advertRate();
        c.city   = rate > 40;
        c.woods  = rate < 6;
        c.night  = Clock::night();
        c.pet    = Squachy::petChoice() != Squachy::PetId::OFF && Pet::anyOut(now);
        c.visits = true;
        Encounters::tick(now, eligible && s_encK == Kind::NONE && s_vp == VisitPhase::GONE, c);
    }
    // The free roll's die: on the floor while the screen is his alone.
    {
        s_freeR = 11 * u; s_freeX = w - 26 * u; s_freeY = floorY - s_freeR;
        const bool show = eligible && s_encK == Kind::NONE && s_vp == VisitPhase::GONE && s_die == Die::NONE &&
                          Encounters::active() == Kind::NONE && Encounters::freeReady();
        if (show && !s_freeShown) {
            s_freeAt = now; s_freeOutAt = 0; s_playAt = 0;
            s_freeCx = w + s_freeR; s_freeCy = s_freeY;
            s_playNext = now + 9000 + ((now * 2654435761u) >> 17) % 6000;   // their first game, soon
        }
        if (!show && s_freeShown) {                                  // called away
            s_freeOutAt = now ? now : 1; s_outX = s_freeCx; s_playAt = 0;
        }
        s_freeShown = show;
        if (s_freeWant && !s_freeWantAt) s_freeWantAt = now ? now : 1;
        if (show) {
            // Thirty seconds in view and nobody has touched it: it rolls off, and
            // that one is gone. Not in the middle of a game with him.
            if (now != s_freeStayLast) {
                const uint32_t dt = now - s_freeStayLast;
                s_freeStayMs += dt > 100 ? 100 : dt;
            }
            if (s_freeWant) freeThrow(now);
            else if (s_freeStayMs >= Encounters::FREE_STAY_MS && !s_playAt) {
                s_freeStayMs = 0; s_freeSaid = false;
                Encounters::setFree(false);
                Serial.println("[encounter] the free roll's die went unrolled");
            }
            else if (!s_freeSaid && now - s_freeAt > 2500) { s_freeSaid = true; Squachy::announce(FREE_LINE); }
        }
        s_freeStayLast = now;
        playTick(now, w, u);
        // Asked for and the screen never came free: let it go.
        if (s_freeWant && now - s_freeWantAt > 20000) { s_freeWant = false; s_freeWantAt = 0; }
        if (s_die == Die::TOSS && now - s_dieAt >= DIE_TOSS_MS) {
            // Out of sight: now it is rolled, and what it rolled comes down.
            s_die = Die::NONE;
            s_freeSaid = false;
            Squachy::setHush(false);
            Encounters::rollFree(c);
        }
    }
    const Kind want = Encounters::active();
    if (want == Kind::NONE) {
        if (s_encK != Kind::NONE) encEnd();
    } else if (want != s_encK && want != s_dieFor) {
        // Picked: the die goes down first, and what it rolled starts when
        // it has been read (dieTick). Not for the mimic, which is the die.
        if (s_encK != Kind::NONE) encStop();
        if (want == Kind::MIMIC) encStart(want, now);
        else                     dieStart(want, now);
    }
    if (s_die == Die::NONE && s_encK == Kind::NONE && eligible && s_vp == VisitPhase::GONE &&
        Encounters::takeMiss()) missStart(now);
    dieTick(now, w, floorY, u);
    if (s_encK == Kind::NONE) return;
    const uint32_t ms = encMs(now);
    s_encHitR = 0;
    const bool shiny = Encounters::activeShiny();

    switch (s_encK) {
        // ---- the visitors: they end when the visit does ----
        case Kind::DRIFTER:
            if (ms > 40000) s_encPeerOn = false;                       // time to go
            if ((ms > 4000 && s_vp == VisitPhase::GONE) || ms > 90000) encEnd();
            break;
        case Kind::GOBLIN:
            if (ms > 90000) s_encGobOn = false;
            if ((ms > 4000 && s_vp == VisitPhase::GONE) || ms > 120000) encEnd();
            break;
        case Kind::TWIN:
            // He does not walk out: at 32 s he glitches (encounterFront) and at
            // 34 s he is simply gone.
            if (ms > 34000 && s_encPeerOn) {
                s_encPeerOn = false;
                if (s_hostingId == (0xE6C00000u | (s_encSeq & 0xFFFFu))) { s_vp = VisitPhase::GONE; s_visitGuestLine = nullptr; }
                Squachy::announce("...was that me?");
            }
            if ((ms > 4000 && s_vp == VisitPhase::GONE && !s_encPeerOn) || ms > 90000) encEnd();
            break;

        // ---- behind Squachy ----
        case Kind::KAIJU: {
            // A giant Mothman wades along behind the horizon, right to left.
            const int hz = horizonY(bandTop, floorY);
            const int scale = (h >= 400 ? 9 : 5);
            const float k = ms / 12000.0f;
            const int x = w + 16 * scale - (int)((w + 32 * scale) * k);
            const int bob = (int)(2 * u * sinf(ms / 260.0f));
            // The ground in front of him, kept and put back over his lower half.
            const bool cut = ridge() && stripSave(t, x - 16 * scale, hz, 32 * scale, 6 * scale + 3 * u);
            Pet::drawMothman(t, x, hz - 2 * scale + bob, scale, now, false);
            if (cut) stripRestore(t);
            if (shiny) shinyAt(t, x, hz - 6 * scale, 15 * scale, now, u);
            reachUp(0, 1200, now);
            say(1, 2200, now, "Who's a VERY big boy?");
            reachUp(2, 7000, now);
            if (ms > 12500) { stripFree(); encEnd(); }
            break;
        }
        case Kind::ELDER: {
            const int hz = ridge() ? horizonY(bandTop, floorY) : floorY;
            const float up = elderUp(ms);
            const float dim = up;
            if (dim > 0.02f) Theme::dimRegion(t, 0, bandTop, w, horizonY(bandTop, floorY) - bandTop, (uint8_t)(110 * dim));
            if (up > 0) {
                const int H = (floorY - bandTop) * 62 / 100;
                const int cx = w * 80 / 100;
                const int feet = hz + H - (int)(up * H * 70 / 100);
                const bool waving = ms >= 6300 && ms < 7800;
                const float eyes = ms < EL_RISE1 ? 0.0f : ms < EL_RISE1 + 300 ? (ms - EL_RISE1) / 300.0f : 1.0f;
                const bool blink = ms >= 9000 && ms < 9150;
                drawElder(t, cx, feet, H, waving, blink ? 0.0f : eyes, ms >= EL_RISE1 && ms < EL_RISE1 + 500, now, hz);
                if (shiny && up > 0.5f) shinyAt(t, cx, hz - H * 30 / 100, H / 3, now, u);
                if (!s_encTapped && ms >= EL_RISE1 && ms < EL_SINK0) {
                    s_encHitX = cx; s_encHitY = feet - H * 70 / 100; s_encHitR = H / 3;
                }
            }
            say(0, 2300, now, "...");
            say(1, 4000, now, "Grandpa?");
            reachUp(2, 6000, now);
            laugh(3, 8300, now);
            if (ms > EL_END) encEnd();
            break;
        }
        default: break;
    }
}

// The parts drawn over Squachy, where the idle events go. True when an
// encounter is playing, so the idle events sit this one out.
static bool encounterPlay(TFT_eSPI& t, uint32_t now, int w, int h, int bandTop, int floorY);
static bool encounterFront(TFT_eSPI& t, uint32_t now, const DetectionEngine& eng, int w, int h, int bandTop, int floorY) {
    (void)eng;
    const bool played = encounterPlay(t, now, w, h, bandTop, floorY);
    freeDraw(t, now, w, h);                              // the free roll's, waiting: the idle events carry on round it
    return dieDraw(t, now, w, h, floorY) || played;      // the die goes on top of whatever is playing
}
static bool encounterPlay(TFT_eSPI& t, uint32_t now, int w, int h, int bandTop, int floorY) {
    if (s_encK == Kind::NONE) return false;
    const uint32_t ms = encMs(now);
    const int u = h >= 400 ? 2 : 1;
    const bool shiny = Encounters::activeShiny();
    // A shiny one of the dice's visitors: sparkles where he was drawn.
    if (shiny && !drawn(s_encK) && s_encGR > 0 && encGuestHere()) shinyAt(t, s_encGX, s_encGY, s_encGR, now, u);
    switch (s_encK) {
        case Kind::MIMIC: {
            const int R = 20 * u;                                 // awake, it is a good deal bigger than it let on
            int cx = w / 2, hw = 24 * u, top = 0, bot = floorY;
            Squachy::lastFootprint(cx, hw, top, bot);
            if (ms < s_mimWake) {                                 // one rolls in to be it
                const float q = 1.0f - (float)ms / (float)s_mimWake;
                drawD20(t, (float)s_freeX + (float)(w + s_freeR + 2 - s_freeX) * q * q, (float)s_freeY, s_freeR, 360.0f * q * q, 0, u);
                s_mimLast = now;
                return true;
            }
            const uint32_t m = ms - s_mimWake;
            if (m < 700) {                                        // it shivers. Then it opens its eyes.
                const float sh = sinf((float)m / 28.0f) * ((float)m / 700.0f) * 3.0f * u;
                drawMimic(t, (int)(s_mimX + sh), floorY, s_freeR, 0.0f, s_mimFace, m > 420, 0, sh * 2.0f, u);
                s_mimLast = now;
                return true;
            }
            if (cue(0, s_mimWake + 700, now)) {
                Squachy::announce("THAT IS NOT A DIE!");
                Squachy::visitPose(now, 800, Squachy::VisitPose::STARTLED);
            }
            const uint32_t dt = now - s_mimLast > 100 ? 100 : now - s_mimLast;
            s_mimLast = now;
            float open = 0.5f + 0.5f * sinf((float)m / 75.0f);    // chomp, chomp
            int hop = 0;
            int r = m - 700 < 150 ? s_freeR + (int)((m - 700) * (R - s_freeR) / 150) : R;
            if (!s_mimBiteAt) {
                if (m >= 1700) {
                    // After him. He bolts for the far wall; it is quicker.
                    const int side = s_mimX >= (float)cx ? 1 : -1;
                    if (cue(1, s_mimWake + 1700, now)) {
                        Squachy::flick((int8_t)-side);
                        Squachy::announce("NOPE. NOPE. NOPE.");      // over the flick's own line
                    }
                    const float tx = (float)(cx + side * (hw * 55 / 100 + R));
                    const float step = 0.30f * u * (float)dt;
                    if (fabsf(tx - s_mimX) <= step) s_mimX = tx; else s_mimX += tx > s_mimX ? step : -step;
                    s_mimFace = (int8_t)(tx < s_mimX ? -1 : tx > s_mimX ? 1 : -side);
                    hop = (int)(fabsf(sinf((float)m / 70.0f)) * 9.0f * u);
                    if ((fabsf(tx - s_mimX) <= 3.0f * u && m >= 2100) || m > 7000) {
                        s_mimBiteAt = now ? now : 1;
                        s_mimFace = (int8_t)-side;
                        Squachy::toss((int8_t)(cx < w / 2 ? 1 : -1));     // clean over its head
                        Squachy::announce("OW! IT BITES!");
                    }
                } else {
                    hop = (int)(fabsf(sinf((float)m / 110.0f)) * 3.0f * u);
                }
            } else {
                const uint32_t since = now - s_mimBiteAt;
                if (since < 1300) {                               // pleased with itself
                    hop  = (int)(fabsf(sinf((float)since / 100.0f)) * 7.0f * u);
                    open = 0.7f + 0.3f * sinf((float)since / 60.0f);
                    if (since < 130) drawBurst(t, (int)s_mimX + s_mimFace * R, floorY - R, since, u);
                } else if (since < 1700) {                        // and a die again, as if nothing had happened
                    open = 0.7f * (1.0f - (float)(since - 1300) / 400.0f);
                    r = R - (int)((since - 1300) * (R - s_freeR) / 400);
                } else {
                    // Off the nearer edge, rolling, with nothing to say for itself.
                    const int out = s_mimX < (float)(w / 2) ? -1 : 1;
                    s_mimX += out * 0.16f * u * (float)dt;
                    if (s_mimX > (float)(-2 * R) && s_mimX < (float)(w + 2 * R))
                        drawD20(t, s_mimX, (float)(floorY - s_freeR), s_freeR, out * (float)(since - 1700) * 0.55f, 0, u);
                    if (since >= 3000 && !(s_encSaid & 8u)) { s_encSaid |= 8u; Squachy::announce("I am not touching that again."); }
                    if (since > 3700) encEnd();
                    return true;
                }
            }
            drawMimic(t, (int)s_mimX, floorY, r, open, s_mimFace, true, hop, 0.0f, u);
            return true;
        }
        case Kind::ICONS: {
            // The last catch, on legs: in from the right, a wait, and off again.
            DetectionType ty = Squachy::lastCaught();
            if (ty == DetectionType::UNKNOWN || (uint8_t)ty >= (uint8_t)DetectionType::COUNT) ty = DetectionType::AIRTAG;
            const int s = 9 * u;
            int x; bool walking;
            if (ms < 2200)      { x = w + 2 * s - (int)((w + 2 * s - w * 82 / 100) * ms / 2200.0f); walking = true; }
            else if (ms < 7800) { x = w * 82 / 100; walking = false; }
            else                { x = w * 82 / 100 + (int)((ms - 7800) * 0.12f * u); walking = true; }
            const int hop = (!walking && (ms / 900) % 3 == 0) ? 2 * u : 0;
            const int cy = floorY - s - 7 * u - hop;
            legs(t, x, cy + s, 7 * u, walking, now, 0x8410);
            Theme::drawTypeIcon(t, ty, x, cy, s);
            eyesAt(t, x, cy - s / 3, 2 * u);
            if (shiny) shinyAt(t, x, cy, s + 9 * u, now, u);
            say(0, 2400, now, "It followed me home.");
            say(1, 5200, now, "Can we keep it?");
            if (ms > 10500) encEnd();
            return true;
        }
        case Kind::ZOOMIES: {
            int px, py;
            if (Pet::zooming(now) && Pet::drawnAt(px, py)) {
                // Dust where it is, so the speed reads on a still frame too.
                for (int i = 0; i < 3; i++)
                    t.fillCircle(px - 10 * u + i * 10 * u + (int)((now / 60 + i) % 3) * u, floorY - 2 * u, (1 + (now / 90 + i) % 2) * u,
                                 Theme::blend(Theme::BG, Theme::WHITE, 120));
                if (shiny) shinyAt(t, px, py - 8 * u, 16 * u, now, u);
            }
            laugh(0, 800, now);
            say(1, 3000, now, "He's fine. He does this.");
            if (ms > 7500) encEnd();
            return true;
        }
        case Kind::LOST: {
            int x; bool walking; uint8_t bars = 4;
            const uint32_t tap = s_encTapped ? s_encTapAt - s_encAt : 0;
            if (ms < 1700) { x = -20 * u + (int)((w * 18 / 100 + 20 * u) * ms / 1700.0f); walking = true; }
            else           { x = w * 18 / 100; walking = false; }
            if (!s_encTapped) {
                if (ms > 1700) bars = (uint8_t)(ms > 6000 ? 1 : 4 - (ms - 1700) / 1450);
                if (ms > 9500) { bars = 0; x -= (int)((ms - 9500) * 0.05f * u); walking = true; }
                if (ms >= 1700 && ms < 9500) { s_encHitX = x; s_encHitY = floorY - 20 * u; s_encHitR = 26 * u; }
                if (ms >= 3500 && ms < 9500 && (ms / 400) % 2) {
                    t.setTextFont(1); t.setTextSize(u + 1); t.setTextColor(Theme::AMBER);
                    t.setCursor(x + 14 * u, floorY - 48 * u); t.print("?"); t.setTextSize(1);
                }
                say(3, 9600, now, "Bye, little signal.");
                if (ms > 12500) encEnd();
            } else {
                const uint32_t since = ms - tap;
                if (since == 0 || !(s_encSaid & 1u)) say(0, tap, now, "Home's that way!");
                bars = (uint8_t)(since < 1000 ? 1 + since / 330 : 4);
                if (since > 1600) { x -= (int)((since - 1600) * 0.09f * u); walking = true; }
                if (since > 1000 && since < 2600) {
                    t.setTextFont(1); t.setTextSize(u + 1); t.setTextColor(Theme::GREEN);
                    t.setCursor(x - 12 * u, floorY - 48 * u - (int)(since - 1000) / 80); t.print("HOME!"); t.setTextSize(1);
                }
                laugh(1, tap + 1200, now);
                if (since > 4200) encEnd();
            }
            if (s_encK == Kind::LOST) drawLostSignal(t, x, floorY, u, bars, walking, now);
            if (shiny && s_encK == Kind::LOST) shinyAt(t, x, floorY - 20 * u, 22 * u, now, u);
            return true;
        }
        case Kind::ABDUCT: {
            // The UFO comes for the last catch, takes it, thinks better of it.
            DetectionType ty = Squachy::lastCaught();
            if (ty == DetectionType::UNKNOWN || (uint8_t)ty >= (uint8_t)DetectionType::COUNT) ty = DetectionType::AIRTAG;
            const int tx = w * 82 / 100, s = 9 * u;
            const int uy = bandTop + 18 * u;
            int ux = tx;
            if (ms < 2200) ux = -30 * u + (int)((tx + 30 * u) * ms / 2200.0f);
            else if (ms > 8600) ux = tx + (int)((ms - 8600) * 0.25f * u);
            const int ground = floorY - s - 4 * u, top = uy + 8 * u;
            int iy = ground;
            if (ms >= 3000 && ms < 5400) iy = ground - (ground - top) * (int)(ms - 3000) / 2400;
            else if (ms >= 5400 && ms < 6400) iy = -1000;                  // inside
            else if (ms >= 6400 && ms < 8200) iy = top + (ground - top) * (int)(ms - 6400) / 1800;
            if (ms >= 2600 && ms < 8200) drawBeam(t, ux, top, floorY, u, now);
            if (iy > -1000) Theme::drawTypeIcon(t, ty, tx + (ms >= 3000 && ms < 8200 ? (int)(2 * u * sinf(ms / 200.0f)) : 0), iy, s);
            drawUfo(t, ux, uy, u, now);
            if (shiny) shinyAt(t, ux, uy, 24 * u, now, u);
            say(0, 3300, now, "Hey! Put that back!");
            say(1, 8400, now, "Thank you.");
            if (ms > 10500) encEnd();
            return true;
        }
        case Kind::TWIN:
            // The last two seconds: he comes apart.
            if (ms > 32000 && ms < 34000 && s_encPeerOn) {
                for (int i = 0; i < 6 + (int)(ms - 32000) / 150; i++) {
                    const int y = bandTop + (int)random(0, floorY - bandTop);
                    const uint16_t c = random(0, 2) ? Theme::VAPOR_PINK : Theme::CYAN;
                    t.fillRect(w / 2 + (int)random(-10, 20), y, w / 2, (int)random(1, 4) * u, c);
                }
            }
            return false;
        case Kind::ELDER:
            // The photo: a flash, then the polaroid until he has gone.
            if (s_encTapped) {
                const uint32_t since = now - s_encTapAt;
                if (since < 140) flash(t, w, h);
                else polaroid(t, 6 * u, bandTop + 24 * u, u, now);
            } else if (s_encHitR > 0 && (ms / 500) % 4 == 0) {
                // A hint that he can be tapped: a ring, now and then.
                t.drawCircle(s_encHitX, s_encHitY, s_encHitR - 2 * u, Theme::blend(Theme::BG, Theme::WHITE, 90));
            }
            return true;
        default:
            return drawn(s_encK);
    }
}
#else
bool uiClearEncounterTap(int, int, uint32_t) { return false; }
bool uiClearFreeRoll() { return false; }
void uiClearDiePlayNow() {}
#endif

void uiClearTick(TFT_eSPI& t, uint32_t now, const DetectionEngine& eng, bool advance, bool scanMenu) {
    int w = t.width();
    int h = t.height();
    // This boot's only: the rows the black box brought back were last boot's.
    {
        uint8_t n = 0;
        for (uint8_t i = 0; i < eng.logCount(); i++) {
            const Detection* d = eng.logAt(i);
            if (d && !d->restored) n++;
        }
        s_hitsThisBoot = n;
    }
    // Recomputed every tick, not cached per-board: rotating the screen
    // changes w/h live, and the counter layout should follow it rather
    // than staying stuck at whatever orientation was active at boot.
    bool landscape = w > h;
    // Two rows in landscape on every board, the 3.5" included. Stepping the
    // counters up to size 2 was tried and taken back out: thirteen types is
    // about 77 characters, which is 924 px at size 2 against a 480 px row, so
    // bigger digits could only be paid for with four rows instead of two --
    // and four bars of counters under the mascot is not what that screen is
    // for. The counters stay small and stay two lines.
    const uint8_t counterRows = landscape ? COUNTER_ROWS_LANDSCAPE : COUNTER_ROWS_PORTRAIT;

    Theme::ButtonBarGeom bar = Theme::computeButtonBar(w, h);
    // FOB MODE: no buttons, so the bar's rows belong to the scene. Pushing it
    // off the bottom moves everything that stands on it down to the floor.
    if (s_fob) bar.y = h + 2;
    const int lineH          = 14;
    const int countersTop    = bar.y - counterRows * lineH - 6;
    // The counter rows alone sit 9px lower than countersTop, and nothing
    // else does. Measured off a rendered landscape frame before any of
    // this: the headline's ink ended at row 172, the two counter rows ran
    // 180-186 and 194-200, and the button bar started at 214 -- so the
    // block sat 7px under the headline and 13px above the buttons, hugging
    // the text above it.
    //
    // Centring it in that slack was the first attempt and it was the wrong
    // one: 10px above and 10px below is balanced, and reads as belonging to
    // neither the headline nor the buttons. Low is better. The counters are
    // chrome, the same as the buttons are, so grouping the two into one
    // footer and leaving the headline up with Squachy says what goes with
    // what. 9 puts the last row's ink 4px off the button bar.
    //
    // Not further: 13 would touch the buttons. (The rows also have to land
    // inside the background's repaint or they smear; that runs to the bottom
    // of the screen now, so it no longer sets the limit.)
    //
    // Deliberately NOT folded into countersTop, which would look like the
    // tidier fix. That value is the floor of everything above it: the
    // headline is bottom-aligned to it, Squachy sizes himself against it,
    // the pet takes it as its band, and the background repaints to it.
    // Moving it would slide the headline down with the rows (leaving the
    // grouping exactly as muddled as before) and hand Squachy nine more
    // pixels of height -- which he cannot take: his waving arm already
    // reaches row 4, and growing him walks the hand off the top edge.
    //
    // Orientation-independent by construction. The last row's ink lands at
    // bar.y - 14 + this whatever counterRows is, so portrait's four rows
    // clear the buttons by the same 4px landscape's two do.
    const int counterTextTop = countersTop + 9;


    // ...which frees the rows it used to sit in, and Squachy takes them.
    //
    // His floor was countersTop, a number that stopped meaning anything to
    // him once the counters moved down into the footer: it is neither where
    // the text starts nor where the screen runs out. Two rows above the
    // counter ink is the real bottom of the space he has.
    //
    // He is sized from the band he is handed -- scale = charAvail /
    // BASE_HEIGHT -- so this is the whole of "make him bigger", and it is
    // bigger everywhere rather than per costume. His feet land on the new
    // floor for free; nothing else needs moving.
    //
    // The ceiling on this is his WAVING ARM, not his head. It reaches about
    // two rows above his crest scaled, and the crest is only 8 rows off the
    // top today, so there is far less room up there than the empty-looking
    // rows suggest. See the measurement in the commit that added this.
    // Where the pets stand: the line Squachy's feet used to land on, kept
    // when he moved down onto the buttons (asked for 2026-10-04).
    const int petFloor = counterTextTop - 2;

    const int titleBottom  = 16;

    // The counter tiles moved to a strip under the title bar, as many rows
    // tall as the most there can ever be, so Squachy's size never changes
    // as they come and go -- and he stands on the button bar's top line.
    // CLASSIC keeps it all where it was before v1.31.0: his band from the
    // title bar to the counters, which are at the bottom, with NEARBY over
    // his feet.
    const bool xp = Settings::detXp();
    // XP's tiles start a couple of rows from the very top, in the title
    // bar between its corner icons, the way a taskbar sits. Not when the
    // WATCHING / HUNTING pill is up -- that lives in the same middle of the
    // bar, so they drop under it, and Squachy gives up those rows while it
    // is set -- and never on the T-Watch, whose clock and badges are there.
    // The T-Deck's LORA pill lives in that middle too, whenever the radio is
    // on, so the tiles sit under it then (asked for 2026-10-13, always-on
    // 2026-10-15).
#if defined(TWATCH_S3)
    const bool tilesLow = true;
#else
    const bool tilesLow = eng.watchKind() != DetectionEngine::WatchKind::NONE ||
                          eng.huntKind()  != DetectionEngine::WatchKind::NONE
#if SQUACH_LORA && defined(TDECK_PLUS)
                          || Settings::loraListen() != 0   // the LORA pill is up whenever the radio is on (2026-10-15)
#endif
                          ;
#endif
    const int tilesTop      = tilesLow ? titleBottom + 2 : 2;
    // The 135-pixel boards may borrow a third row on a crowded day (see the
    // fit below); Squachy gives up that row's height only while it is used.
    static uint8_t s_tileRowsLast = 0;      // rows the tiles took last frame
    // ...or rather they did: their names took three rows of a 135-row screen
    // and left Squachy a sliver under his own bubble. There the tiles are
    // the colour and the count alone, in ONE row (asked for 2026-10-10);
    // selecting one still opens what it counts, which is where the name is.
    const bool miniTiles    = Theme::compact();
    // The tiles' own row height: twice the counters' on the large panels,
    // where every tile is drawn at double size (drawXpTile's k).
    // FONTS > XP TILES moves it between the two (Theme::xpTileScale()). Made
    // BIGGER than the panel draws them, they may take one more row; a day
    // too crowded for even that draws them at the panel's own size until
    // there are fewer kinds again (the fit below sets s_bigTilesFrom).
    static uint8_t s_bigTilesFrom = 0;      // kinds that did not fit big; 0 = none yet
    static int     s_bigTilesW = 0;         // ...on a panel this wide
    static uint8_t s_tileKindsLast = 0;     // kinds the tiles showed last frame
    const int tileNative    = Theme::large() ? 2 : 1;
    if (s_bigTilesW != w) { s_bigTilesFrom = 0; s_bigTilesW = w; }
    int tileWant            = Theme::xpTileScale();
    if (tileWant > tileNative && s_bigTilesFrom && s_tileKindsLast >= s_bigTilesFrom) tileWant = tileNative;
    const int tileK         = tileWant;
    const uint8_t extraRows = (!miniTiles && tileK > tileNative) ? 1 : 0;
    const uint8_t rowsKept  = miniTiles ? 1
                            : (extraRows && s_tileRowsLast > counterRows) ? s_tileRowsLast : counterRows;
    const int tileLineH     = lineH * tileK;
    const int bandTop       = xp ? tilesTop + rowsKept * tileLineH : titleBottom;
    const int squachyBottom = xp ? bar.y - 1 : counterTextTop - 2;
    // A panel far taller than it is wide -- the phone app's emulator, shaped
    // like the phone; no board is -- would hand him a band twice his usual
    // height, and he grows to fill whatever he is given, so he came out
    // enormous with his feet sunk behind the buttons. Above 8:5 his band stops
    // at nine tenths of the panel's width and the sky has the rest. Every real
    // panel is under 8:5, so none of this runs on a board.
    // On a panel that tall he also stands a fifth of the way up rather than on
    // the buttons: down there he was a small figure at the foot of a long empty
    // sky; up in the ground band he is standing in the scene. Picked from five
    // renders at 240x533 (sizes 110/90/75% of the width, on the buttons or raised).
    const bool tallPanel    = h * 5 > w * 8;
    const int sqFeet        = tallPanel ? h * 4 / 5 : squachyBottom;
    const int sqCap         = sqFeet - w * 9 / 10;
    const int sqTop         = (tallPanel && sqCap > bandTop) ? sqCap : bandTop;
    // Measured: feet ended 6 rows above the bar; CLASSIC never sank them.
    const int SQ_FOOT_SINK  = xp ? 6 : 0;

    // Background animation, the whole screen top to bottom -- style picked
    // from the settings menu. It used to stop just above the button bar and
    // leave a black strip under it; the buttons fill their own boxes, so the
    // animation runs on around and beneath them instead.
    // The counters get drawn over the band further down, so tell the
    // background where its usable floor really is before it places
    // anything that stands on the ground.
    // Two arguments now, because the counters no longer start where Squachy's
    // feet land. Cameos still stand level with him at countersTop; the
    // backgrounds that fill a bright ground band keep filling to where the
    // numbers actually begin, instead of stopping nine pixels short of them.
    Theme::setBackgroundFloor(squachyBottom, xp ? bar.y : counterTextTop);
    // From the very top of the screen, not from titleBottom. The title bar
    // used to own rows 0-15 and paint them every frame; with it gone they
    // belonged to nobody and kept whatever the previous frame left there.
    // Handing them to the background is also the point of removing the bar:
    // the animation now runs edge to edge behind the two corner buttons.
    //
    // Squachy is still told his band starts at titleBottom. He sizes himself
    // from the space he is given, so telling him about these rows would make
    // him a tenth bigger and move everything hanging off him.
    FrameProf::lap(FrameProf::CHROME);   // the counting and geometry above
    // TERMINAL MODE: the desktop of windows instead of the scene. Everything
    // that would have stood in the scene -- Squachy, the pets, the tiles,
    // the headline -- already stays out in this mode (see below).
    // The Cardputer's built-in screen with the EXT screen on shows neither.
    if (s_infoOnly)                   t.fillRect(0, 0, w, h, Theme::BG);
    else if (Settings::boringMode())  uiTerminalDraw(t, now, eng, titleBottom + 2, bar.y - 2);
    else                              Theme::drawActiveBackground(t, now, 0, h, eng, advance);
    Theme::clearBackgroundFloor();
    // December 1 to 26: snow, lights under the tiles, a drift at his feet.
    // Over the scene only, not TERMINAL MODE or the info-only screen.
    const bool december = !s_infoOnly && !Settings::boringMode() && December::active(now);
    if (december) December::back(t, now, w, bandTop, squachyBottom + 1);
    FrameProf::lap(FrameProf::BG);
    // The battery lamp, under everything that moves for the same reason as
    // the corner clock: a speech bubble that reaches up there covers it.
    drawBatteryGauge(t, now);
#if defined(TWATCH_S3)
    // Under everything that moves: see drawCornerClock().
    if (DrawBand::has(0, titleBottom)) s_cornerClockPillR = drawCornerClock(t, w);
    twatchGpsBadge(t);   // under the bubbles too
#if SQUACH_LORA
    twatchLoraBadge(t);
#endif
#endif
#if defined(CYD_GPS)
    if (DrawBand::has(0, titleBottom)) s_cornerClockPillR = drawGpsLabel(t, w);
#endif
#if SQUACH_LORA && defined(TDECK_PLUS)
    twatchLoraBadge(t);   // LORA n NEW, under the GPS label, under the bubbles
#endif
    // Everything from here that moves by the call, not by the clock, moves
    // on the mascot's clock. See uiMascotStep().
    const bool step = uiMascotStep(now, advance);
#if SQUACH_MESH
    // A random encounter: the dice, and whatever stands behind him.
    encounterBack(t, now, eng, w, h, bandTop, squachyBottom,
                  !Settings::boringMode() && !s_infoOnly && !Squachy::onboardingActive() && !s_crowdDrawn);
#endif
    // His bubble rises past the tiles to row 1, where it always sat. Undone
    // straight after he is drawn: the desk and the alert cameo want theirs.
    // XP: his bubble -- and a visitor's -- rises to just under last frame's
    // tiles, or to the top row when there are none: never onto them, on any
    // panel (asked for 2026-10-13; the large panels used to rise over the
    // tiles and hide the ones underneath). The pets' balloons keep under the
    // same edge (Pet::tick's band top).
    if (xp) Squachy::setBubbleRiseTo(s_tileRowsLast ? tilesTop + s_tileRowsLast * tileLineH + 1 : 1);

    // Squachy: main character, reacts to events, cracks jokes when idle.
    // His available region runs all the way to countersTop (not
    // statusTop) — past where the ALL CLEAR text sits — so he scales up
    // further and his feet land on top of its upper portion. The text
    // itself draws AFTER him (below) so it stays fully legible on top
    // of his body wherever they overlap, instead of being covered. A
    // big bounce can push his dirty-rect clear a few px above
    // titleBottom into the title bar's row, so the title bar is drawn
    // after him too — it fully repaints its own row every frame, so it
    // always ends up on top and never shows any bleed-over from his
    // clear box.
    //
    // "Boring mode" skips this call entirely — his internal state
    // (mood/quip timers, the stats Squachy::trigger() tracks for the
    // Diary screen) keeps updating regardless since that's driven from
    // main.cpp's trigger() calls, not from here; this only turns off
    // his actual on-screen presence. The background above already
    // fully repaints this whole region every frame, so skipping him
    // just leaves it as animated negative space — no layout changes
    // needed anywhere else on this screen.
#if CROWD_BENCH
    // A test build: the crowd benchmark stands in for the Squachys while it
    // runs, and the visit machine sits it out.
    if (CrowdBench::active() && !Settings::boringMode() && !s_infoOnly)
        CrowdBench::draw(t, now, sqTop, squachyBottom);
    else
#endif
    if (!Settings::boringMode() && !s_infoOnly) {
        // The last argument is the SIZE row in Settings. CLEAR is the only
        // screen that passes it: everywhere else he is a cameo in a box
        // somebody sized deliberately, and shrinking him there would just
        // leave a hole.
#if SQUACH_MESH
#if SQUACH_MESH_DEMO
        demoTick(now);
#endif
        // SquachMesh: when a peer is visiting, BOTH Squachys drop to SMALL and
        // stand apart. Small is not a courtesy to the guest, it is what makes
        // two of them fit at all -- at the default size he is already most of
        // the usable height, and two of those would overlap before either had
        // room to stand.
        //
        // The host keeps the left. A visitor arriving on the right is the
        // reading order and it means the resident does not appear to move
        // aside for a stranger.
        visitTick(now);
        // The hosted COPY, refreshed for the *same* visitor's outfit/shades/
        // name changes but never swapped for a different MAC mid-visit.
        // It stays intact through LEAVING even after radio presence is lost.
        const SquachMesh::Peer* guest = visitHosting();
        // A message in the air replaces his scripted line and his nameplate
        // while it is up -- it carries the sender's name itself, in red.
        const bool msgFresh = messageShowing(now) || tutorReply();
        s_msgGuestOn = false;
        s_badgeOn    = false;
        s_crowdN     = 0;
        s_crowdDrawn = false;
        // ---- the crowd ------------------------------------------------------
        // With CROWD set past one, everybody in range is on screen at once and
        // NOBODY is standing on the floor: they drift about their own patch of
        // it, ours included. They shrink to fit, which is what makes it cheap
        // -- measured on hardware, eight at the size they land on cost 17 ms a
        // frame against 29 for today's two at full size.
        //
        // The visit machine still runs underneath: whoever is visiting keeps
        // his conversation and his bubble. What stands down is the part that
        // needs two Squachys at fixed marks on the ground -- the set pieces
        // and the emotes -- because there is no ground here to stand on.
        const uint8_t crowdMax = Settings::meshCrowd();
        Mesh::SquadMember crowd[8];
        uint8_t crowdN = 0;
        // The cap is PEERS, and the setting counts bodies: we are the one it
        // does not have to ask the radio about. Asking for `crowdMax` of them
        // and then drawing ourselves as well would put nine on an UP TO 8.
        const uint8_t peerCap = (uint8_t)((crowdMax > 8 ? 8 : crowdMax) - 1);
        if (crowdMax > 1) crowdN = Mesh::squadList(now, crowd, peerCap);
        if (crowdMax > 1 && crowdN >= 2) {
            uiClearDrawCrowd(t, now, crowd, crowdN, sqTop, squachyBottom, step, msgFresh, 22,
                             crowdGrow(t, (uint8_t)(crowdN + 1)));
        } else if (guest) {
            drawVisit(t, now, guest, sqTop, squachyBottom, step, msgFresh);
        } else
#endif
        // His band runs a few rows INTO the bar: the bottom of it is where
        // his shadow goes, under his feet, and the bar paints over that --
        // so his soles, not his shadow, land on the bar's top line.
        {
            Squachy::setSizeBoost(soloBoost(t, Settings::squachySizePct()));
#if defined(SQW_SMALL)
            // At 135 rows he fills his band: a bubble at its top sat on his
            // face. It hangs from his feet instead.
            Squachy::setBubbleSinkTo(sqFeet);
#endif
            Squachy::tick(t, w / 2, sqTop, sqFeet + SQ_FOOT_SINK - sqTop, now, step,
                          1.0f, false, -1, Settings::squachySizePct());
            Squachy::setBubbleSinkTo(-1);
            Squachy::setSizeBoost(1.0f);
        }
        Squachy::setBubbleRiseTo(-1);
#if SQUACH_MESH
        // Everybody in range, whether or not one of them is on screen.
        {
            const uint8_t squad = Mesh::squadCount(now);
            if (squad >= 1) drawSquadBadge(t, w - 4, xp ? squachyBottom - 1 : counterTextTop - 2, squad);
        }
        drawMessageUi(t, now, bandTop, squachyBottom);
#endif
    }


    // Rare decorative flourishes (UFO/sparkle/critter/glitch-line) --
    // see idle_events.h. Skipped for the same reasons Squachy's own
    // presence is skipped above (boring mode) or would collide with
    // the walkthrough's own bubble (onboarding) -- a UFO flying past
    // mid-lesson would be more distraction than delight.
    FrameProf::lap(FrameProf::SQUACHY);
    Squachy::setBubbleRiseTo(-1);   // boring mode skips the one above
    if (!Settings::boringMode() && !s_infoOnly && !Squachy::onboardingActive()) {
#if SQUACH_MESH
        // An encounter takes the idle events' place while it plays.
        if (!encounterFront(t, now, eng, w, h, bandTop, squachyBottom))
#endif
        IdleEvents::tick(t, now, 0, bandTop, w, squachyBottom, step);
    }
    FrameProf::lap(FrameProf::IDLE);
    if (december) December::front(t, now, w, bandTop, squachyBottom + 1);

    // Whatever the background wants on top of the mascot. Right now
    // that is the werewolf's speech bubble: drawFire computes it but
    // deliberately does not paint it, because the background is drawn
    // before Squachy and the bubble was ending up behind him. Same
    // reasoning as ALL CLEAR below -- text is the one thing here that
    // cannot afford to be half-covered.
    Theme::drawBackgroundOverlay(t, now);

    // Title bar at the top
    if (DrawBand::has(0, titleBottom) && !s_fob) Theme::drawTitleBar(t, ">> SQUACHWATCH <<  SCANNING");

    // The watch/hunt indicator, in the title bar's empty middle. AFTER the bar
    // itself, which repaints that whole band -- see drawWatchPill()'s comment
    // for why the first attempt at this was invisible. Drawn in every mode,
    // whenever either target is set.
    {
        const bool watching = eng.watchKind() != DetectionEngine::WatchKind::NONE;
        const bool hunting  = eng.huntKind()  != DetectionEngine::WatchKind::NONE;
        // The flag goes inside the guard with the drawing it describes. Left
        // outside it, the pass that cannot reach the title bar would clear a
        // pill the other pass had just drawn, and it would stop being tappable.
        if (DrawBand::has(0, titleBottom)) {
            s_watchPillOn = false;
            // Right of the pill: the watch's corner clock, drawn earlier (see
            // drawCornerClock()); -1 elsewhere, the old fixed reserve.
            if (watching || hunting) drawWatchPill(t, w, watching, hunting, s_cornerClockPillR);
        }
    }

    // Is anything actually live right now? Not lifetime -- a camera seen an
    // hour ago is not something happening, and the counters decay for the
    // same reason.
    FrameProf::lap(FrameProf::CHROME);
    bool anyActive = false;
    for (uint8_t i = 0; i < (uint8_t)DetectionType::COUNT; i++) {
        if (eng.countByType((DetectionType)i) > 0) { anyActive = true; break; }
    }
    // ...and not only things you told it to ignore or snooze. Your own AirTag
    // and your own doorbell are counted -- the counters stay honest, as
    // IgnoreList promises -- but they are not news, and a headline that never
    // goes away because of them is one you stop reading (asked for
    // 2026-10-03, IGNORE first, then SNOOZE too: silenced() is both). A
    // deauth flood is an attack with no device of its own to ignore, so it
    // always counts. Looked up four times a second, not every frame: the log
    // is 64 rows, the ignore list 64 MACs and the snoozes 32.
    // The same pass says whether any of it is the serious kind -- trackers,
    // police kit, skimmers, attack tools -- which TH3 0N3's lenses run red
    // for. Doorbells and glasses are news, not Agents.
    // The same pass marks each type that has a device in range you have NOT
    // silenced: its tile is lit, and a tile whose only devices are your own
    // ignored ones is drawn dim.
    static bool s_serious = false;
    static uint32_t s_litMask = 0;
    if (anyActive) {
        static uint32_t s_unAt = 0;
        static bool     s_un   = true;
        if (!s_unAt || now - s_unAt >= 250) {
            s_unAt = now ? now : 1;
            s_un = eng.countByType(DetectionType::DEAUTH) > 0;
            s_serious = s_un;
            s_litMask = s_un ? (1u << (uint8_t)DetectionType::DEAUTH) : 0u;
            for (uint8_t i = 0; i < eng.logCount(); i++) {
                const Detection* d = eng.logAt(i);
                if (!d || !d->active || IgnoreList::silenced(d->mac)) continue;
                s_un = true;
                if ((uint8_t)d->type < 32) s_litMask |= 1u << (uint8_t)d->type;
                switch (d->type) {
                    case DetectionType::FLOCK:  case DetectionType::AXON:   case DetectionType::SKIMMER:
                    case DetectionType::RAVEN:  case DetectionType::ALPR:   case DetectionType::AIRTAG:
                    case DetectionType::SAMSUNG_TAG: case DetectionType::GOOGLE_TAG: case DetectionType::TILE:
                    case DetectionType::EVILTWIN:    case DetectionType::HACKER:
                    case DetectionType::MINE:
                        s_serious = true; break;
                    default: break;
                }
            }
        }
        anyActive = s_un;
    } else {
        s_serious = false;
        s_litMask = 0;
    }
    // Its ARRIVAL is the event, so the glitch fires on the edge rather than
    // on the state -- once, when the screen goes from nothing to something,
    // not again when a second detection joins the first. Level 3 is where
    // the shared burst adds a full-screen tear on top of the jitter and
    // dropout, which is the point: the headline does not fade in, it cuts
    // in badly, the way a signal does.
    //
    // triggerGlitchBurst drives the same burst drawBangersText already reads
    // from, so the headline glitches on its own with nothing else wired up.
    static bool s_wasActive = false;
    if (anyActive && !s_wasActive) Theme::triggerGlitchBurst(3);
    s_wasActive = anyActive;
    Squachy::setNearbyLit(anyActive && s_serious);

    // XP has no headline: the counter tiles say the same thing and say WHAT,
    // and nothing is left painted over Squachy or the pet. CLASSIC keeps the
    // rainbow NEARBY cutting in over his feet whenever anything is live.
    s_nearbyOn = false;           // set again below only if it is drawn
    // LG, measured off a render: the painted band runs ty+4 to ty+32 at this
    // size, so 33 puts its last row HEADLINE_PAD above the counter text.
    static const Theme::BangersSize HEADLINE_SIZE = Theme::BangersSize::LG;
    static const int HEADLINE_H   = 33;
    static const int HEADLINE_PAD = 5;
    const int headlineTop = counterTextTop - HEADLINE_PAD - HEADLINE_H;
    if (!xp && !Settings::boringMode())
    if (anyActive && t.height() >= 200) {
        // The rainbow ALL CLEAR used to own. It was the good part and it was
        // wasted on the state you see least; now it runs on the state that
        // actually matters. Same hue-wash as Squachy's party-mode confetti.
        static const uint16_t RAINBOW[6] = {
            Theme::RED, Theme::AMBER, Theme::GREEN,
            Theme::CYAN, Theme::VAPOR_PURPLE, Theme::PINK
        };
        const float huePos = fmodf((float)now / 900.0f, 6.0f);
        const int i0 = (int)huePos % 6, i1 = (i0 + 1) % 6;
        const uint16_t col =
            Theme::blend(RAINBOW[i0], RAINBOW[i1], (uint16_t)((huePos - (int)huePos) * 255));
        // Not a status label any more. This row now appears BECAUSE an
        // event happened, so it reads as the event rather than describing
        // the screen's state -- the job a label like ACTIVE DETECTIONS was
        // doing twice, and less precisely than the counters underneath.
        //
        // One word because the subject was the vague half. SOMETHING'S
        // NEARBY said nothing the counters do not say better; NEARBY is the
        // half that carries the meaning, and dropping the other one is what
        // buys the bigger face above.
        const char* msg = "NEARBY";
        // 2px black outline: draw the same text at every offset in a
        // 5x5 grid around the real position (minus the center) in
        // black first, then the real color on top. A full grid, not
        // just a ring at radius 2, so there's no gap between the 1px
        // and 2px shells. The Bangers glyph renderer only paints ink
        // pixels (not a full opaque cell), so the offset passes land
        // as a clean outline rather than clobbering each other.
        static const int8_t OUTLINE_OFS[24][2] = {
            {-2,-2},{-1,-2},{0,-2},{1,-2},{2,-2},
            {-2,-1},{-1,-1},{0,-1},{1,-1},{2,-1},
            {-2, 0},{-1, 0},        {1, 0},{2, 0},
            {-2, 1},{-1, 1},{0, 1},{1, 1},{2, 1},
            {-2, 2},{-1, 2},{0, 2},{1, 2},{2, 2},
        };
        int tw = Theme::bangersTextWidth(msg, HEADLINE_SIZE);
        int ty = headlineTop;
        if (tw <= w - 8) {
            int tx = (w - tw) / 2;
            // One pass, not twenty-four: this was 14.8 ms of every frame.
            if (DrawBand::has(ty, ty + HEADLINE_H)) {
                Theme::drawBangersOutline(t, tx, ty, msg, Theme::BLACK, HEADLINE_SIZE, 2);
                Theme::drawBangersText(t, tx, ty, msg, col, HEADLINE_SIZE);
            }
            // Padded well past the ink: the word is only 33px tall and it is
            // pressed with a thumb, over a moving Squachy.
            s_nbX = (int16_t)(tx - 16); s_nbY = (int16_t)(ty - 10);
            s_nbW = (int16_t)(tw + 32); s_nbH = (int16_t)(HEADLINE_H + 20);
            s_nearbyOn = true;
        } else {
            // Kept as a guard, not because the current headline needs it:
            // NEARBY measures 90px in LG against the 232 a 240px portrait
            // screen leaves, so it clears by a factor of two and a half.
            // Bangers has no step below MD to fall back to the way the
            // built-in font does, so any future headline that outgrows the
            // narrow rotation drops to the built-in face rather than clip.
            t.setTextSize(2);
            int sw = t.textWidth(msg);
            int sx = (w - sw) / 2, sy = counterTextTop - HEADLINE_PAD - t.fontHeight(2);
            t.setTextColor(Theme::BLACK, Theme::BG);
            for (uint8_t i = 0; i < 24; i++) {
                t.setCursor(sx + OUTLINE_OFS[i][0], sy + OUTLINE_OFS[i][1]);
                t.print(msg);
            }
            t.setTextColor(col, Theme::BG);
            t.setCursor(sx, sy);
            t.print(msg);
            s_nbX = (int16_t)(sx - 16); s_nbY = (int16_t)(sy - 10);
            s_nbW = (int16_t)(sw + 32); s_nbH = (int16_t)(t.fontHeight() + 20);
            s_nearbyOn = true;
        }
    }
    FrameProf::lap(FrameProf::HEADLINE);

    // The pet, after Squachy AND after the headline.
    //
    // After Squachy because he perches on top of him. After the headline
    // because he spends most of his visits on the ground, and the ground on
    // this screen is the same rows the headline occupies -- drawn before it
    // he stood there for three seconds with his legs behind ACTIVE
    // DETECTIONS, which is a poor showing for the only other character on
    // the device.
    //
    // Squachy stays behind the headline on purpose and this does not change
    // that: he is 130px of opaque brown and the text has to survive him. The
    // pet is thirty pixels wide and moving, so passing in front reads as
    // depth rather than as an obstruction.
    //
    // Drawn after the counters now (see below): with DETECTIONS on XP the
    // counter buttons sit along the top, and C1iPPY's balloon has to land on
    // top of them rather than under them.

    // Counter lines above the buttons — all 13 detection types, split
    // across counterRows (2 in landscape, capped at 4/row in portrait
    // -- see the constants above) so a row never runs wide enough to
    // clip off a narrow portrait screen, while landscape still gets
    // the more compact two-row layout it has room for. Whole
    // label:count tokens only, so nothing ever breaks mid-word. No
    // flat clear here anymore — the background animation now fully
    // repaints this whole row every frame (it runs to the bottom of the
    // screen), the same "let the background do the erasing"
    // pattern already relied on for Squachy and the status line above.
    t.setTextSize(1);
    t.setTextColor(Theme::CYAN, Theme::BG);
    t.setTextWrap(false);

    // Evenly balanced, not greedily packed (e.g. 4/4/4/1 in portrait)
    // -- a lone last row with a single item looked worse than several
    // similarly-sized rows does, and this still never exceeds the
    // per-orientation cap on any row.
    DetectionType counterTypes[MAX_COUNTER_TYPES];
    uint8_t counterN = activeCounterTypes(counterTypes);
    // TERMINAL MODE: COUNTS says this, in its own window.
    if (Settings::boringMode()) {
        s_tileN = 0;
        s_tileRowsLast = 0;
    } else if (!xp) {
        // CLASSIC: every type, zeroes and all, in balanced rows -- except on
        // a screen as small as the StickS3's, where there is no room for a
        // row of zeroes and only the types in range are shown.
        s_tileN = 0;
        if (t.height() < 200) {
            uint8_t kept = 0;
            for (uint8_t i = 0; i < counterN; i++)
                if (counterCount(eng, counterTypes[i]) > 0) counterTypes[kept++] = counterTypes[i];
            counterN = kept;
        }
        uint8_t base      = counterN / counterRows;
        uint8_t remainder = counterN % counterRows;
        uint8_t start = 0;
        for (uint8_t row = 0; row < counterRows; row++) {
            uint8_t n = base + (row < remainder ? 1 : 0);
            const int rowY = counterTextTop + row * lineH;
            // `start` advances either way: the rows share one list of types,
            // so a row that is not painted still hands the next one its place.
            if (DrawBand::has(rowY, rowY + lineH))
                drawCounterLine(t, w, rowY, eng, counterTypes + start, n);
            start += n;
        }
    } else {
        // Only the types with something in range, and none at all when it is
        // quiet: a row of zeroes was most of what this screen showed, and the
        // ones that matter were lost in it.
        {
            uint8_t kept = 0;
            for (uint8_t i = 0; i < counterN; i++)
                if (counterCount(eng, counterTypes[i]) > 0) counterTypes[kept++] = counterTypes[i];
            counterN = kept;
        }
        // As few rows as they fit in, sitting on the buttons; the space for the
        // most rows is still kept, so Squachy never changes size as things come
        // and go.
        const uint8_t perRowCap = landscape ? MAX_COUNTER_TYPES : MAX_PER_ROW_PORTRAIT;
        uint16_t tileW[MAX_COUNTER_TYPES];
        char     tileTxt[MAX_COUNTER_TYPES][14];
        for (uint8_t i = 0; i < counterN; i++) {
            if (miniTiles) snprintf(tileTxt[i], sizeof(tileTxt[i]), "%u", counterCount(eng, counterTypes[i]));
            else           snprintf(tileTxt[i], sizeof(tileTxt[i]), "%s %u", counterLabel(counterTypes[i]),
                                    counterCount(eng, counterTypes[i]));
            tileW[i] = (uint16_t)(((int)strlen(tileTxt[i]) * XP_ADV + 10 + XP_ICON_W) * tileK);
        }
        int tileGap = 4 * tileK, tilePad = 5 * tileK;
        uint8_t rowsUsed = 0;
        // A row that reaches into the title bar's icon band keeps between the
        // corner icons: the menu on the left, rotate (and the lock, when the
        // PIN is on) on the right. Centred, so the same margin both sides.
        const int iconMargin = (Theme::TITLE_ICON_W + (Security::enabled() ? 26 : 0) > Theme::TITLE_ICON_W
                                    ? Theme::TITLE_ICON_W + (Security::enabled() ? 26 : 0)
                                    : Theme::TITLE_ICON_W) + 3
#if defined(CYD_GPS)
                               + (gpsLabelShown() ? gpsLabelW() + 2 : 0)   // the GPS label, left of the icons
#endif
                               ;
        auto rowRoom = [&](uint8_t row) {
            const int top = tilesTop + row * tileLineH;
            return top < Theme::TITLE_ICON_BAND_H ? w - 2 * iconMargin : w - 8;
        };
        // Tighter tiles, once, if a crowded day will not fit the rows there are.
        for (uint8_t pass = 0; pass < 2 && counterN && !rowsUsed; pass++) {
        if (pass == 1) {
            tileGap = 2 * tileK; tilePad = 2 * tileK;
            for (uint8_t i = 0; i < counterN; i++) tileW[i] -= 6 * tileK;
        }
        for (uint8_t r = 1; r <= (miniTiles ? 1 : counterRows + extraRows); r++) {
            const uint8_t bse = counterN / r, rem = counterN % r;
            bool fits = true;
            uint8_t st = 0;
            for (uint8_t row = 0; row < r && fits; row++) {
                const uint8_t n = bse + (row < rem ? 1 : 0);
                int rw = 0;
                for (uint8_t k = 0; k < n; k++) rw += tileW[st + k] + (k ? tileGap : 0);
                if (n > perRowCap || rw > rowRoom(row)) fits = false;
                st += n;
            }
            if (fits) { rowsUsed = r; break; }
        }
        }
        // One row of mini tiles that still will not fit: as many as do, the
        // last of them saying how many more there are.
        if (miniTiles && counterN && !rowsUsed) {
            rowsUsed = 1;
            uint8_t keep = counterN;
            while (keep > 1) {
                char more[8];
                snprintf(more, sizeof more, "+%u", (unsigned)(counterN - keep + 1));
                const int moreW = (int)strlen(more) * XP_ADV + 10 + XP_ICON_W - 6;
                int rw = moreW;
                for (uint8_t k = 0; k + 1 < keep; k++) rw += tileW[k] + tileGap;
                if (rw <= rowRoom(0)) {
                    snprintf(tileTxt[keep - 1], sizeof tileTxt[keep - 1], "%s", more);
                    tileW[keep - 1] = (uint16_t)moreW;
                    break;
                }
                keep--;
            }
            counterN = keep;
        }
        // Bigger than the panel's own and still no fit: back to its own size
        // from the next frame, for as long as there are this many kinds.
        if (counterN && !rowsUsed && tileK > tileNative) s_bigTilesFrom = counterN;
        else if (s_bigTilesFrom && counterN < s_bigTilesFrom) s_bigTilesFrom = 0;
        s_tileKindsLast = counterN;
        if (counterN && !rowsUsed) rowsUsed = counterRows + extraRows;
        s_tileRowsLast = rowsUsed;
        // A kind that has just turned up flashes orange for a few seconds, the
        // way an XP taskbar button asks for you.
        static uint32_t s_tileSince[(uint8_t)DetectionType::COUNT] = {};
        {
            bool live[(uint8_t)DetectionType::COUNT] = {};
            for (uint8_t i = 0; i < counterN; i++) live[(uint8_t)counterTypes[i]] = true;
            for (uint8_t i = 0; i < (uint8_t)DetectionType::COUNT; i++) {
                if (!live[i]) s_tileSince[i] = 0;
                else if (!s_tileSince[i]) s_tileSince[i] = now ? now : 1;
            }
        }
        s_tileN = 0;
        int bX = 0, bY = 0, bW = 0, bH = 0;
        const bool bub = !Settings::boringMode() && !s_infoOnly && Squachy::ownBubble(bX, bY, bW, bH);
        if (counterN) {
            const uint8_t bse = counterN / rowsUsed, rem = counterN % rowsUsed;
            uint8_t st = 0;
            for (uint8_t row = 0; row < rowsUsed; row++) {
                const uint8_t n = bse + (row < rem ? 1 : 0);
                const int rowY = tilesTop + 2 + row * tileLineH;
                int rw = 0;
                for (uint8_t k = 0; k < n; k++) rw += tileW[st + k] + (k ? tileGap : 0);
                int x = (w - rw) / 2;
                const bool paint = DrawBand::has(rowY - 2, rowY + tileLineH);
                for (uint8_t k = 0; k < n; k++) {
                    const uint8_t i = st + k;
                    const DetectionType ty = counterTypes[i];
                    bool lit = false;
                    for (uint8_t d = 0; d < (uint8_t)DetectionType::COUNT && d < 32; d++)
                        if ((s_litMask & (1u << d)) && uiClearColumnHolds(ty, (DetectionType)d)) { lit = true; break; }
                    const int ty0 = rowY - 2, th = 12 * tileK;
                    // A tile under his speech bubble waits for it to go: the
                    // bubble is the one that has something to say right now.
                    const bool underBubble = bub && x < bX + bW && x + tileW[i] > bX && ty0 < bY + bH && ty0 + th > bY;
                    if (paint && !underBubble) {
                        // Lit is XP blue; just arrived flashes orange; only your
                        // own ignored or snoozed things are a greyed-out slate.
                        const uint32_t since = s_tileSince[(uint8_t)ty];
                        const bool flash = lit && since && now - since < 3000 && ((now - since) / 500) % 2 == 0;
                        // The "+N" tile stands for several kinds: grey, not one of their colours.
                        drawXpTile(t, x, ty0, tileW[i], th, tileTxt[i],
                                   (miniTiles && tileTxt[i][0] == '+') ? (uint16_t)0x8410 : Theme::colorFor(ty),
                                   flash ? 1 : (lit ? 0 : 2), tilePad, tileK);
                    }
                    if (s_tileN < TILE_MAX) s_tiles[s_tileN++] = { (int16_t)x, (int16_t)ty0, (int16_t)tileW[i], (int16_t)th, ty };
                    x += tileW[i] + tileGap;
                }
                st += n;
            }
        }
    }

    // The pet, last of the scene: over the counters, under the buttons.
    Pet::setGround(sqFeet);
    if (!Settings::boringMode() && !s_infoOnly) drawPets(t, now, w, h, bandTop, petFloor);
    if (s_infoOnly) {
        const int top = (s_tileRowsLast ? tilesTop + s_tileRowsLast * tileLineH : 18) + 6;
        int y = top;
        t.setTextFont(1); t.setTextSize(1); t.setTextWrap(false);
        t.setTextColor(Theme::AMBER, Theme::BG);
        t.setCursor(6, y); t.print("SQUACHY IS ON THE BIG SCREEN"); y += 13;
        char line[48];
        snprintf(line, sizeof line, "Seen this boot: %u   In the log: %u",
                 (unsigned)s_hitsThisBoot, (unsigned)eng.logCount());
        t.setTextColor(Theme::WHITE, Theme::BG);
        t.setCursor(6, y); t.print(line); y += 13;
#if defined(CARDPUTER_ADV) && defined(CYD_GPS)
        if (Wardrive::enabled()) {
            char st[24];
            Gnss::status(now, st, sizeof st);
            const bool logging = Wardrive::sdState(now) == Wardrive::SdState::LOGGING;
            if (Wardrive::sdState(now) == Wardrive::SdState::NO_CARD) snprintf(st, sizeof st, "NO CARD");
            snprintf(line, sizeof line, "WARDRIVE %s  rows %lu", st, (unsigned long)Wardrive::written());
            t.setTextColor(logging ? Theme::GREEN : Theme::AMBER, Theme::BG);
            t.setCursor(6, y); t.print(line); y += 13;
        }
#endif
        y += 3;
        static const char* const KEYS[] = {
            "S scan   L log   D desk   B background",
            "E turns the EXT screen off",
            "ESC back   arrows move   ENTER press",
        };
        t.setTextColor(Theme::CYAN, Theme::BG);
        for (const char* k : KEYS) {
            if (y + 8 > bar.y - 3) break;
            t.setCursor(6, y); t.print(k); y += 11;
        }
    }

    // Soft buttons, straight over the background: it repaints the whole
    // strip under them every frame, margins and gaps included. Each
    // button fills its own box, so the labels stay on a dark ground.
    if (DrawBand::has(bar.y, bar.y + bar.h) && !s_fob)
        Theme::drawButtonBar(t, ButtonId::NONE,
                             scanMenu ? Theme::ButtonBarMode::SCAN_PICKER : Theme::ButtonBarMode::MAIN);
#if SQUACH_MESH
    // Last, so it sits over the counters and the buttons -- which it has
    // taken over for as long as it runs; main.cpp routes every tap to it.
    if (MeshTutor::active()) {
        if (MeshTutor::step() == MeshTutor::Step::TAP_ICON && s_bubbleOn) {
            const int ix = s_bubX + 8, iy = s_bubY + 8;
            MeshTutor::drawFrame(t, now, ix, iy, MSG_ICON_W, MSG_ICON_H);
            // From below: above it is where the visitor's lines go.
            MeshTutor::drawArrow(t, now, ix + MSG_ICON_W / 2, iy + MSG_ICON_H + 7,
                                 MeshTutor::Dir::UP);
        }
        MeshTutor::drawCard(t, false);
    }
#endif
}
