// SquachWatch-CYD — firmware updates over Bluetooth.
//
// WHAT THIS IS. A GATT service the flasher website talks to from a browser
// (Web Bluetooth), streaming a new firmware image into the spare app slot while
// the current one keeps running. When the whole image has arrived and its
// signature checks out, the bootloader is pointed at the new slot and the board
// restarts into it. If anything fails, nothing is switched and the board stays
// exactly where it was.
//
// WHY IT CANNOT RUN ALL THE TIME. Update mode stops BLE scanning and mesh
// advertising for its duration. Two reasons, both measured rather than assumed:
// the scanner's per-advertisement allocations fragment the heap, and a GATT
// connection plus a 4 KB write buffer needs a contiguous block that a busy
// scanner will not leave behind. So this is a mode you enter deliberately from
// the menu, not a service that sits waiting.
//
// WHAT MAKES IT SAFE. Three things, in this order:
//   1. The board must be UNLOCKED and somebody must confirm on the screen. A
//      PIN-locked board never accepts an update.
//   2. The image is hashed as it streams, and the hash must match a signature
//      made with the project's private key (held as a GitHub Actions secret).
//      The matching public key is compiled in below. An image built by anybody
//      else is refused after the last byte and never booted.
//   3. The ESP32's own rollback: if the new image fails to mark itself valid on
//      first boot, the bootloader falls back to the slot that was working.
//
// Only builds carrying the two-slot layout can be updated this way -- see
// partitions_ota.csv for why the first one has to arrive over USB.
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace OtaBle {

// Where an update has got to. Drives the on-screen progress and nothing else --
// the transfer itself is driven by BLE callbacks, not by the UI.
enum class State : uint8_t {
    OFF = 0,        // not in update mode; the radio belongs to detection
    WAITING,        // advertising, nobody connected yet
    CONNECTED,      // a browser is attached but has not started sending
    RECEIVING,      // bytes arriving
    VERIFYING,      // last byte in, checking the signature
    DONE,           // verified and staged; the board restarts shortly
    FAILED,         // see failureText(); nothing was changed
};

// Enter and leave update mode. begin() stops scanning and starts advertising
// the update service; end() puts the radio back the way it was. Entering while
// the device is locked is refused -- see Security::locked().
bool begin();
void end();

void tick(uint32_t now);      // called from loop() while state() != OFF

State       state();
uint8_t     percent();        // 0..100, meaningful while RECEIVING
uint32_t    bytesReceived();
uint32_t    bytesExpected();
const char* failureText();    // why it FAILED, in words a person can act on

// The six-digit number shown on screen while WAITING. The browser must send it
// before the device accepts a single byte, which is what stops a stranger in
// range pushing an image at a board sitting in update mode. Regenerated every
// time update mode is entered.
uint32_t pairingCode();

}  // namespace OtaBle
