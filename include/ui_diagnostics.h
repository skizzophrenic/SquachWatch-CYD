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

// Filled in by main.cpp each tick, since gathering these values means
// touching board-specific globals (which raw touch reader, which
// calibration storage) that only main.cpp already has in scope --
// this file just formats and draws whatever it's handed.
struct DiagnosticsInfo {
    // Live touch, straight from the touch chip -- not run through
    // calibration. hasRaw is false on boards whose touch path only
    // ever hands back already-calibrated coordinates (AWOK/cyd35's
    // native TFT_eSPI touch), since there's no separate raw reading to
    // show there.
    bool    hasRaw;
    bool    rawTouching;
    int16_t rawA, rawB;

    // The same touch, mapped through whatever calibration is active
    // right now -- this is what the rest of the app actually sees.
    bool touchValid;
    int  mappedX, mappedY;

    // Calibration source/values currently in effect.
    bool    usingSavedCal;
    int16_t calA0, calA1, calB0, calB1;

    // Frame timing, exponentially smoothed in main.cpp. pushUs is the
    // SPI cost of getting the sprite onto the panel; frameUs is the
    // whole loop() iteration, so frameUs - pushUs is the drawing work.
    // Both in microseconds.
    uint32_t    pushUs;
    uint32_t    frameUs;

    // System.
    uint32_t    freeHeap;
    uint32_t    largestBlock;
    const char* resetReason;
    const char* boardName;
    bool        usingCapTouch;
};

void uiDiagnosticsInit(TFT_eSPI& t);
void uiDiagnosticsTick(TFT_eSPI& t, uint32_t now, const DetectionEngine& eng, const DiagnosticsInfo& info);

// Single [ BACK ] button, same shape as the raw-scan screen's.
bool uiDiagnosticsHitBack(int x, int y, int screenW, int screenH);
