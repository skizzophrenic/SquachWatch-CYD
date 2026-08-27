// SquachWatch-CYD — optional SD card event log
// If the SD card is mounted at boot, each Detection is appended to
// /squachwatch-YYYYMMDD.log as one CSV line.
// If the card is absent, every call is a silent no-op.
#pragma once
#include <Arduino.h>
#include "state.h"

class SdLog {
public:
    bool begin();              // returns true if card mounted
    bool ready() const { return _ready; }
    void logEvent(const Detection& d);
    void tick();               // flush / housekeeping (called from loop)
private:
    bool     _ready = false;
    uint32_t _lastFlush = 0;
    char     _filename[24] = {0};
    void     openDaily();
};
