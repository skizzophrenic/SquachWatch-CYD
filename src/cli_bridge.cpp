// cli_bridge.cpp — Wires CLI commands to SquatchWatch toolbox functions
// Every tool accessible from serial command line.
#include "cli.h"
#include "ble_tx_power.h"
#include "ui_toolbox.h"
#include "badusb_payloads.h"
#include "mesh.h"   // real cyd_mesh_t + sender prototypes (see note below)
#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <NimBLEDevice.h>

// External references to toolbox state (declared in ui_toolbox.cpp)
extern int s_scanCount;
struct ScanResult { char ssid[33]; int32_t rssi; uint8_t channel; uint8_t encType; uint8_t bssid[6]; };
extern ScanResult s_scanResults[];

// ---- WiFi callbacks ----
static void cb_wifi_scan_start() {
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    Serial.println("[OK] WiFi scan started — results will print when done");
    int n = WiFi.scanNetworks(false, true);  // BLOCKING — waits for results
    if (n > 0) {
        Serial.printf("[DATA] %d networks found:\n", n);
        for (int i = 0; i < n && i < 40; i++) {
            Serial.printf("[DATA] %2d  %-32s  %4ddBm  CH%2d  %s  %s\n",
                i, WiFi.SSID(i).c_str(), WiFi.RSSI(i), WiFi.channel(i),
                WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "OPEN" : "ENC",
                WiFi.BSSIDstr(i).c_str());
        }
    } else {
        Serial.printf("[DATA] Scan returned %d\n", n);
    }
}

static void cb_wifi_scan_stop() {
    WiFi.scanDelete();
    Serial.println("[OK] WiFi scan stopped");
}

static void cb_wifi_list() {
    int n = WiFi.scanComplete();
    if (n == WIFI_SCAN_RUNNING) {
        Serial.println("[DATA] Scan in progress...");
        return;
    }
    if (n <= 0) {
        Serial.printf("[DATA] %d networks\n", n);
        return;
    }
    Serial.printf("[DATA] %d networks:\n", n);
    for (int i = 0; i < n && i < 40; i++) {
        Serial.printf("[DATA] %2d  %-32s  %4ddBm  CH%2d  %s  %s\n",
            i, WiFi.SSID(i).c_str(), WiFi.RSSI(i), WiFi.channel(i),
            WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "OPEN" : "ENC",
            WiFi.BSSIDstr(i).c_str());
    }
}

// ---- BLE callbacks ----
static void cb_ble_scan_start() {
    Serial.println("[OK] BLE scan — use toolbox for results");
}

static void cb_ble_scan_stop() {
    Serial.println("[OK] BLE scan stopped");
}

static void cb_ble_list() {
    Serial.println("[DATA] BLE list — use toolbox for results");
}

// ---- WiChat callbacks ----
// g_mesh (cyd_mesh_t) and the cyd_mesh_send_* prototypes come from mesh.h.
// A previous hand-rolled `extern struct { ... } g_mesh;` here declared a
// DIFFERENT layout than the real cyd_mesh_t, so writes to .channel/.callsign
// silently corrupted the mesh dedup ring buffer. Always use the header.

static void cb_chat_send(const char *msg) {
    cyd_mesh_send_chat(msg);
    Serial.printf("[OK] Sent: %s\n", msg);
}

static void cb_chat_ping() {
    cyd_mesh_send_ping();
    Serial.println("[OK] Ping sent");
}

static void cb_chat_nodes() {
    Serial.printf("[DATA] %d nodes on CH%d\n", g_mesh.node_count, g_mesh.channel);
}

static void cb_chat_set_channel(int ch) {
    if (ch < 1 || ch > 14) { Serial.println("[ERR] Channel 1-14"); return; }
    g_mesh.channel = ch;
    esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
    Serial.printf("[OK] Channel set to %d\n", ch);
}

static void cb_chat_set_name(const char *name) {
    strncpy(g_mesh.callsign, name, sizeof(g_mesh.callsign) - 1);
    g_mesh.callsign[sizeof(g_mesh.callsign) - 1] = '\0';
    Serial.printf("[OK] Name set to %s\n", g_mesh.callsign);
}

static void cb_chat_status() {
    Serial.printf("[DATA] Callsign: %s  Channel: %d  Nodes: %d  Messages: %d\n",
        g_mesh.callsign, g_mesh.channel, g_mesh.node_count, g_mesh.msg_count);
}

