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

#define HardwareSerial SimHardwareSerial
#define Serial1        SimSerial1
#define Serial2        SimSerial2
#include "Arduino.h"   // the sim's (-I$(SIM)); #pragma once there
#undef HardwareSerial
#undef Serial1
#undef Serial2

#include "gps_serial.h"
#include "freertos_shim.h"   // sound.cpp's player task, on pthreads

// The ESP32 core's pull-down mode, which the sim's Arduino.h does not
// define. Only the firmware's EXTID dev command uses it (main.cpp,
// SQW_DEVTOOLS; unreachable here -- no dev console input -- but compiled).
#ifndef INPUT_PULLDOWN
#define INPUT_PULLDOWN 0x09
#endif
