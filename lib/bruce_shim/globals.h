// Bruce shim — globals.h
// Maps Bruce's global state to SquatchWatch's environment.
#pragma once
#include <Arduino.h>
#include <TFT_eSPI.h>
#include <vector>
#include <functional>
#include <string>

// Forward declare the SquatchWatch canvas that Bruce modules draw to.
// NOTE: bruce_tft must be assigned to the live TFT_eSPI before any Bruce
// module renders, or the `tft` macro below dereferences null. The Bruce
// modules (bruce_modules/*) are not currently linked into any env — nothing
// in src/ includes them — so this is dormant. If they are ever wired into the
// UI, bind the canvas (bruce_tft = &display) at toolbox init first.
extern TFT_eSPI* bruce_tft;

// Shim: Bruce modules use `tft.` — we provide a reference
#define tft (*bruce_tft)

// Screen dimensions (landscape 480x320)
#define tftWidth  (bruce_tft->width())
#define tftHeight (bruce_tft->height())
#define WIDTH     tftWidth
#define HEIGHT    tftHeight

// Font sizes Bruce uses
#ifndef FP
#define FP 1
#endif
#ifndef FM
#define FM 2
#endif
#ifndef FG
#define FG 3
#endif

// Bruce config stub
struct BruceConfigWifiAp {
    String ssid = "BruceAP";
    String pwd = "";
};

struct EvilPortalEndpoints {
    bool allowGetCreds = true;
    bool allowSetSsid = true;
    bool showEndpoints = true;
    String getCredsEndpoint = "/get-creds";
    String setSsidEndpoint = "/set-ssid";
};

struct BruceConfig {
    uint16_t priColor = 0x07FF;   // cyan
    uint16_t bgColor  = 0x0000;   // black
    uint16_t secColor = 0xF800;   // red
    BruceConfigWifiAp wifiAp;
    EvilPortalEndpoints evilPortalEndpoints;
    String evilPortalGatewayIp = "172.0.0.1";
    std::vector<String> evilWifiNames;

    String getWifiPassword(const String&) { return ""; }
    void addWifiCredential(const String&, const String&) {}
};

struct BruceConfigPins {
    uint8_t rotation = 1;
};

extern BruceConfig bruceConfig;
extern BruceConfigPins bruceConfigPins;

// Input state — mapped from SquatchWatch touch in the toolbox tick
extern volatile bool AnyKeyPress;
extern volatile bool EscPress;
extern volatile bool SelPress;
extern volatile bool PrevPress;
extern volatile bool NextPress;
extern volatile bool LongPress;
extern volatile bool returnToMenu;

// Line height — Bruce uses LH (8 pixels) for layout
#ifndef LH
#define LH 8
#endif

#ifndef BTN_ALIAS
#define BTN_ALIAS "TAP"
#endif

#ifndef EEPROMSIZE
#define EEPROMSIZE 128
#endif

// padprintln — Bruce's println helper
inline void padprintln(const String &s) {
    if (bruce_tft) bruce_tft->println(s);
}


// check() macro Bruce uses
inline bool check(volatile bool& flag) {
    bool v = flag;
    flag = false;
    return v;
}

// Keyboard stub
struct TouchPoint {
    int16_t x = 0, y = 0;
    bool pressed = false;
};
extern TouchPoint touchPoint;

// Color helper Bruce uses
inline uint16_t getColorVariation(uint16_t color) { return color; }

// esp_iface_mac_addr_set — IDF 5.x API, provide fallback for IDF 4.x
#include <esp_wifi.h>
#ifndef ESP_IDF_VERSION_MAJOR
#define ESP_IDF_VERSION_MAJOR 4
#endif
#if ESP_IDF_VERSION_MAJOR < 5
#include <esp_system.h>
// IDF 4.x lacks esp_iface_mac_addr_set. Set the MAC on the requested
// interface rather than the global base MAC (esp_base_mac_addr_set only
// takes effect before esp_wifi_init and ignores the per-iface request).
inline esp_err_t esp_iface_mac_addr_set(const uint8_t *mac, esp_mac_type_t type) {
    wifi_interface_t iface = (type == ESP_MAC_WIFI_SOFTAP) ? WIFI_IF_AP : WIFI_IF_STA;
    return esp_wifi_set_mac(iface, mac);
}
#endif
