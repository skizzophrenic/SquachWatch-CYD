// SquachWatch-CM0 -- Linux SX1262 backend for the `LoraRadio` shim.
//
// The firmware owns the UI/detection side; this file owns the Cap
// LoRa-1262 on the board's cap-bus. It replicates, not copies, the
// pocket-lorascan app's bring-up recipe (the one verified on this exact
// hardware): enable ext_5v_out, drive the PI4IOE5V6408 I2C expander at
// 0x43 (out|0x01, hiz&0xFE, dr|0x01), reset GPIO26, poll BUSY GPIO22, read
// IRQ GPIO23, and speak to the SX1262 over /dev/spidev0.1 (1 MHz, mode 0).
//
// The GPS path on GPIO14/15 (lines 14/15 on gpiochip0) is never touched.
//
// Unlike the ESP32 backend this is single-threaded: the firmware's
// `waitIrq()` is a poll loop, not a semaphore; `busHold()`/`busRelease()`
// are no-ops. One caller at a time, as in lora_radio.h.
#pragma once

#include "lora_radio.h"   // the shim contract; make -I firmware/include

namespace LoraRadio {
// Teardown: let the chip out of reset, release the GPIO lines, close the
// SPI fd. begin() calls it at the top and on every failure path, so a retry
// after a failed bring-up starts clean. The firmware never calls it.
void resetBus();
}
