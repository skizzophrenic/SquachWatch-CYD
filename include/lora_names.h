// SquachWatch-CYD — the Meshtastic names this board has learned, kept.
//
// A Meshtastic text frame names its sender only by node number; the name
// arrives in a NodeInfo, which a node broadcasts when it starts and then only
// every few hours. Without a memory, every restart puts the chat back to
// "!3b1c9a2e" until each node happens to announce itself again. This is that
// memory: node number -> short name and long name, the newest few dozen,
// written to flash now and then by the sniffer (see lora_sniffer.cpp) and
// read back at boot.
//
// Standalone, for the host tests: no Arduino, no flash. The sniffer does the
// saving with save()/load() and calls everything here under its own lock.
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace Lora {
namespace Names {

static const uint8_t CAP = 48;

// Remember a node's names. An empty string leaves that half as it was. True
// when anything changed (and the store is then dirty).
bool learn(uint32_t num, const char* tag, const char* name);
// What is known about a node. False when nothing is.
bool lookup(uint32_t num, char* tag, size_t tagCap, char* name, size_t nameCap);

uint8_t count();
void    clear();

// Whether there is anything unsaved, and marking it saved.
bool dirty();
void markSaved();

// A compact image of the store for flash, and back. save() returns the bytes
// written (0 if cap is too small); load() refuses anything it does not
// recognise and leaves the store empty rather than half-filled.
size_t bytesNeeded();
size_t save(uint8_t* out, size_t cap);
bool   load(const uint8_t* in, size_t len);

}
}
