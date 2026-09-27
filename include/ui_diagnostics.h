// SquachWatch-CYD — on-device diagnostics screen
// Reached from Settings. Surfaces exactly the things that otherwise
// only a serial cable and a live monitor session can show: live raw
// touch readings, what calibration is actually active, free heap, and
// why the board last reset. Plain text, no animation, no Squachy --
// this is a maintenance tool, not part of the normal experience.
#pragma once
#include <TFT_eSPI.h>
#include <stdint.h>
#include "detection.h"
#include "blackbox.h"

// Filled in by main.cpp each tick, since gathering these values means
// touching board-specific globals (which raw touch reader, which
// calibration storage) that only main.cpp already has in scope --
// this file just formats and draws whatever it's handed.
// What the board was doing the last time it died.
//
// A panic reset tells you THAT it crashed and nothing else, which is what
// made the first one useless: it happened during a drive, on a build with no
// SquachMesh in it, and the only thing recoverable afterwards was the word
// PANIC. Whether the heap was exhausted, which screen was up, how long it had
// been running -- all gone.
//
// RTC memory survives a software reset (a panic is one) but not a power
// cycle, so a breadcrumb written there each second costs no flash wear and is
// readable on the next boot. It is not a backtrace, but "died at 41 minutes
// with 900 bytes of largest block, on CLEAR" and "died at 3 minutes with a
// healthy heap" are different bugs, and this tells them apart.
struct CrashReport {
    bool     valid;        // a panic boot AND a breadcrumb that looks sane
    uint32_t uptimeMs;     // how long it had been up
    uint32_t heapFree;
    uint32_t heapBlock;    // largest contiguous -- the fragmentation canary
    uint32_t lifetime;     // detections seen, as a proxy for RF churn
    uint8_t  screen;       // AppState it was on

    // From the core dump in flash -- only after a reset that writes one. The
    // task and the addresses are what addr2line needs, against the ELF of the
    // firmware that crashed, to name the function from a photo of the screen.
    bool     haveDump;
    bool     dumpOlder;    // written by other firmware than is running now
    char     task[16];
    uint32_t pc;
    uint32_t cause, vaddr; // the Xtensa exception cause and the address it hit
    uint8_t  btN;
    uint32_t bt[4];        // the frames above pc, nearest first
};

struct DiagnosticsInfo {
    CrashReport crash;

    // Live touch, straight from the touch chip -- not run through
    // calibration. Every board has a raw reading since the five-target
    // calibration; hasRaw stays for the emulator, which has no chip.
    bool    hasRaw;
    bool    rawTouching;
    int16_t rawA, rawB;

    // The same touch, mapped through whatever calibration is active
    // right now -- this is what the rest of the app actually sees.
    bool touchValid;
    int  mappedX, mappedY;

    // Calibration source/values currently in effect. calSource names it;
    // when null, usingSavedCal picks between "saved" and the default.
    bool    usingSavedCal;
    const char* calSource = nullptr;
    int16_t calA0, calA1, calB0, calB1;

    // Frame timing, exponentially smoothed in main.cpp. pushUs is the
    // SPI cost of getting the sprite onto the panel; frameUs is the
    // whole loop() iteration, so frameUs - pushUs is the drawing work.
    // Both in microseconds.
    uint32_t    pushUs;
    uint32_t    frameUs;
    // Cost of the animated backdrop alone, carried over from the last
    // screen that drew one -- this screen does not. Without it FRAME
    // here only ever describes the diagnostics screen.
    uint32_t    bgUs;
    // The whole frame of the last full screen shown (MAIN, LOG, DESK and the
    // like), averaged on that screen alone. nullptr until there has been one.
    const char* lastScreenName;
    uint32_t    lastScreenUs;
    // The black box: sightings kept in flash since the last CLR, crashes
    // kept, and the newest of those. bbReady false when the space is not ours.
    bool     bbReady;
    uint16_t bbKept;
    uint8_t  bbCrashes;
    bool     bbHaveLast;
    BlackBox::BootRecord bbLast;

    // System.
    uint32_t    freeHeap;
    uint32_t    largestBlock;
    // ...and the same pair at the first pass of loop(), for the BOOT line.
    uint32_t    loopFree;
    uint32_t    loopLargest;
    const char* resetReason;
    const char* boardName;
    // Which app slot is running, and the version in the other one (nullptr
    // when there is nothing there this board could switch to).
    const char* otaSlot;
    const char* otaOther;
    bool        usingCapTouch;
    // The wireless slot, on the board that has one: the chip, the
    // oscillator, the mode. Empty elsewhere, and the line is skipped.
    char        lora[80];
};

void uiDiagnosticsInit(TFT_eSPI& t);
void uiDiagnosticsTick(TFT_eSPI& t, uint32_t now, const DetectionEngine& eng, const DiagnosticsInfo& info);

// Single [ BACK ] button, same shape as the raw-scan screen's.
bool uiDiagnosticsHitBack(int x, int y, int screenW, int screenH);
