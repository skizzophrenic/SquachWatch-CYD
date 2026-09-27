// SquachWatch-CYD — the LORA screen: what the wireless slot hears, and which
// antenna hears it best.
//
// Ten views on one screen. Six are the maintenance tool: LIST is every frame
// newest first and a tap opens PACKET, the bytes and the decoded line; the same
// ring filtered to MeshCore adverts is a seventh nothing extra is stored for;
// NODES is one row per transmitter; TRAFFIC is the rate and the occupancy of the
// air as they move; CHANS is the crypto channels, and a tap on one opens
// CHANMSG, what actually came through that key; STATS is the radio channel.
// Two are the antenna survey -- SURVEY, the stations heard first-hand with the
// trend of each one's signal, and SURVEYCMP, two runs of it against each other,
// which is where the measurement actually happens. PICK is the grid that
// reaches every one of them in one tap.
//
// HOW THEY ARE REACHED, because four views used to sit in a ring with the two
// bar buttons naming their neighbours and ten cannot. A ring of ten is five taps
// wide, and a button that says [ TRAFFIC ] tells you nothing about where the
// other eight are. So the bar is [ BACK ] [ VIEWS ] [ SURVEY ] on every view:
// the picker puts all ten on the glass at once with a live figure under each, so
// every view is two taps from every other, and the survey -- the reason this
// phase exists -- is one tap from all of them and never behind the picker. What
// a view needs beyond that it draws for itself, big, in its own body: the
// survey's START/STOP is a 392-pixel button because it is pressed with a thumb
// while the other hand holds an antenna.
//
// THE BAR OWNS EVERY PIXEL UNDER THE BODY, not only the twenty its buttons are
// drawn in. It is drawn six pixels short of the bottom of the screen and the
// body stops four above it, and those eight rows of nothing -- two millimetres
// either side of a 7.6 mm target -- used to fall through to the view's own hit
// test, where the frame list's row arithmetic opened a frame. That is what the
// owner hit: "ich klicke auf VIEWS, ich bekomme einen einzelnen frame
// angezeigt; erst wenn ich nochmal klicke, kann ich die views auswaehlen." A
// press two millimetres off a button presses that button. sim/test_lora_bar.sh
// holds it, edge to edge, off the board.
//
// THE MIDDLE BUTTON IS THE PICKER IN EVERY VIEW, and the sentence above was not
// true of the code until the bar was fixed: PACKET relabelled slot 1 to "[ < ]"
// and stepped frames with it, so the views button was the views button in nine
// views and a frame-step in the tenth. Slot 2 is the per-view slot -- it says
// SURVEY, or CMP in the survey, or OLDER in the frame view -- and its label
// always names what it does. src/ui_lora.cpp's barLabels() carries the rule and
// says what PACKET gave up for it.
//
// Plain text at size 1 for the dense lists on purpose: this is the maintenance
// tool and a sysop wants the numbers, not the mascot. The SURVEY view is the
// deliberate exception and src/ui_lora.cpp says why -- it is read at arm's
// length, outdoors, one-handed.
//
// What a finger can do here is limited by design, and the views say so at their
// foot rather than pretending otherwise: a panel with no keyboard cannot type
// `#gelsenkirchen` or `dipole at the balcony rail`, so adding and dropping
// channel keys is `LORA CHAN` and naming a survey run is `LORA SURVEY LABEL`.
#pragma once
#include <TFT_eSPI.h>
#include <stdint.h>

class DetectionEngine;

enum class LoraView : uint8_t {
    LIST = 0,     // every frame, newest first (ADVERTS is this one filtered)
    PACKET,       // one frame: the fields, the decode, the bytes
    NODES,        // one row per transmitter
    STATS,        // the radio channel: counters, airtime, noise, the spectrum
    CHANS,        // the crypto channels and their keys
    CHANMSG,      // what came through one channel's key -- or through all of them
    TRAFFIC,      // the rate and the occupancy of the air, as they move
    SURVEY,       // the antenna survey: the stations heard first-hand, live
    SURVEYCMP,    // two survey runs against each other: the measurement
    PICK,         // the grid that reaches all of the above
    COUNT
};

// Opens on `v`; the SYSTEM page's LORA CHANNELS row comes straight to CHANS.
// The views that need a subject chosen first (PACKET, CHANMSG) and the picker
// itself open on LIST instead.
void uiLoraInit(TFT_eSPI& t, LoraView v = LoraView::LIST);
// Draws the screen and says whether it did. The ten views are static text that
// changes a few times a minute, and the frame they were drawn into is kept
// from one loop() to the next, so a tick whose inputs are what they were at
// the last draw -- the view, the scrolls, the counters, a toast, the clock's
// second -- leaves the frame alone and returns false, and main.cpp then skips
// the push (ui_lora.cpp's sceneSig says exactly what counts as an input).
// uiLoraDirty() forces the next tick to draw regardless: a finger on the
// glass, and the transition glitch, which paints over the frame.
bool uiLoraTick(TFT_eSPI& t, uint32_t now, const DetectionEngine& eng, bool advance = true);
void uiLoraDirty();
void uiLoraScroll(int delta);                // positive = down
// How far a finger must travel for one row of scroll: the height of a row in
// whichever view is showing. main.cpp's drag compared against a flat 10 px,
// which on a finger-sized row scrolled the list two rows for every row of
// finger travel -- it slipped out from under the thumb -- and called a 12 px
// wobble during a tap a drag.
int  uiLoraDragStep(TFT_eSPI& t);
LoraView uiLoraView();

// A touch that has stayed put past main.cpp's hold threshold, at the position it
// went down. True when this screen took it, and then the caller must NOT let the
// release become a tap as well -- the same "the touch that opened the panel
// cannot also press it" rule the raw-scan screen's confirm panel keeps
// (src/main.cpp's RAWSCAN case).
//
// One gesture uses it today: a hold on a run in the SURVEYCMP strip raises the
// modal panel that drops that run or resets the whole survey. A tap there still
// chooses the run for the comparison, which is why deleting cannot be a tap --
// src/ui_lora.cpp's "dropping a run" block has the reasoning.
bool uiLoraHold(TFT_eSPI& t, int x, int y, int screenW, int screenH);

enum class LoraTap : uint8_t { NONE, HANDLED, BACK };
// Handles the bar, the rows and the in-body buttons itself; BACK means leave
// the screen, and the views that sit under another one (PACKET under LIST,
// CHANMSG under CHANS, the picker over whatever opened it) answer HANDLED and
// go up one level instead.
LoraTap uiLoraTap(TFT_eSPI& t, int x, int y, int screenW, int screenH);
