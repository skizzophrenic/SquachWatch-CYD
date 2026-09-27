// SquachWatch-CYD — the decoded messages, kept instead of printed and
// forgotten.
//
// Until now a MeshCore GRP_TXT or a Meshtastic text frame went to the console
// as one line and nowhere else: the key opened it, the counter went up, the
// words were gone. The CHANS view can say that thirteen keys are held and how
// many frames each has opened, which is a list of locks and not a list of what
// came through them. This is the ring behind "tap a channel and read it".
//
// ONE RING, NOT ONE PER CHANNEL, and the reason is where the traffic is: a
// board can hold twenty-four MeshCore channels and thirty-four Meshtastic ones,
// and on a real band nearly everything arrives on one or two of them. Sixty
// per-channel rings would spend nearly all of their memory on channels that
// never say anything, and the quiet channel somebody actually wants to read
// would get the same three slots as the busy one. One ring, filtered on the way
// out, gives the quiet channel its messages for as long as CAP total messages
// take to arrive -- hours on a quiet band. The cost is the honest one and it is
// written down here: a busy channel does push a quiet channel's older messages
// out, and dropped() says how many have gone.
//
// WHAT IS NOT KEPT
//
//   A channel with no key opens nothing, so there is nothing to keep. Its row
//   in the CHANS view shows zero frames and this ring holds nothing for it;
//   that is not a bug to work around, it is what encryption is.
//
//   A MeshCore GRP_DATA is a frame the key DID open, carrying something that is
//   not text -- a position, a sensor reading, a binary payload this firmware
//   does not parse. It is counted where every opened frame is counted
//   (Lora::ChannelRow::frames) and it is not kept here, because a ring of
//   messages holding a row that says "12 bytes of something" is a ring with the
//   messages pushed out of it. The difference between ChannelRow::frames and
//   what this ring holds for that channel is the honest way for a view to say
//   "opened more than it could read".
//
//   A direct message between two MeshCore nodes never decrypts at all: it is
//   sealed to a key pair this board does not have half of.
//
// Standalone, and the store is handed in the way the survey's is, so a board
// with no PSRAM and no radio pays nothing for the file being compiled.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "lora_pkt.h"

namespace Lora {
namespace Msgs {

// How many messages are held, all channels together. 256 of them is 71,696
// bytes -- 70 kB, measured by test/lora_msgs_test.cpp printing bytesNeeded() --
// of the CrowPanel 7's 7.6 MB of PSRAM, and at the traffic this board sees on
// 869.618 -- a handful of GRP_TXT an hour outside a net -- it is a day's worth
// rather than an afternoon's. A view lists the newest first and scrolls.
static const uint16_t CAP = 256;

struct Msg {
    Proto    proto;
    // The DECODER'S own channel index, not a row in the CHANS view: the view's
    // rows are MeshCore's list and Meshtastic's flattened together and the
    // flattening changes as quiet presets start being listed.
    // Lora::channelRowKey() turns a tapped row into this pair.
    uint8_t  chan;
    // The network's OWN type byte for this message, so a view can label a line
    // rather than showing every kind of text as chat: Meshtastic's portnum
    // (Meshtastic::PORT_TEXT, or PORT_ALERT for a critical alert, which is text
    // and is worth reading and would otherwise be thrown away), and MeshCore's
    // frame type (MeshCore::TYPE_GRP_TXT -- the only one that reaches here).
    uint8_t  port;
    uint32_t ms;      // millis() at reception
    uint32_t epoch;   // the wall clock's second, or 0 when the clock was not trusted
    int16_t  rssi;    // dBm of the copy we heard -- see `direct`
    int8_t   snr4;    // SNR x 4
    // Repeaters this copy crossed, as far as the frame says. 0xFF means the
    // frame gave no way to tell -- a Meshtastic sender before firmware 2.3
    // sends no hop_start, so hops away is unknowable (include/lora_nodes.h).
    uint8_t  hops;
    // Whether the frame came off the transmitter of the station that sent it,
    // by the same per-network rule the node table uses (include/lora_nodes.h).
    // False means rssi and snr4 measure the last relay's link to here and say
    // nothing about how far away the sender is.
    bool     direct;
    // Who it says sent it.
    //
    // MeshCore: whatever came before ": " in the plaintext. CLAIMED, NOT
    // VERIFIED -- a group message carries no signature and the sender field is
    // a string the sender typed. Anyone on the channel can type anyone's name.
    // Meshtastic: the node number, or the short name once a NodeInfo has
    // arrived, which is the node's own claim about itself in the same way.
    char     sender[40];
    // Meshtastic's node number, so a view can reach the node row. Zero for
    // MeshCore, where a group message identifies its sender only by that
    // claimed string -- the frame's source hash is one byte of a public key
    // and naming a node from one byte is wrong most of the time
    // (include/lora_feed.h).
    uint64_t senderId;
    // 201 bytes: MeshCore's GroupText::text holds 160 and Meshtastic's text
    // 200, so nothing arrives truncated by this ring.
    char     text[201];
};

// Exactly what begin() wants, and the same contract as the survey's: until a
// block is handed over, note() is a no-op and every read is empty.
size_t bytesNeeded();
bool   begin(void* mem, size_t bytes);
bool   ready();

// One decoded message. Called from Lora::Nodes' decoders, which are the one
// place a frame off the air is decoded exactly once -- decode() also runs for
// every row the LIST and CHANS views redraw, so a call from a screen would
// record the refresh rate.
void note(Proto p, uint8_t chan, uint8_t port, const char* sender, uint64_t senderId, const char* text,
          const Packet& pk, uint32_t now, bool direct, uint8_t hops);

// How many are held in total, and how many have been pushed out since boot.
uint16_t count();
uint32_t total();
uint32_t dropped();
// Newest first, across every channel. False past the end.
bool     at(uint16_t idxFromNewest, Msg& out);

// The same, for one channel. `count` walks the whole ring, which is 256
// comparisons -- cheaper than a per-channel index and paid only when a view is
// open on a channel.
uint16_t countFor(Proto p, uint8_t chan);
bool     atFor(Proto p, uint8_t chan, uint16_t idxFromNewest, Msg& out);
// A page of one channel in one pass over the ring, newest first: the shape
// Lora::nodeSnapshot has, for the same reason -- a caller that draws fourteen
// rows should walk the ring once, not fourteen times, and on the device every
// one of those walks happens inside the sniffer's lock.
uint16_t pageFor(Proto p, uint8_t chan, Msg* out, uint16_t cap, uint16_t from = 0);

void clear();

}
}
