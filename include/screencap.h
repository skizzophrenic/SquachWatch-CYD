// SquachWatch-CYD — serial screen capture
#pragma once
#include <TFT_eSPI.h>
#include <stdint.h>

namespace ScreenCap {
    // Watches the serial port for a capture request and, when one
    // arrives, emits a framed snapshot of `fb` back over that same
    // port. Call once per loop() iteration, right after the frame has
    // been pushed to the panel, so the buffer holds a complete frame
    // rather than one caught mid-draw.
    //
    // Request  (PC -> device): the ASCII bytes "SQCAP\n".
    // Response (device -> PC): a 14-byte header, the pixel payload,
    // then a 16-bit checksum:
    //
    //   "SQFB"    4  magic -- this port also carries ordinary
    //                Serial.println() debug text, so the PC side needs
    //                something unambiguous to scan for
    //   version   1  protocol version, currently 1
    //   format    1  0 = raw RGB565, one uint16 per pixel, row-major
    //   width     2  little-endian
    //   height    2  little-endian
    //   length    4  little-endian payload byte count
    //   payload   n  `length` bytes
    //   checksum  2  little-endian sum of every payload byte, mod 65536
    //
    // The format byte is here from the start so run-length encoding can
    // be added later as format 1 -- the live PC mirror this is
    // groundwork for can't use raw frames (see the timing note below)
    // but shouldn't need a differently-shaped parser either.
    //
    // A capture blocks for as long as the write takes: a 320x240 raw
    // frame is 150KB, which at 2,000,000 baud is roughly 750ms of
    // visible pause on the device. That's an acceptable hitch for a
    // deliberate one-shot screenshot and exactly why the eventual
    // mirror needs the compressed format instead.
    //
    // KNOWN ISSUE: the pixels come through correctly, but the checksum
    // is unreliable. With no SD card inserted the SD driver logs
    // "Card Failed!" every few hundred ms from its own task, and those
    // ASCII lines splice into the middle of the binary payload --
    // shifting everything after them and putting the checksum bytes
    // where the reader isn't looking. It's ARDUHAL logging, gated at
    // compile time by CORE_DEBUG_LEVEL, so esp_log_level_set() can't
    // suppress it at runtime; the fix is either -DCORE_DEBUG_LEVEL=0 on
    // every board env (a bare [env] build_flags gets overridden by each
    // board's own) or not re-probing the absent SD card on a loop.
    // Until then, tools/screenshot.py --force writes the image anyway.
    //
    // fb may be null (boards with no full-screen sprite, or a failed
    // allocation) -- the request is answered with a plain-text error
    // rather than silence, so the PC side doesn't just hang.
    void poll(TFT_eSprite* fb, int w, int h);

    // True while a capture is mid-flight. The frame is streamed out of
    // the live sprite over many loop() iterations, so the caller has to
    // stop drawing into that buffer for the duration or the image tears
    // -- half of one frame stitched to half of the next. main.cpp's
    // loop() uses this to hold rendering still until the capture
    // finishes, which costs about a second of frozen UI per screenshot.
    bool busy();
}
