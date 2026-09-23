// SquachWatch-CYD — CrowPanel 7 peripheral probe (bench only, -DCROWPANEL7_PERIPH_PROBE).
// Runs once at the end of setup(), radios up: contiguous internal RAM, the
// PCF8563 clock, the SD slot (raw CMD0, then a mount), the STC8 buzzer, and
// a WiFi scan through the sniffer's own driver.
#pragma once
void crowPeriphProbe();
