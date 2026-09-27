// SquachWatch-CYD — the SX1262 in the CrowPanel 7's wireless slot.
//
// A thin wrapper over RadioLib that keeps RadioLib's types out of every other
// file. One caller at a time: the sniffer's own task (lora_sniffer.cpp) owns
// the SPI bus and is the only thing that calls in here after begin().
#pragma once
#include <stdint.h>
#include "lora_pkt.h"

namespace LoraRadio {

// What bring-up found, for DIAGNOSTICS and the serial log.
struct BringUp {
    bool     busyLow;       // BUSY fell after a reset pulse: something is powered on the slot
    bool     chipFound;     // the version register read "SX126x"
    bool     ok;            // RadioLib is up and the modem answers
    char     version[17];   // the chip's own version string, or what the bus returned
    uint8_t  tcxoDeci;      // the DIO3 voltage that worked, in tenths of a volt; 0 = crystal
    int16_t  err;           // the last RadioLib error code, 0 when ok
    uint16_t devErrors;     // the chip's own error word after bring-up
};

// Probes the slot without RadioLib first (a reset pulse, then the version
// register), so a K1 on the card position costs milliseconds and not
// RadioLib's ten retries. Then brings the modem up, trying the TCXO voltages
// docs/LORA.md lists. False when nothing answered; r says how far it got.
bool begin(BringUp& r);
const BringUp& status();

// Reconfigure the modem for a profile. Standby first; the caller restarts
// reception afterwards. Frequency jumps beyond the last image calibration's
// reach recalibrate on the way.
bool apply(const Lora::Profile& p);

// Continuous reception, DIO1 on every event below.
bool startReceive();
// Reception with a timeout, in milliseconds (at most ~262 s).
bool startReceiveFor(uint32_t ms);
// A CAD of `symbols` symbols (2, 4 or 8). With gotoRx, the chip drops into
// reception by itself on a hit, for at most rxMs.
bool startCad(uint8_t symbols, bool gotoRx, uint32_t rxMs);
void standby();

// The chip's raw IRQ word, and clearing some of it. Bits as in irq().
enum Irq : uint16_t {
    IRQ_RX_DONE   = 0x0002,
    IRQ_PREAMBLE  = 0x0004,
    IRQ_HDR_VALID = 0x0010,
    IRQ_HDR_ERR   = 0x0020,
    IRQ_CRC_ERR   = 0x0040,
    IRQ_CAD_DONE  = 0x0080,
    IRQ_CAD_HIT   = 0x0100,
    IRQ_TIMEOUT   = 0x0200,
};
uint16_t irq();
void     clearIrq(uint16_t bits);
// Blocks the calling task until DIO1 fires or ms pass. True on the IRQ.
bool waitIrq(uint32_t ms);

// After IRQ_RX_DONE: the bytes, the signal, the header. Fills everything but
// ms/epoch/profile/proto; the caller has those. False if the read failed.
bool readPacket(Lora::Packet& pk);

// The instantaneous RSSI, for the noise floor and the spectrum sweep.
int16_t rssiNow();
// Tune only (no modem reconfiguration), for a sweep; the modem stays in RX.
bool tuneHz(uint32_t hz);

}
