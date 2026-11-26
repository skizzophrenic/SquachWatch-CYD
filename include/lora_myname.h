// SquachWatch-CYD — the name this board goes by on LoRa, both networks.
//
// One name for Meshtastic and MeshCore (the owner, 2026-10-21): MeshCore puts
// it before the colon of every text, Meshtastic carries it in the NodeInfo
// sent before the first text (src/lora_sniffer.cpp, announceIfDue). It is
// typed on the LORA SETTINGS page (src/ui_lorasettings.cpp) and kept in
// Settings::loraName(); empty means "the squad nickname", which is SQUACHY
// until someone names him, so a board nobody has touched behaves as before.
//
// The SHORT NAME is Meshtastic's four characters. Empty means the rule the
// NodeInfo always used: SQWT while the name is still the default SQUACHY
// ("SQUA" read as nothing, the owner's pick), otherwise the first four
// characters of the name.
//
// Standalone, for the host tests: no Arduino, no Settings, no Squachy. The
// callers hand in the stored values.
#pragma once
#include <stddef.h>

namespace Lora {
namespace MyName {

static const size_t NAME_LEN  = 24;   // characters, not counting the NUL
static const size_t SHORT_LEN = 4;

// The name that goes on the air: `custom` when it has anything but spaces in
// it, else `nick` the same way, else "SQUACHY". Never null, never empty.
const char* longName(const char* custom, const char* nick);

// Meshtastic's short name into out (cap >= SHORT_LEN + 1): `customShort` when
// it has anything but spaces, cut to four characters; else SQWT while
// `longName` is SQUACHY; else the first four characters of `longName`.
void shortName(const char* customShort, const char* longName, char* out, size_t cap);

// What a typed name may hold before it is stored: printable ASCII, no ':'
// (MeshCore splits "name: text" at the first colon), spaces trimmed from both
// ends, cut to `max` characters. In place; returns the new length. An empty
// result means "use the default".
size_t clean(char* s, size_t max);

}
}
