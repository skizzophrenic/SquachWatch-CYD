// SquachWatch-CYD — clear (idle) screen implementation
#include "ui_clear.h"
// Needed this early: the message helpers sit up with the visit machine,
// above where the rest of this file pulls these in.
#include "theme.h"
#include "settings.h"
#include "meshtalk.h"
#if SQUACH_MESH
#include "squachmesh.h"
#include "meshtutor.h"
#include "squachy.h"
#include "detection.h"
#include "emote_script.h"
#include "crowd_bench.h"

// A peer supplied from outside -- the emulator's --peer flag today, the radio
// eventually. Always wins over the demo below.
static const SquachMesh::Peer* s_guest = nullptr;
void uiClearSetGuest(const SquachMesh::Peer* p) { s_guest = p; }

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
static uint32_t     s_guestLaughUntil = 0;   // the guest's half of the laugh

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
        if (dt <= 0) return homeX;          // still saying goodbye; stand still
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

static void visitBeat(uint32_t now, Squachy::VisitMoment m) {
    s_beatAt = now;
    s_beatNo++;
    if (m == Squachy::VisitMoment::HANGOUT) {
        // Three beats, not two: question, answer, and the host getting the
        // last word. The strict two-beat alternation was correct dialogue
        // and still read as correspondence rather than company.
        switch (s_hangStep) {
            case 0:                                 // the host asks
                s_guestTurn      = false;
                s_visitGuestLine = nullptr;
                s_beatMs         = Squachy::visitHangHost(s_exchange);
                s_hangStep       = 1;
                break;
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

// ---- napping ----------------------------------------------------------------
// Left alone long enough mid-visit -- no detection, nobody touching him -- the
// two of them doze off together. Anything that would have interrupted a
// conversation wakes them: a tap on him, a detection, a message arriving.
static const uint32_t NAP_AFTER_MS = 240000;
static const uint32_t WAKE_MS      = 1900;      // Squachy's STRETCH_MS: they stretch together
static bool     s_napping = false;
static uint32_t s_napSeen = 0, s_wakeUntil = 0;

static bool messageShowing(uint32_t now);         // with the message UI, below

static Piece nextPiece() {
    const uint8_t n = s_pieceNo++;
    switch (Settings::background()) {
        case Settings::Background::SNOWFALL:  return (n % 2 == 0) ? Piece::SNOW : Piece::RPS;
        case Settings::Background::SYNTHWAVE: return (n % 3 == 2) ? Piece::RPS : Piece::DANCE;
        default:                              return (n % 2 == 0) ? Piece::DANCE : Piece::RPS;
    }
}

static uint32_t pieceGap() {
    const uint32_t g = PIECE_EVERY_MS + (uint32_t)random(0, PIECE_JITTER_MS);
    return Settings::background() == Settings::Background::SYNTHWAVE ? g / 2 : g;
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

static void pieceStart(uint32_t now) { pieceBegin(now, nextPiece(), false); }

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

// ---- napping, continued ------------------------------------------------------
static bool napDue(uint32_t now) {
    // Four minutes of nobody doing anything to him -- and of this visit
    // having gone on that long, so a guest who just arrived is not greeted
    // by two Squachys nodding off.
    return now - Squachy::lastInteractionAt() > NAP_AFTER_MS && now - s_vpAt > NAP_AFTER_MS;
}

static void napStart(uint32_t now) {
    s_napping        = true;
    s_napSeen        = Squachy::lastInteractionAt();
    s_guestTurn      = false;
    s_visitGuestLine = nullptr;
    Squachy::visitNap(now);
    Serial.println("[visit] nap");
}

static void napTick(uint32_t now) {
    if (s_napping) {
        const bool poked = Squachy::lastInteractionAt() != s_napSeen || s_eqN > 0;
        if (!poked && !messageShowing(now)) { Squachy::visitNap(now); return; }
        s_napping        = false;
        s_wakeUntil      = now + WAKE_MS;
        Squachy::visitWake(now);
        s_guestTurn      = true;
        s_visitGuestLine = Squachy::visitWakeLine(s_beatNo);
        Serial.println("[visit] wake");
        return;
    }
    // Stretched out: the conversation picks up where it left off, and the
    // four minutes start again from here.
    if ((int32_t)(s_wakeUntil - now) <= 0) {
        s_wakeUntil      = 0;
        s_visitGuestLine = nullptr;
        s_guestTurn      = false;
        s_beatAt         = now;
        s_beatMs         = 600;
        s_vpAt           = now;
        if (!s_nextPieceAt || (int32_t)(s_nextPieceAt - now) < 30000) s_nextPieceAt = now + 30000;
    }
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
    if (s_napping)                                           return P::SLEEPY;
    if (s_wakeUntil && (int32_t)(s_wakeUntil - now) > 0)     return P::STRETCH;
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
// Two things go wrong without this. A peer whose advert changes mid-visit
// would morph on screen -- outfit and colours swapping on a Squachy standing
// still, which reads as a glitch rather than as anything. And the visitor has
// to keep being drawn through LEAVING after the peer is already gone, which a
// pointer to a slot that has been cleared cannot do.
static SquachMesh::Peer s_hosting{};
static uint32_t         s_hostingId = 0;

// What the screen should actually draw: the copy, for as long as the visit
// lasts, and nothing once it is over.
static const SquachMesh::Peer* visitHosting() {
    return (s_vp == VisitPhase::GONE) ? nullptr : &s_hosting;
}

// Public answer to "who is visiting": the hosted copy, not the raw radio
// state. Anything asking should see the same visitor the screen does,
// including while he is still walking out after the peer has gone.
const SquachMesh::Peer* uiClearGuest() { return visitHosting(); }

// ---- messages ---------------------------------------------------------------
// A received message is drawn in RED, filled, so it can never be mistaken for
// the scripted banter two Squachys trade on their own. That is pink and black,
// and it is the device talking to itself; red means a person sent it.
static const uint32_t MSG_SHOW_MS = 8000;

static bool    s_msgGuestOn = false;      // a visitor is on screen this frame
static int     s_msgGx = 0, s_msgHeadTop = 0;
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
    // A typed message runs to 48 characters, wider than a portrait screen:
    // it wraps to two lines, or three, rather than running off the edge.
    int maxW = w - 20;
    if (maxW > 47 * t.textWidth("M")) maxW = 47 * t.textWidth("M");
    char rows[3][48];
    const uint8_t n = Theme::wrapText(t, line, maxW, rows, 3);
    int bw = t.textWidth(from);
    for (uint8_t i = 0; i < n; i++) {
        const int rw = t.textWidth(rows[i]);
        if (rw > bw) bw = rw;
    }
    bw += 12;
    if (bw > w - 8) bw = w - 8;
    const int bh = 17 + n * 9;                // 26 for one line, as it always was
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
    t.setTextColor(Theme::WHITE, Theme::RED);
    for (uint8_t i = 0; i < n; i++) {
        t.setCursor(bx + 6, by + 14 + i * 9);
        t.print(rows[i]);
    }
}

// The little bubble. Cyan dots when there is nothing new; the dots take turns
// while a message of ours is on the air, like somebody typing; and it goes red
// with a "!" when one has arrived and not been read.
// Sized for a thumb, not for discretion. It is the only way into messages,
// and at the first size, 20x13, it was hard to see and harder to hit.
static const int MSG_ICON_W = 32, MSG_ICON_H = 22;

static void drawMessageIcon(TFT_eSPI& t, int x, int y, bool unread, bool sending, uint32_t now) {
    const int iw = MSG_ICON_W, ih = MSG_ICON_H;
    const uint16_t edge = unread ? Theme::RED : Theme::CYAN;
    t.fillRoundRect(x, y, iw, ih, 5, unread ? Theme::RED : Theme::BG);
    t.drawRoundRect(x, y, iw, ih, 5, edge);
    t.fillTriangle(x + 4, y + ih - 1, x + 12, y + ih - 1, x + 4, y + ih + 5, edge);
    if (unread) {
        t.setTextSize(2);
        t.setTextColor(Theme::WHITE, Theme::RED);
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
    t.fillRoundRect(x, y, bw, bh, 6, Theme::PURPLE);
    t.drawRoundRect(x, y, bw, bh, 6, Theme::VAPOR_PINK);
    // A little head, for "more of us": round, with two ears.
    t.fillCircle(x + 12, y + 13, 6, Theme::VAPOR_PINK);
    t.fillRect(x + 5, y + 4, 4, 4, Theme::VAPOR_PINK);
    t.fillRect(x + 15, y + 4, 4, 4, Theme::VAPOR_PINK);
    t.setTextColor(Theme::WHITE, Theme::PURPLE);
    t.setCursor(x + 24, y + (bh - 14) / 2);
    t.print(b);
    t.setTextSize(1);
    // A finger-sized target around it.
    s_badX = (int16_t)(x - 6); s_badY = (int16_t)(y - 8);
    s_badW = (int16_t)(bw + 12); s_badH = (int16_t)(bh + 16);
    s_badgeOn = true;
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

static void drawCrowd(TFT_eSPI& t, uint32_t now, const Mesh::SquadMember* crowd,
                      uint8_t n, int top, int floorY, bool advance, bool msgFresh) {
    const int w = t.width();
    // Ours plus theirs. squadList() reports the peers it can hear and NEVER
    // this board, so the body count is one higher than the list is long.
    // Sizing the screen for `n` and then spending slot zero on ourselves is
    // what quietly dropped a visitor: with four in range you saw three.
    const uint8_t total = (uint8_t)(n + 1);
    const int bottom = floorY - 22;   // clear of the squad badge and the counters

    // The shape is a RULE, not a search. Up to four stand in one row; past
    // four they take two, the back row full and the front row holding
    // whatever is left over:
    //
    //     4 -> 4            5 -> 3 behind, 2 in front
    //     6 -> 3 and 3      7 -> 4 behind, 3 in front      8 -> 4 and 4
    //
    // Five across reads as a queue where four across still reads as a group,
    // and a search that was left to work it out for itself put five in a line
    // because a single row scored 1.54 against two rows' 1.40.
    const int rows = total <= 4 ? 1 : 2;
    const int cols = ((int)total + rows - 1) / rows;
    const float cw = (float)(w - 8) / (float)cols;
    const float ch = (float)(bottom - top) / (float)rows;

    // ...and then they are drawn as LARGE as that shape can carry. Each count
    // gets its own size: two of them have room to be nearly full height, eight
    // of them do not, and pinning every count to one size made the small
    // crowds needlessly tiny.
    //
    // A body is 56 wide and 74 tall in scale units, and it has to clear its
    // cell WITH the drift on top -- the wander is 15% of a cell either side
    // horizontally and 10% vertically, so the body itself may have 70% of the
    // width and 80% of the height. Without that margin neighbours collide at
    // the ends of their drift rather than at rest, which is the kind of fault
    // that only shows up a few seconds after a screenshot is taken.
    const float sMax = (float)(floorY - top) / 91.0f;
    float s = fminf(0.70f * cw / 56.0f, 0.80f * ch / 74.0f);
    if (s > sMax) s = sMax;

    // Where a cell sits, and how far its occupant has wandered off the middle
    // of it. `drift` is what ours is denied.
    // Half a body each way: for keeping them on the screen, and for standing
    // them in the middle of their own patch of it.
    const int halfW = (int)(28.0f * s), halfH = (int)(37.0f * s);
    auto cellX = [&](int k, int i, bool drift) {
        const float dx = drift ? sinf((float)now / 1900.0f + (float)i * 1.7f) * cw * 0.15f : 0.0f;
        int x = 4 + (int)(((float)(k % cols) + 0.5f) * cw + dx);
        // Nobody drifts off an edge. The left-hand cell was taking his box to
        // x=-2, which is half a Squachy hanging off the side of the screen.
        if (x < halfW + 2)     x = halfW + 2;
        if (x > w - halfW - 2) x = w - halfW - 2;
        return x;
    };
    // His FEET -- placed so the body stands in the MIDDLE of its cell rather
    // than on the floor of it. With a single row that difference is the whole
    // feature: feet at the bottom of the band put everybody along the bottom
    // edge, standing in a line, which is exactly what roaming replaces.
    auto cellY = [&](int k, int i, bool drift) {
        const float dy = drift ? sinf((float)now / 2600.0f + (float)i * 0.9f) * ch * 0.10f : 0.0f;
        return top + (int)(((float)(k / cols) + 0.5f) * ch + (float)halfH + dy);
    };

    // Ours takes the most central cell there is and holds it. In a crowd where
    // everything is moving, the one you actually control is the one you have to
    // be able to pick out, and a seat that never moves picks itself out.
    // Measured against the middle of the screen rather than assumed, because
    // the middle of a 3x3 and the middle of a 4x2 are not the same cell.
    int selfIdx = 0;
    {
        float bestD = -1.0f;
        const float mx = (float)w * 0.5f, my = (float)(top + bottom) * 0.5f;
        for (int k = 0; k < (int)total; k++) {
            const float x = 4.0f + ((float)(k % cols) + 0.5f) * cw;
            // The middle of the cell, which is now the middle of the body in
            // it: cellY() stands him in his patch rather than on its floor.
            const float y = (float)top + ((float)(k / cols) + 0.5f) * ch;
            const float d = (x - mx) * (x - mx) + (y - my) * (y - my);
            if (bestD < 0.0f || d < bestD) { bestD = d; selfIdx = k; }
        }
    }
    // Which cell peer `i` gets: everybody shuffles past the seat ours is in.
    auto peerCell = [&](uint8_t i) { return (int)i < selfIdx ? (int)i : (int)i + 1; };

#ifndef ARDUINO
    // Emulator only: the geometry itself, not where bodies happened to land.
    // Two fixes aimed at selfIdx and the row headroom changed nothing on
    // screen, which means the numbers are not what they are being read as.
    if (getenv("SQUACHSIM_CROWDBOX"))
        fprintf(stderr, "[crowdgeom] total=%u cols=%d rows=%d s=%.3f top=%d bottom=%d "
                        "cw=%.1f ch=%.1f selfIdx=%d selfX=%d selfY=%d\n",
                (unsigned)total, cols, rows, (double)s, top, bottom,
                (double)cw, (double)ch, selfIdx,
                cellX(selfIdx, 0, false), cellY(selfIdx, 0, false));
#endif

    // Who is visiting, if anybody. He keeps the conversation -- his line, his
    // nodding, his laugh -- and the rest are company.
    int guestI = -1;
    for (uint8_t i = 0; i < n; i++)
        if (Mesh::peer() && memcmp(Mesh::peerMac(), crowd[i].mac, 6) == 0) { guestI = (int)i; break; }

    // Nameplates stop being labels and start being clutter somewhere around
    // five: past that they come off, and a tap puts one back for a few
    // seconds. Four or fewer and everybody keeps his, which is the size where
    // they still read.
    const bool allNames = total <= 4;
    if (s_tapName && (int32_t)(s_tapUntil - now) <= 0) s_tapName = false;
    s_crowdN = 0;

    // One of them, body only. Records where he landed so a tap can find him,
    // and reports whether he is holding a bubble.
    auto drawOne = [&](uint8_t i, int cx, int baseY, bool isGuest) {
        const SquachMesh::Peer& p = crowd[i].peer;
        Squachy::setOutfitPreview((int8_t)p.outfit);
        Squachy::setShadesPreview((int8_t)p.shade);
        const char* line = (isGuest && !msgFresh) ? s_visitGuestLine : nullptr;
        Squachy::drawWaving(t, cx, baseY, now + (uint32_t)i * 137u, s, line,
                            line != nullptr, 0, i % 3 == 0, 18, isGuest && now < s_guestLaughUntil,
                            isGuest && !s_guestTurn, line != nullptr,
                            isGuest ? guestPose(now) : Squachy::VisitPose::NONE);
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

    // His name, small, where his bubble would be so the two can never both be up.
    auto drawName = [&](uint8_t i, int cx, int baseY) {
        const SquachMesh::Peer& p = crowd[i].peer;
        const char* nm = (p.custom && p.name[0]) ? p.name : Squachy::nicknameAt(p.nick);
        t.setTextSize(1);
        t.setTextWrap(false);
        const int nw = t.textWidth(nm);
        int nx = cx - nw / 2;
        if (nx < 2) nx = 2;
        if (nx + nw > w - 2) nx = w - 2 - nw;
        t.setTextColor(Theme::CYAN, Theme::BG);
        t.setCursor(nx, baseY - (int)(58.0f * s) - 10);
        t.print(nm);
    };

    auto wantsName = [&](uint8_t i) {
        return allNames || (s_tapName && memcmp(s_tapMac, crowd[i].mac, 6) == 0);
    };

    // The order of what follows is the whole point of it.
    //
    // At eight, a speech bubble is wider than the patch its owner stands in,
    // so it WILL cross a neighbour -- there is no layout where it doesn't.
    // Drawn last it crosses him the way a speech bubble is meant to, in front,
    // instead of being half-painted over by whoever happened to come after.
    // So: the silent ones go down first, then their nameplates, then the only
    // two who can be holding a bubble -- the visitor, and ours.

    // 1. everybody who is neither talking nor us.
    for (uint8_t i = 0; i < n; i++) {
        if ((int)i == guestI) continue;
        const int k = peerCell(i);
        drawOne(i, cellX(k, i, true), cellY(k, i, true), false);
    }
    // 2. their names, over the lot of them.
    for (uint8_t i = 0; i < n; i++) {
        if ((int)i == guestI || !wantsName(i)) continue;
        const int k = peerCell(i);
        drawName(i, cellX(k, i, true), cellY(k, i, true));
    }
    // 3. the visitor, bubble and all.
    if (guestI >= 0) {
        const uint8_t i = (uint8_t)guestI;
        const int k = peerCell(i);
        const int cx = cellX(k, i, true), baseY = cellY(k, i, true);
        if (!drawOne(i, cx, baseY, true) && wantsName(i)) drawName(i, cx, baseY);
    }
    // 4. and ours last of all, in the seat that does not move. He is drawn
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
        static float corr = 0.85f;
        const float got = Squachy::lastScale();
        if (got > 0.05f && s > 0.05f) {
            float want = corr * s / got;
            if (want < 0.2f) want = 0.2f;
            if (want > 3.0f) want = 3.0f;
            corr += (want - corr) * 0.35f;
        }
        const int band = baseY - top;
        int pct = band > 1 ? (int)(corr * s * 56.0f * 100.0f / (float)band) : 100;
        if (pct < 10)  pct = 10;
        if (pct > 100) pct = 100;
        Squachy::tick(t, cx, top, band, now, advance, 0.3f, false, 0, (uint8_t)pct);
    }
}

// A tap on one of the crowd: puts his name up for a few seconds. Ours is
// deliberately not in the list -- a tap on him pets him, which is what a tap
// on him has always done, and asking a Squachy you own who he is is not a
// question anybody has.
bool uiClearCrowdTap(int x, int y, uint32_t now) {
    if (Settings::boringMode()) return false;
    for (uint8_t i = 0; i < s_crowdN; i++) {
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
        if (ix + MSG_ICON_W > w - 4) ix = w - 4 - MSG_ICON_W;
        // Dropped clear of the red bubble while one is up: at the larger
        // size the two overlapped at the corner and, both red, read as one.
        drawMessageIcon(t, ix, head + 4 + (showing ? 8 : 0),
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
            s_napping   = false;
            s_wakeUntil = 0;
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
        s_hosting   = *g;                   // copied once, on arrival
        s_hostingId = id;
        s_vp = VisitPhase::ARRIVING; s_vpAt = now;
        s_guestTurn = true;                 // so the HOST speaks first
        s_hangStep  = 0;                    // and asks the first question
        s_guestLaughUntil = 0;              // no laughter carried in from
        s_piece = Piece::NONE;              // nor a set piece, a scare or a nap
        s_nextPieceAt = 0;
        s_guestStartleUntil = 0;
        s_napping = false; s_wakeUntil = 0;
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
        s_piece = Piece::NONE; s_napping = false; s_wakeUntil = 0;
        s_vp = VisitPhase::LEAVING;
        visitBeat(now, Squachy::VisitMoment::PART);
        s_vpAt = now + s_beatMs;            // goodbye first, then the walk
        return;
    }
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
            if (now - s_vpAt >= WALK_MS) {
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
                    s_nextPieceAt = now + PIECE_FIRST_MS + (uint32_t)random(0, 15000);
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
            // A set piece, or a nap, holds the conversation until it is done.
            if (s_piece != Piece::NONE)  { pieceTick(now); break; }
            if (s_napping || s_wakeUntil) { napTick(now);   break; }
            // Emotes are handled above, before the switch: they interrupt from
            // any phase with two Squachys on screen rather than only this one.
            if (now - s_beatAt >= s_beatMs + TURN_GAP_MS) {
                // No timed exit. He used to say goodbye after a minute and
                // then -- with the other board still right there -- walk
                // straight back in on the next frame, which on hardware read
                // as a Squachy who kept trying to leave. He goes when the
                // other one does: the id check above sends him off, with his
                // goodbye, the moment the peer is gone.
                // Between exchanges only -- never cutting a question off
                // from its answer.
                if (s_hangStep == 0 && napDue(now))
                    napStart(now);
                // Not while a crowd is on screen: every set piece puts the two
                // of them on marks on the ground, and in a crowd there is no
                // ground -- they are drifting. The conversation carries on.
                else if (s_hangStep == 0 && s_nextPieceAt && Settings::meshCrowd() <= 1 &&
                         (int32_t)(now - s_nextPieceAt) >= 0)
                    pieceStart(now);
                else
                    visitBeat(now, Squachy::VisitMoment::HANGOUT);
            }
            break;
        case VisitPhase::LEAVING:
            if ((int32_t)(now - s_vpAt) >= (int32_t)WALK_MS) {
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

// Whoever WOULD be visiting right now, before the visit machine has decided
// what to do about it. Not what gets drawn -- see visitHosting().
static const SquachMesh::Peer* rawGuest(uint32_t now) {
    // The tutorial's DEMO visitor comes first: while it runs, it is the
    // lesson, and a real peer turning up must not walk onto the stage.
    if (const SquachMesh::Peer* d = MeshTutor::guest()) return d;
    if (s_guest) return s_guest;          // the emulator's --peer, when set
    if (const SquachMesh::Peer* p = Mesh::peer()) { s_realSeenAt = now; return p; }
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
    if (MeshTutor::guest()) return 0x7070u;
    if (s_guest) return 0x5111u;
    if (Mesh::peer()) {
        const uint8_t* m = Mesh::peerMac();
        uint32_t h = 2166136261u;                  // FNV-1a, plenty for six bytes
        for (int i = 0; i < 6; i++) { h ^= m[i]; h *= 16777619u; }
        return h ? h : 1u;                         // 0 means "nobody"
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
        // four BLE-tracker types into one column to save space.
        case DetectionType::AIRTAG:      return "TRACKER";
        case DetectionType::DRONE:       return "DRONE";
        case DetectionType::RAVEN:       return "RAV";
        case DetectionType::ALPR:        return "ALPR";
        case DetectionType::CAMERA:      return "CAM";
        case DetectionType::SAMSUNG_TAG: return "STAG";
        case DetectionType::GOOGLE_TAG:  return "GTAG";
        case DetectionType::TILE:        return "TILE";
        case DetectionType::RING:        return "RING";
        case DetectionType::DEAUTH:      return "DEAUTH";
        case DetectionType::EVILTWIN:    return "EVIL";
        // Four letters, not "HACKER": this row is packed six-across in
        // landscape and four-across on a 240px portrait panel. EVILTWIN
        // keeps its own label everywhere else -- it just does not get its
        // own column here, because its count is inside this one.
        case DetectionType::HACKER:      return "HACK";
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
static uint16_t counterCount(const DetectionEngine& eng, DetectionType t) {
    uint16_t n = eng.countByType(t);
    if (t == DetectionType::AIRTAG) {
        n += eng.countByType(DetectionType::GOOGLE_TAG)
           + eng.countByType(DetectionType::TILE)
           + eng.countByType(DetectionType::SAMSUNG_TAG);
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

// All types in one place, chunked into rows of at most MAX_PER_ROW at
// draw time (see uiClearTick()) instead of two hand-split arrays --
// the old 7-and-6 split ran wide enough on a narrow 240px portrait
// screen that FLOCK (first on the line) got clipped off the left edge
// entirely. A hard per-row cap fixes that on both boards, not just
// AWOK's narrower panel. GOOGLE_TAG/TILE are deliberately absent --
// AIRTAG stands in for all four as "TRACKER" (see counterLabel/
// counterCount above).
static const DetectionType ALL_COUNTER_TYPES[] = {
    DetectionType::FLOCK,   DetectionType::AXON,     DetectionType::META,   DetectionType::SKIMMER,
    DetectionType::RAVEN,   DetectionType::AIRTAG,   DetectionType::DRONE,  DetectionType::ALPR,
    DetectionType::CAMERA,  DetectionType::HACKER,   DetectionType::RING,   DetectionType::DEAUTH,
};
static const uint8_t ALL_COUNTER_TYPES_N = sizeof(ALL_COUNTER_TYPES) / sizeof(ALL_COUNTER_TYPES[0]);
// Portrait (narrow) caps at 4 per row -- see the comment above. Landscape
// has plenty of width for the original 7-and-6 two-row split (that's
// exactly what this produces: 13 types / 2 rows), so row count is
// picked dynamically off the live orientation in uiClearTick() rather
// than fixed at compile time -- it changes every time the screen
// rotates, not just once per board.
static const uint8_t MAX_PER_ROW_PORTRAIT   = 4;
static const uint8_t COUNTER_ROWS_LANDSCAPE = 2;
static const uint8_t COUNTER_ROWS_PORTRAIT  =
    (ALL_COUNTER_TYPES_N + MAX_PER_ROW_PORTRAIT - 1) / MAX_PER_ROW_PORTRAIT;  // ceil

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
    int tw = t.textWidth(buf);
    t.setCursor((w - tw) / 2, y);
    t.print(buf);
}

void uiClearTick(TFT_eSPI& t, uint32_t now, const DetectionEngine& eng, bool advance, bool scanMenu) {
    int w = t.width();
    int h = t.height();
    // Recomputed every tick, not cached per-board: rotating the screen
    // changes w/h live, and the counter layout should follow it rather
    // than staying stuck at whatever orientation was active at boot.
    bool landscape = w > h;
    const uint8_t counterRows = landscape ? COUNTER_ROWS_LANDSCAPE : COUNTER_ROWS_PORTRAIT;

    Theme::ButtonBarGeom bar = Theme::computeButtonBar(w, h);
    const int lineH          = 14;
    const int countersTop    = bar.y - counterRows * lineH - 6;
    // bar.y - 1, not bar.y - 4. Those three rows were a real seam rather
    // than spare margin: the animation repainted down to 209 and the button
    // bar starts at 214, so 210-213 were painted once at boot and never
    // touched again. On DIGITAL RAIN a flat black strip sat under the
    // falling glyphs; on TERMINAL LOG the text stopped a few rows short of
    // the buttons. Nothing draws there, so the animation may as well.
    //
    // It is also what lets the counter rows sit as low as they now do --
    // see counterTextTop. They have to land inside the repainted region or
    // they smear, and 209 was the wall.
    const int countersBottom = bar.y - 1;
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
    // Not further: 13 would touch the buttons. The real ceiling was lower
    // still until a moment ago -- the rows have to land inside the
    // background's repaint or they smear, and that wall sat at 209, which
    // is why countersBottom above had to move first.
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

    // The headline hangs off the counter block now, not off countersTop.
    //
    // Measured off a render: ty puts the Bangers ink 4 rows lower and it
    // runs 23 rows, with the 24-pass outline adding 2 more each way. So the
    // whole painted band is ty+2 .. ty+28, and sitting it HEADLINE_PAD above
    // the counters is that arithmetic run backwards.
    //
    // It used to bottom-align to countersTop, which put it at row 146 --
    // floating across his shins in the middle of otherwise empty space,
    // with 17 rows of nothing between it and the numbers it belongs to.
    // LG, not MD. The size is a consequence of the word: at 90px LG the
    // headline uses under a third of a 320px row, where the old 17-character
    // one needed MD just to fit and still ran 216px. Short text does not
    // want the same face at a smaller size, it wants the bigger face -- and
    // this row is a burst that cuts in for a moment, so it has to land.
    static const Theme::BangersSize HEADLINE_SIZE = Theme::BangersSize::LG;
    // Measured off a render at HEADLINE_SIZE by diffing a seeded frame
    // against a --noseed one, which isolates the headline from a background
    // that repaints every row of this band: the painted result runs ty+4 to
    // ty+32, so 29 rows of which the 24-pass outline is the outer 2 each
    // way. 33 is what puts that last painted row exactly HEADLINE_PAD above
    // the counter text.
    static const int HEADLINE_H   = 33;
    static const int HEADLINE_PAD = 5;
    const int headlineTop = counterTextTop - HEADLINE_PAD - HEADLINE_H;

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
    const int squachyBottom = counterTextTop - 2;

    const int titleBottom  = 16;
    // The animation runs all the way down to just above the button
    // bar, covering Squachy's region, the text row, and the counters —
    // everything below the title bar erases and repaints together
    // every frame.
    const int rainEnd      = countersBottom;

    // Background animation, from below the title bar down to just
    // above the button bar — style picked from the settings menu.
    // The counters get drawn over the band further down, so tell the
    // background where its usable floor really is before it places
    // anything that stands on the ground.
    // Two arguments now, because the counters no longer start where Squachy's
    // feet land. Cameos still stand level with him at countersTop; the
    // backgrounds that fill a bright ground band keep filling to where the
    // numbers actually begin, instead of stopping nine pixels short of them.
    Theme::setBackgroundFloor(squachyBottom, counterTextTop);
    // From the very top of the screen, not from titleBottom. The title bar
    // used to own rows 0-15 and paint them every frame; with it gone they
    // belonged to nobody and kept whatever the previous frame left there.
    // Handing them to the background is also the point of removing the bar:
    // the animation now runs edge to edge behind the two corner buttons.
    //
    // Squachy is still told his band starts at titleBottom. He sizes himself
    // from the space he is given, so telling him about these rows would make
    // him a tenth bigger and move everything hanging off him.
    Theme::drawActiveBackground(t, now, 0, rainEnd, eng, advance);
    Theme::clearBackgroundFloor();

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
    if (CrowdBench::active() && !Settings::boringMode())
        CrowdBench::draw(t, now, titleBottom, squachyBottom);
    else
#endif
    if (!Settings::boringMode()) {
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
        // The hosted COPY, not whoever the radio is hearing right now. That
        // is what keeps a visitor from morphing mid-visit and what lets him
        // still be drawn while he walks out after the peer has gone.
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
            s_crowdDrawn = true;
            drawCrowd(t, now, crowd, crowdN, titleBottom, squachyBottom, advance, msgFresh);
        } else if (guest) {
            const int SMALL_PCT = 70;
            const int gap  = w / 4;
            const int homeX = w / 2 + gap;     // where the guest stands
            const int offX  = w + 40;          // off the right edge

            Squachy::tick(t, w / 2 - gap + hostLeanPx(), titleBottom,
                          squachyBottom - titleBottom,
                          now, advance, 1.0f, false, 0, SMALL_PCT);

            // The visitor is drawn, not ticked: tick() owns mood, quip timers
            // and the walk state, all of which are file-static singletons
            // describing OUR Squachy. Calling it twice would have the guest
            // driving the host's animation. drawWaving is the same body with
            // none of that -- it is what the alert screen's cameo already uses.
            //
            // Both previews are the existing overrides the unlock popup uses
            // to show a costume nobody owns yet. Set them, draw, clear them:
            // left set they would silently redress our own Squachy everywhere.
            Squachy::setOutfitPreview((int8_t)guest->outfit);
            Squachy::setShadesPreview((int8_t)guest->shade);
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
            // wanderRangePx is what animates his legs. Walking in with it at 0
            // slid him across the floor like furniture; a couple of pixels of
            // wander is enough to put a walk cycle under the movement without
            // reading as a stagger.
            // Waves while walking in and through the hellos, then settles.
            // A guest who never stops waving reads as a stuck frame rather
            // than as a greeting once he has been there half a minute.
            const bool stillGreeting = (s_vp == VisitPhase::ARRIVING ||
                                        s_vp == VisitPhase::MEETING ||
                                        s_vp == VisitPhase::LEAVING ||
                                        s_piece == Piece::WAVE);    // and when waved at
            Squachy::drawWaving(t, gx, squachyBottom - scriptGuestLift(now, gs,
                                    squachyBottom - (int)(58.0f * gs) - 20), now, gs,
                                msgFresh ? nullptr : s_visitGuestLine,
                                // Mouthing it while the red bubble is up.
                                msgFresh || s_visitGuestLine != nullptr,
                                (visitWalking() || fiveWalking(now) || scriptWalking(now)) ? 2 : 0,
                                stillGreeting,
                                // Just above his own head, not the boot
                                // splash's 34 -- that lands in the host's
                                // bubble row and the two paint over each
                                // other. Higher by a hat, when he has one.
                                20 + scriptHatPx(gs),
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
            Squachy::setShadesPreview(-1);
            Squachy::setOutfitPreview(-1);

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

            // Nameplate. It goes in the row his speech bubble uses, and only
            // on the beats when he is not using it.
            //
            // It used to sit under his feet, on the reasoning that above is
            // where the bubble goes and a name up there would be hidden for
            // exactly the moments he is worth identifying. Correct reasoning,
            // impossible position: squachyBottom is counterTextTop - 2, so
            // the fit test below it asked whether 8 <= -2 and the answer has
            // always been no. The name has never been drawn, on any board, in
            // any orientation -- which meant custom names travelled between
            // devices, were decoded correctly, and were then shown to nobody.
            //
            // Taking turns with the bubble solves both halves. There is
            // guaranteed room, because the bubble fits there; and he is
            // silent for most of a visit, so the name is up most of the time
            // and gone only while he is saying something -- at which point
            // which of the two is speaking is not in question anyway.
            if (!s_visitGuestLine && !msgFresh) {
                const char* nm = (guest->custom && guest->name[0])
                                   ? guest->name
                                   : Squachy::nicknameAt(guest->nick);
                t.setTextSize(1);
                t.setTextWrap(false);
                const int nw = t.textWidth(nm);
                int nx = gx - nw / 2;
                if (nx < 2) nx = 2;
                if (nx + nw > w - 2) nx = w - 2 - nw;
                // The same head-top drawWaving() computes from the same two
                // numbers, then the bubble's own gap, then its height -- so
                // the name sits on the baseline the bubble would have used.
                const int headTopY = squachyBottom - (int)(58.0f * gs);
                const int ny = headTopY - 20 - scriptHatPx(gs) + 3;
                t.setTextColor(Theme::CYAN, Theme::BG);
                t.setCursor(nx, ny);
                t.print(nm);
            }
            // Where he is this frame, for the message bubble and its button.
            s_msgGuestOn = true;
            s_msgGx      = gx;
            s_msgHeadTop = squachyBottom - (int)(58.0f * gs);
        } else
#endif
        Squachy::tick(t, w / 2, titleBottom, squachyBottom - titleBottom, now, advance,
                      1.0f, false, -1, Settings::squachySizePct());
#if SQUACH_MESH
        // Everybody in range, whether or not one of them is on screen.
        {
            const uint8_t squad = Mesh::squadCount(now);
            if (squad >= 1) drawSquadBadge(t, w - 4, counterTextTop - 2, squad);
        }
        drawMessageUi(t, now, titleBottom, squachyBottom);
#endif
    }


    // Rare decorative flourishes (UFO/sparkle/critter/glitch-line) --
    // see idle_events.h. Skipped for the same reasons Squachy's own
    // presence is skipped above (boring mode) or would collide with
    // the walkthrough's own bubble (onboarding) -- a UFO flying past
    // mid-lesson would be more distraction than delight.
    if (!Settings::boringMode() && !Squachy::onboardingActive()) {
        IdleEvents::tick(t, now, 0, titleBottom, w, squachyBottom, advance);
    }

    // Whatever the background wants on top of the mascot. Right now
    // that is the werewolf's speech bubble: drawFire computes it but
    // deliberately does not paint it, because the background is drawn
    // before Squachy and the bubble was ending up behind him. Same
    // reasoning as ALL CLEAR below -- text is the one thing here that
    // cannot afford to be half-covered.
    Theme::drawBackgroundOverlay(t, now);

    // Title bar at the top
    Theme::drawTitleBar(t, ">> SQUACHWATCH <<  SCANNING");

    // ALL CLEAR (only flash if there are NO active detections). Same
    // Bangers headline font as the ALERT screen's "!! DETECTION !!" —
    // now sitting directly on top of the counter block it labels, rather
    // than floating in the middle of the empty space above it. See
    // headlineTop.
    //
    // Still drawn AFTER Squachy, and that stays deliberate. Drawing it
    // first would let his shadow and feet fall across it, which sounds
    // better than it is: he is about 60px wide at the ankles against a
    // 185px headline, and a word with its middle punched out is not a
    // word. The 24-pass black outline is what keeps it legible over him.
    // Is anything actually live right now? Not lifetime -- a camera seen an
    // hour ago is not something happening, and the counters decay for the
    // same reason.
    bool anyActive = false;
    for (uint8_t i = 0; i < (uint8_t)DetectionType::COUNT; i++) {
        if (eng.countByType((DetectionType)i) > 0) { anyActive = true; break; }
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

    // Nothing on the row while nothing is happening. ALL CLEAR is gone and
    // the headline does not replace it: a permanent label asserting anything
    // over a screen of zeroes is just untrue, and the counters underneath
    // already say the same thing more precisely.
    //
    // Blank is not a compromise here, it is the better screen. Squachy's
    // geometry hangs off counterTextTop rather than this row, so he does not
    // move -- he is simply no longer painted over by a 25-pass headline that
    // exists to be legible ON TOP of him. The pet and the Mowin' Man stop
    // running in behind it. And the 24 outline passes plus the fill come off
    // the frame the device spends nearly all its time rendering.
    //
    // Nothing needs erasing: the background repaints this whole band every
    // frame, so a headline that stops being drawn is simply gone next frame.
    if (anyActive) {
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
            for (uint8_t i = 0; i < 24; i++) {
                Theme::drawBangersText(t, tx + OUTLINE_OFS[i][0], ty + OUTLINE_OFS[i][1],
                                        msg, Theme::BLACK, HEADLINE_SIZE);
            }
            Theme::drawBangersText(t, tx, ty, msg, col, HEADLINE_SIZE);
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
        }
    }

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
    // Still before the counters, which he never reaches.
    if (!Settings::boringMode()) Pet::tick(t, now, w, titleBottom, squachyBottom);

    // Counter lines above the buttons — all 13 detection types, split
    // across counterRows (2 in landscape, capped at 4/row in portrait
    // -- see the constants above) so a row never runs wide enough to
    // clip off a narrow portrait screen, while landscape still gets
    // the more compact two-row layout it has room for. Whole
    // label:count tokens only, so nothing ever breaks mid-word. No
    // flat clear here anymore — the background animation now fully
    // repaints this whole row every frame (rainEnd extends down to
    // countersBottom), the same "let the background do the erasing"
    // pattern already relied on for Squachy and the status line above.
    t.setTextSize(1);
    t.setTextColor(Theme::CYAN, Theme::BG);
    t.setTextWrap(false);

    // Evenly balanced, not greedily packed (e.g. 4/4/4/1 in portrait)
    // -- a lone last row with a single item looked worse than several
    // similarly-sized rows does, and this still never exceeds the
    // per-orientation cap on any row.
    uint8_t base      = ALL_COUNTER_TYPES_N / counterRows;
    uint8_t remainder = ALL_COUNTER_TYPES_N % counterRows;
    uint8_t start = 0;
    for (uint8_t row = 0; row < counterRows; row++) {
        uint8_t n = base + (row < remainder ? 1 : 0);
        drawCounterLine(t, w, counterTextTop + row * lineH, eng, ALL_COUNTER_TYPES + start, n);
        start += n;
    }

    // Soft buttons. The background animation's own repaint stops at
    // countersBottom (see rainEnd above) and each button only fills
    // its own rect, so the row's margins/gaps around and between the
    // three buttons were never actually touched by anything -- on
    // cyd35 specifically (two-pass half-height rendering, see its
    // CLEAR case in main.cpp) that let content from elsewhere in the
    // shared sprite buffer show through there. An explicit flat clear
    // of the whole remaining strip first guarantees it's always clean
    // background before the buttons draw on top, regardless of cause.
    t.fillRect(0, countersBottom, w, h - countersBottom, Theme::BG);
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
