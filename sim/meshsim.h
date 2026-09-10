// SquachWatch-Sim — a virtual SquachMesh peer: another SquachWatch, in the
// room, that only exists in the emulator.
//
// It does not fake the result. It builds a real SquachMesh advert and a real
// message frame and hands them to Mesh::onManufacturerData() -- the same call a
// NimBLE scan callback makes on the device -- so everything downstream is the
// firmware's own code: the decoder, the one-visitor rule, the staleness
// timeout, the consent and DETECT gates, the replay table, the red bubble. If
// a visitor does not show up here, it would not show up on a board either,
// and the status says which switch is why.
//
// It also listens. With TRANSMIT on, whatever our scan response carries is
// what it hears, opened with the same frame code a receiving board uses, and
// it can answer.
//
// The cipher underneath is still the emulator's stand-in (meshcrypto_sim.cpp),
// so this proves the plumbing and the screens, never the cryptography. That
// is pinned on hardware by the boot self-test.
#pragma once
#if SQUACH_MESH
#include <stdint.h>

namespace MeshSim {

// One command, as typed by hand or sent by a panel:
//
//   on | off              the peer arrives, or walks away (the visit then
//                         ends on the firmware's own staleness timeout)
//   outfit N | shade N | nick N
//   name TEXT             a custom name, up to 12 characters; bare "name"
//                         clears it and the nickname comes back
//   phrase same|other     whether it is in your group
//   reply on|off          whether it answers what it hears
//   say N                 it sends canned line N
//   setup                 EMULATOR ONLY: accepts the warning, switches
//                         DETECT, TRANSMIT and MESSAGES on and sets a phrase
//   status | help
//
// Returns false for anything it did not understand, after logging why.
bool command(const char* line);

// One line of JSON: the peer's state and every switch that decides whether
// it can be seen, heard or read. Rebuilt on each call.
const char* status();

// One line of JSON: nicknames, outfits, shades and canned lines, for pickers.
const char* catalog();

// Called from Mesh::radioTick() every loop.
void tick(uint32_t now);

} // namespace MeshSim
#endif
