// SquachWatch-CM0 -- esp_random.h for the device build.
//
// Shadows sim/esp_random.h, whose esp_fill_random() is built on the sim's
// deterministic esp_random(). Both live in esp_system.h here, on the
// kernel CSPRNG -- see the comment there.
#pragma once
#include "esp_system.h"
