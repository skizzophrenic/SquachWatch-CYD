// SquachWatch-CM0 -- the force-included Arduino layer (Makefile: -include).
//
// The firmware's sim/Arduino.h, unchanged, except for one thing it cannot be
// told to leave out: its HardwareSerial (Serial1/Serial2) only replays an
// NMEA file. On the CM0 the GPS is a real UART, so the sim's class and its
// two globals are renamed while that header is read, and the CM0's own
// (gps_serial.h) take the real names. Every translation unit, firmware and
// harness alike, is compiled with this same file, so there is exactly one
// HardwareSerial in the program.
#pragma once
#include <cstdio>

#define HardwareSerial SimHardwareSerial
#define Serial1        SimSerial1
#define Serial2        SimSerial2
#define EspClass       SimEspClass
#define ESP            SimESP
#include "Arduino.h"   // the sim's (-I$(SIM)); #pragma once there
#undef HardwareSerial
#undef Serial1
#undef Serial2
#undef EspClass
#undef ESP

#include "gps_serial.h"
#include "freertos_shim.h"   // sound.cpp's player task, on pthreads

// ---- ESP runtime: the sim's EspClass (and the ESP global) was renamed
// while its header was read, as HardwareSerial was above, and this one
// takes the real names. It carries the sim's fixed diagnostic values
// unchanged, plus the two members only a board with a MAC and a restart
// has: the LORA engine's Meshtastic node number derives from
// getEfuseMac() (lora_sniffer.cpp, 0x5A5A0000 | mac & 0xFFFF) -- the
// built-in wifi's MAC -- and the LORA console's REGION command asks for a
// restart after saving the band plan.
struct EspClass {
    uint32_t getFreeHeap()    { return 180000; }
    uint32_t getHeapSize()    { return 327680; }
    uint32_t getPsramSize()   { return 0; }
    uint32_t getFreePsram()   { return 0; }
    const char* getChipModel(){ return "ESP32-SIM"; }
    uint8_t  getChipRevision(){ return 1; }
    uint32_t getCpuFreqMHz()  { return 240; }
    uint32_t getEfuseMac() {
        static const uint32_t mac = [] {
            unsigned o[6] = {0};
            if (FILE* f = std::fopen("/sys/class/net/wlan0/address", "r")) {
                if (std::fscanf(f, "%x:%x:%x:%x:%x:%x",
                        &o[0], &o[1], &o[2], &o[3], &o[4], &o[5]) != 6)
                    for (unsigned& v : o) v = 0;
                std::fclose(f);
            }
            if (o[0] == o[1] && o[1] == o[2] && o[2] == o[3]
               && o[3] == o[4] && o[4] == o[5])
                return 0x00A01Bu;   // all zero: a stable stand-in
            return (uint32_t(o[2]) << 24) | (uint32_t(o[3]) << 16)
                 | (uint32_t(o[4]) << 8) | o[5];
        }();
        return mac;
    }
    // The app is not the whole OS: nothing to reboot from here. The one
    // caller is the LORA console's REGION save (lora_sniffer.cpp), behind
    // the watch/T-Deck console block, so on the CM0 this only ever logs.
    void restart() {
        std::fprintf(stderr, "[esp] restart() on the CM0 is a no-op\n");
    }
};
inline EspClass ESP;

// The ESP32 core's pull-down mode, which the sim's Arduino.h does not
// define. Only the firmware's EXTID dev command uses it (main.cpp,
// SQW_DEVTOOLS; unreachable here -- no dev console input -- but compiled).
#ifndef INPUT_PULLDOWN
#define INPUT_PULLDOWN 0x09
#endif
