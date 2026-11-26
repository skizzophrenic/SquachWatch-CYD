// SquachWatch-CM0 -- the firmware's FreeRTOS include, on one shim.
//
// The engine's files ask for the ESP32's headers by name (firmware/src/
// lora_enrich.cpp, lora_feed.cpp, lora_sniffer.cpp); the CM0's pthreads
// shim is the single header the force-include already carries
// (../freertos_shim.h, via arduino_cm0.h), and is #pragma once, so this
// forwarder costs nothing.
#pragma once
#include "../freertos_shim.h"