// ---- HID callbacks ----
#if defined(CONFIG_IDF_TARGET_ESP32S3)
#include <USB.h>
#include <USBHIDKeyboard.h>
extern USBHIDKeyboard s_globalKb;
static void cb_hid_run() { Serial.println("[OK] Use 'hid payload <os> <name>'"); }
static void cb_hid_type(const char *text) {
    s_globalKb.begin(); delay(100); s_globalKb.print(text);
    Serial.printf("[OK] Typed: %s\n", text);
}
static void cb_hid_key(uint8_t keycode) {
    s_globalKb.begin(); s_globalKb.press(keycode); delay(30);
    s_globalKb.releaseAll();
    Serial.printf("[OK] Key sent: 0x%02X\n", keycode);
}
#else
static void cb_hid_run() { Serial.println("[ERR] USB HID not available on this chip"); }
static void cb_hid_type(const char *) { Serial.println("[ERR] USB HID not available"); }
static void cb_hid_key(uint8_t) { Serial.println("[ERR] USB HID not available"); }
#endif

// ---- System callbacks ----
static void cb_set_brightness(int level) {
    // Backlight PWM is not wired through the CLI bridge. Report honestly
    // rather than claiming success for a no-op.
    Serial.printf("[ERR] brightness not supported via CLI (requested %d)\n", level);
}

static void cb_reboot() {
    Serial.println("[OK] Rebooting...");
    delay(100);
    ESP.restart();
}

// ---- System: max power at boot ----
static void maxRadioPower() {
    esp_wifi_set_max_tx_power(80);  // +20 dBm WiFi
    NimBLEDevice::setPower(SQW_BLE_MAX_PWR);  // +21 dBm BLE
    Serial.println("[OK] Radio power: WiFi +20dBm, BLE +21dBm");
}

// ---- Registration ----
void cli_bridge_init() {
    static cli_callbacks_t cbs;
    memset(&cbs, 0, sizeof(cbs));

    cbs.wifi_scan_start = cb_wifi_scan_start;
    cbs.wifi_scan_stop = cb_wifi_scan_stop;
    cbs.wifi_list = cb_wifi_list;
    cbs.ble_scan_start = cb_ble_scan_start;
    cbs.ble_scan_stop = cb_ble_scan_stop;
    cbs.ble_list = cb_ble_list;
    cbs.chat_send = cb_chat_send;
    cbs.chat_ping = cb_chat_ping;
    cbs.chat_nodes = cb_chat_nodes;
    cbs.chat_set_channel = cb_chat_set_channel;
    cbs.chat_set_name = cb_chat_set_name;
    cbs.chat_status = cb_chat_status;
    cbs.hid_run = cb_hid_run;
    cbs.hid_type = cb_hid_type;
    cbs.hid_key = cb_hid_key;
    cbs.set_brightness = cb_set_brightness;
    cbs.reboot = cb_reboot;

    cli_set_callbacks(&cbs);

    // Max radio power at boot
    maxRadioPower();
}

// =============================================================================
// Tool executor — every attack tool accessible from CLI
// =============================================================================
// These are declared in ui_toolbox.cpp — we call them directly
extern void wifiScanStart();
extern int s_scanCount;

// Tool state helpers
extern void uiToolboxStopTool();
extern bool uiToolboxToolRunning();

