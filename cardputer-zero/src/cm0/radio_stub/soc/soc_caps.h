// SquachWatch-CM0 -- stub of ESP-IDF's soc_caps.h.
//
// The firmware's 5G gate reads one symbol from it (SOC_WIFI_SUPPORT_5G,
// detection.cpp's static_assert). True here means "this build's sniffer
// may have 5 GHz"; the adapter's actual capability is probed at boot
// (wifiMonHasFiveGhz) and that is what the channel probe reports.
#ifndef SQW_STUB_SOC_CAPS_H
#define SQW_STUB_SOC_CAPS_H
#define SOC_WIFI_SUPPORT_5G 1
#endif
