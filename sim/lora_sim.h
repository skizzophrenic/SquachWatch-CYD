// SquachWatch-CYD PC emulator — the LoRa radio's stand-in, for the LORA page.
//
// sim/lora_sim.cpp defines the Lora:: calls src/ui_lorahome.cpp makes, the
// way sim/detection_sim.cpp stands in for the detection engine: no radio, a
// made-up band driven by the emulator's clock. See lora_sim.cpp for what is
// on the air and why every name and place in it is the one it is.
#pragma once
#include <stdint.h>

namespace SimLora {

struct Config {
    // When the capture starts, in millis(). The band is laid out around it:
    // a packet lands 0.7 s into every capture, and the chats' ages and the
    // nodes' last-heard times count back from it.
    uint32_t captureAt = 0;
    // No packets for this many seconds before the capture, and none during
    // it: the quiet band, where the SPECTRUM twinkles brighter. 0 = busy.
    uint16_t quietSecs = 0;
    // Which network the packet that lands in the capture is on, so the
    // open tab gets its new line: 0 Meshtastic, 1 MeshCore.
    uint8_t  net = 0;
    // The chats' "N NEW" badge.
    uint16_t unread = 0;
};

void begin(const Config& c);
// Every frame, before the page draws: keeps the GPS fix fresh.
void step(uint32_t now);

}