void cli_tool_exec(const char* cmd) {
    // --- Stop ---
    if (strcmp(cmd, "stop") == 0) {
        if (uiToolboxToolRunning()) {
            uiToolboxStopTool();
            Serial.println("[OK] Tool stopped");
        } else {
            Serial.println("[OK] Nothing running");
        }
        return;
    }

    // --- Deauth ---
    if (strncmp(cmd, "deauth ", 7) == 0) {
        int idx = atoi(cmd + 7);
        if (s_scanCount == 0) {
            Serial.println("[ERR] No scan results. Run 'scan wifi' first");
        } else if (idx < 0 || idx >= s_scanCount) {
            Serial.printf("[ERR] Invalid index %d (0-%d)\n", idx, s_scanCount - 1);
        } else {
            // Start deauth via toolbox
            extern void deauthStart(int);
            deauthStart(idx);
            Serial.printf("[OK] Deauth started on network #%d\n", idx);
        }
        return;
    }

    // --- Capture Handshake ---
    if (strncmp(cmd, "capture handshake ", 18) == 0) {
        int idx = atoi(cmd + 18);
        if (s_scanCount == 0) {
            Serial.println("[ERR] No scan results. Run 'scan wifi' first");
        } else if (idx < 0 || idx >= s_scanCount) {
            Serial.printf("[ERR] Invalid index %d\n", idx);
        } else {
            extern void handshakeCaptureStart(int);
            handshakeCaptureStart(idx);
            Serial.printf("[OK] Handshake capture started on #%d\n", idx);
        }
        return;
    }

    // --- Capture PCAP ---
    if (strcmp(cmd, "capture pcap") == 0) {
        extern void pcapCaptureStart();
        pcapCaptureStart();
        Serial.println("[OK] PCAP capture started");
        return;
    }

    // --- Capture Probe ---
    if (strcmp(cmd, "capture probe") == 0) {
        extern void probeSniffStart();
        probeSniffStart();
        Serial.println("[OK] Probe sniff started");
        return;
    }

    // --- Clone AP ---
    if (strncmp(cmd, "clone ", 6) == 0) {
        int idx = atoi(cmd + 6);
        if (s_scanCount == 0) {
            Serial.println("[ERR] No scan results. Run 'scan wifi' first");
        } else {
            extern void cloneApStart(int);
            cloneApStart(idx >= 0 && idx < s_scanCount ? idx : 0);
            Serial.printf("[OK] Clone AP started for #%d\n", idx);
        }
        return;
    }

    // --- Evil Portal ---
    if (strcmp(cmd, "evil portal start") == 0) {
        extern void evilPortalStart();
        evilPortalStart();
        Serial.println("[OK] Evil portal started");
        return;
    }
    if (strcmp(cmd, "evil portal stop") == 0) {
        extern void evilPortalStop();
        evilPortalStop();
        Serial.println("[OK] Evil portal stopped");
        return;
    }

    // --- Karma ---
    if (strcmp(cmd, "karma start") == 0) {
        extern void karmaStart();
        karmaStart();
        Serial.println("[OK] Karma started");
        return;
    }
    if (strcmp(cmd, "karma stop") == 0) {
        extern void karmaStop();
        karmaStop();
        Serial.println("[OK] Karma stopped");
        return;
    }

    // --- DHCP Starve ---
    if (strcmp(cmd, "dhcp starve") == 0) {
        extern void dhcpStarveStart();
        dhcpStarveStart();
        Serial.println("[OK] DHCP starve started");
        return;
    }

    // --- Spam Beacon ---
    if (strcmp(cmd, "spam beacon") == 0) {
        extern void beaconSpamStart();
        beaconSpamStart();
        Serial.println("[OK] Beacon spam started");
        return;
    }

    // --- Spam Rickroll ---
    if (strcmp(cmd, "spam rickroll") == 0) {
        extern void rickrollStart();
        rickrollStart();
        Serial.println("[OK] Rickroll started");
        return;
    }

    // --- Spam Apple ---
    if (strcmp(cmd, "spam apple") == 0) {
        extern void appleSpamStart();
        appleSpamStart();
        Serial.println("[OK] Apple BLE spam started");
        return;
    }

    // --- Spam Samsung ---
    if (strcmp(cmd, "spam samsung") == 0) {
        extern void samsungSpamStart();
        samsungSpamStart();
        Serial.println("[OK] Samsung BLE spam started");
        return;
    }

    // --- Spam SwiftPair ---
    if (strcmp(cmd, "spam swiftpair") == 0) {
        extern void swiftPairStart();
        swiftPairStart();
        Serial.println("[OK] SwiftPair spam started");
        return;
    }

    // --- Flipper Scan ---
    if (strcmp(cmd, "flipper scan") == 0) {
        extern void flipperDetectStart();
        flipperDetectStart();
        Serial.println("[OK] Flipper detect started");
        return;
    }

    // --- AirTag Spoof ---
    if (strcmp(cmd, "airtag spoof") == 0) {
        extern void airtagSpoofStart();
        airtagSpoofStart();
        Serial.println("[OK] AirTag spoof started");
        return;
    }

    // --- Haystack ---
    if (strcmp(cmd, "haystack start") == 0) {
        extern void haystackStart();
        haystackStart();
        Serial.println("[OK] Haystack beacon started");
        return;
    }
    if (strcmp(cmd, "haystack stop") == 0) {
        uiToolboxStopTool();
        Serial.println("[OK] Haystack stopped");
        return;
    }

    // --- LAN Scan ---
    if (strcmp(cmd, "lan scan") == 0) {
        extern void wifiNetScanStart();
        wifiNetScanStart();
        Serial.println("[OK] LAN scan started");
        return;
    }

    // --- BadUSB ---
    if (strncmp(cmd, "badusb ", 7) == 0) {
        extern void executePayload(const char*);
        extern String payloadWithIP(const char*, const char*);
        const char* args = cmd + 7;
        // Parse: badusb <os> <payload> [ip]
        // os: windows|mac|linux
        // payload: wifi|sysinfo|revshell|admin|antivirus|download|browser|persist
        int os = -1;
        if (strncmp(args, "windows ", 8) == 0) { os = 0; args += 8; }
        else if (strncmp(args, "mac ", 4) == 0) { os = 1; args += 4; }
        else if (strncmp(args, "linux ", 6) == 0) { os = 2; args += 6; }
        else { Serial.println("[ERR] badusb <windows|mac|linux> <payload>"); return; }

        int payload = -1;
        if (strncmp(args, "wifi", 4) == 0) payload = 0;
        else if (strncmp(args, "sysinfo", 7) == 0) payload = 1;
        else if (strncmp(args, "revshell", 8) == 0) payload = 2;
        else if (strncmp(args, "admin", 5) == 0) payload = 3;
        else if (strncmp(args, "antivirus", 9) == 0) payload = 4;
        else if (strncmp(args, "download", 8) == 0) payload = 5;
        else if (strncmp(args, "browser", 7) == 0) payload = 6;
        else if (strncmp(args, "persist", 7) == 0) payload = 7;
        else { Serial.println("[ERR] payload: wifi|sysinfo|revshell|admin|antivirus|download|browser|persist"); return; }

        // PAYLOAD_TABLE from badusb_payloads.h (included at top of file)
        const char* script = PAYLOAD_TABLE[os][payload];
        Serial.printf("[OK] Running %s payload %d...\n", os==0?"Windows":os==1?"Mac":"Linux", payload);
        executePayload(script);
        Serial.println("[OK] Payload executed");
        return;
    }

    // --- Extended tools (tools_extended.cpp) ---
    if (strcmp(cmd, "channel analyze") == 0) {
        extern void channelAnalyzerStart();
        channelAnalyzerStart();
        return;
    }
    if (strcmp(cmd, "jam detect") == 0) {
        extern void jamDetectStart();
        jamDetectStart();
        return;
    }
    if (strcmp(cmd, "socks start") == 0) {
        extern void socksProxyStart();
        socksProxyStart();
        return;
    }
    if (strcmp(cmd, "socks stop") == 0) {
        extern void socksProxyStop();
        socksProxyStop();
        return;
    }
    if (strcmp(cmd, "netcut scan") == 0) {
        extern void netcutScan();
        netcutScan();
        return;
    }
    if (strncmp(cmd, "netcut poison ", 14) == 0) {
        extern void netcutPoison(int);
        netcutPoison(atoi(cmd + 14));
        return;
    }
    if (strncmp(cmd, "netcut restore ", 15) == 0) {
        extern void netcutRestore(int);
        netcutRestore(atoi(cmd + 15));
        return;
    }
    if (strcmp(cmd, "netcut stop") == 0) {
        extern void netcutStop();
        netcutStop();
        return;
    }

    // --- BLE extended tools ---
    if (strcmp(cmd, "badble start") == 0) { extern void badBleStart(); badBleStart(); return; }
    if (strcmp(cmd, "badble stop") == 0) { extern void badBleStop(); badBleStop(); return; }
    if (strcmp(cmd, "badble status") == 0) { extern void badBleStatus(); badBleStatus(); return; }
    if (strncmp(cmd, "badble type ", 12) == 0) { extern void badBleType(const char*); badBleType(cmd + 12); return; }
    if (strcmp(cmd, "ble sniff") == 0) { extern void bleSnifferRun(); bleSnifferRun(); return; }
    if (strncmp(cmd, "ninebot ", 8) == 0) {
        // ninebot <addr> <payload_index>
        char addr[18]; int payload = 0;
        if (sscanf(cmd + 8, "%17s %d", addr, &payload) >= 1) {
            extern void ninebotAttack(const char*, int);
            ninebotAttack(addr, payload);
        } else { Serial.println("[ERR] ninebot <ble_addr> [payload 0-4]"); }
        return;
    }
    if (strncmp(cmd, "hfp probe ", 10) == 0) { extern void hfpProbe(const char*); hfpProbe(cmd + 10); return; }

    Serial.printf("[ERR] Unknown tool command: '%s'\n", cmd);
}
