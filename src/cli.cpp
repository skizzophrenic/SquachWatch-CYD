/* cli.cpp -- Serial command interface for remote operation & test automation */
#include "cli.h"
#include "ble_tx_power.h"
#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <NimBLEDevice.h>
#include <string.h>
#include <stdlib.h>

#define CLI_BUF_SIZE 256
#define CLI_BATCH_MAX 32

static char cli_buf[CLI_BUF_SIZE];
static int cli_pos = 0;
static const cli_callbacks_t *cb = nullptr;

// Debug output suppression — when quiet, only [OK]/[ERR]/[DATA] pass through
bool cli_quiet_mode = false;

/* Batch mode */
static bool batch_mode = false;
static char *batch_cmds[CLI_BATCH_MAX];
static int batch_count = 0;

/* Simulated touch state */
static bool sim_touch_active = false;
static int16_t sim_touch_x = 0, sim_touch_y = 0;

static bool cli_exec(const char *line);

void cli_init(void) {
    cli_pos = 0;
    batch_mode = false;
    batch_count = 0;
    Serial.println("[OK] CLI ready. Type 'help' for commands.");
}

void cli_set_callbacks(const cli_callbacks_t *callbacks) {
    cb = callbacks;
}

// Handle one already-read command line. Toolbox-first: returns true if the
// toolbox CLI recognized and handled the line, false if it did not — in which
// case the single serial reader (Clock::pollSerial) falls back to the base
// command set. This replaces cli_poll's own Serial reader so the two parsers
// no longer race for the same bytes on the shared USB serial stream.
bool cli_try(const char *line) {
    if (batch_mode) {
        if (strcmp(line, "end") == 0) {
            /* Execute batch */
            batch_mode = false;
            Serial.printf("[OK] Executing %d commands\n", batch_count);
            for (int i = 0; i < batch_count; i++) {
                cli_exec(batch_cmds[i]);
                free(batch_cmds[i]);
            }
            batch_count = 0;
            Serial.println("[OK] Batch complete");
        } else if (batch_count < CLI_BATCH_MAX) {
            batch_cmds[batch_count++] = strdup(line);
        } else {
            Serial.println("[ERR] Batch full");
        }
        return true;   // batch mode consumes every line
    }
    return cli_exec(line);
}

void cli_poll(void) {
    // Serial input is read in exactly one place now (Clock::pollSerial), which
    // routes each line through cli_try() first. cli_poll only maintains the
    // injected-touch state used by the tap/hold CLI helpers.
    if (sim_touch_active && cb && cb->inject_touch) {
        cb->inject_touch(sim_touch_x, sim_touch_y);
    }
}

/* Parse helpers */
static const char *skip_word(const char *s) {
    while (*s && *s != ' ') s++;
    while (*s == ' ') s++;
    return s;
}

static int parse_int(const char *s) { return atoi(s); }

static void cmd_help(void) {
    Serial.println("[DATA] === SquatchWatch CLI ===");
    Serial.println("[DATA]");
    Serial.println("[DATA] --- Scanning ---");
    Serial.println("[DATA] scan wifi             Scan WiFi networks");
    Serial.println("[DATA] scan ble              Scan BLE devices");
    Serial.println("[DATA] flipper scan          Scan for Flipper Zeros");
    Serial.println("[DATA]");
    Serial.println("[DATA] --- WiFi Attacks ---");
    Serial.println("[DATA] deauth <n>            Deauth network #n");
    Serial.println("[DATA] capture handshake <n> Capture WPA handshake");
    Serial.println("[DATA] capture pcap          Capture all packets to SD");
    Serial.println("[DATA] capture probe         Sniff probe requests");
    Serial.println("[DATA] clone <n>             Clone AP (evil twin)");
    Serial.println("[DATA] evil portal start     Captive portal");
    Serial.println("[DATA] evil portal stop");
    Serial.println("[DATA] karma start           Auto-respond to probes");
    Serial.println("[DATA] karma stop");
    Serial.println("[DATA] dhcp starve           DHCP exhaustion");
    Serial.println("[DATA] spam beacon           Funny SSID spam");
    Serial.println("[DATA] spam rickroll         Rick Astley SSIDs");
    Serial.println("[DATA]");
    Serial.println("[DATA] --- BLE Attacks ---");
    Serial.println("[DATA] spam apple            Apple pairing popups");
    Serial.println("[DATA] spam samsung          Samsung pairing popups");
    Serial.println("[DATA] spam swiftpair        Windows pairing popups");
    Serial.println("[DATA] airtag spoof          Fake AirTag beacon");
    Serial.println("[DATA] badble start          BLE HID keyboard attack");
    Serial.println("[DATA] badble type <text>    Type on paired victim");
    Serial.println("[DATA] badble stop           Stop BadBLE");
    Serial.println("[DATA] ble sniff             Capture BLE advertisements");
    Serial.println("[DATA] ninebot <addr> [0-4]  Scooter unlock");
    Serial.println("[DATA] hfp probe <addr>      HFP vulnerability check");
    Serial.println("[DATA] haystack start        FindMy location beacon");
    Serial.println("[DATA] haystack stop");
    Serial.println("[DATA]");
    Serial.println("[DATA] --- Network ---");
    Serial.println("[DATA] connect <ssid> <pass> Join WiFi network");
    Serial.println("[DATA] disconnect            Leave WiFi");
    Serial.println("[DATA] status                Connection info");
    Serial.println("[DATA] lan scan              Scan LAN (need WiFi)");
    Serial.println("[DATA] channel analyze       WiFi channel utilization");
    Serial.println("[DATA] jam detect            Deauth/jamming detector");
    Serial.println("[DATA] netcut scan           ARP scan local network");
    Serial.println("[DATA] netcut poison <n>     ARP poison device #n");
    Serial.println("[DATA] netcut restore <n>    Restore device #n");
    Serial.println("[DATA] netcut stop           Restore all devices");
    Serial.println("[DATA] socks start           SOCKS4 proxy on :1080");
    Serial.println("[DATA] socks stop            Stop proxy");
    Serial.println("[DATA]");
    Serial.println("[DATA] --- BadUSB ---");
    Serial.println("[DATA] badusb <os> <payload> Run payload");
    Serial.println("[DATA]   os: windows|mac|linux");
    Serial.println("[DATA]   payload: wifi|sysinfo|revshell|admin|antivirus|download|browser|persist");
    Serial.println("[DATA] hid type <text>       Type text via USB");
    Serial.println("[DATA]");
    Serial.println("[DATA] --- WiChat ---");
    Serial.println("[DATA] chat send <msg>       Send mesh message");
    Serial.println("[DATA] chat ping             Ping mesh");
    Serial.println("[DATA] chat nodes            List mesh nodes");
    Serial.println("[DATA] chat channel <n>      Set channel");
    Serial.println("[DATA]");
    Serial.println("[DATA] --- System ---");
    Serial.println("[DATA] stop                  Stop active tool");
    Serial.println("[DATA] power                 Show radio power");
    Serial.println("[DATA] power max             Max WiFi+BLE power");
    Serial.println("[DATA] power wifi <dBm>      Set WiFi power");
    Serial.println("[DATA] heap                  Memory info");
    Serial.println("[DATA] uptime                Uptime");
    Serial.println("[DATA] reboot                Restart device");
    Serial.println("[DATA] quiet                 Suppress debug output");
    Serial.println("[DATA] verbose               Show all output");
    Serial.println("[DATA] help                  This help");
}

static void cmd_tap(const char *args) {
    int x = parse_int(args);
    const char *p = skip_word(args);
    int y = parse_int(p);
    if (cb && cb->inject_touch) {
        cb->inject_touch((int16_t)x, (int16_t)y);
        delay(100);
        if (cb->release_touch) cb->release_touch();
        Serial.printf("[OK] tap %d %d\n", x, y);
    } else {
        Serial.println("[ERR] Touch not available");
    }
}

static void cmd_hold(const char *args) {
    int x = parse_int(args);
    const char *p = skip_word(args);
    int y = parse_int(p);
    p = skip_word(p);
    int ms = parse_int(p);
    if (cb && cb->inject_touch) {
        sim_touch_active = true;
        sim_touch_x = (int16_t)x;
        sim_touch_y = (int16_t)y;
        cb->inject_touch(sim_touch_x, sim_touch_y);
        if (ms > 0) {
            delay(ms);
            sim_touch_active = false;
            if (cb->release_touch) cb->release_touch();
        }
        Serial.printf("[OK] hold %d %d %dms\n", x, y, ms);
    } else {
        Serial.println("[ERR] Touch not available");
    }
}

static void cmd_release(void) {
    sim_touch_active = false;
    if (cb && cb->release_touch) cb->release_touch();
    Serial.println("[OK] release");
}

static void cmd_swipe(const char *args) {
    int x1 = parse_int(args);
    const char *p = skip_word(args);
    int y1 = parse_int(p); p = skip_word(p);
    int x2 = parse_int(p); p = skip_word(p);
    int y2 = parse_int(p); p = skip_word(p);
    int ms = parse_int(p);
    if (ms < 50) ms = 200;

    if (cb && cb->inject_touch) {
        int steps = ms / 10;
        if (steps < 5) steps = 5;
        for (int i = 0; i <= steps; i++) {
            int16_t x = (int16_t)(x1 + (x2 - x1) * i / steps);
            int16_t y = (int16_t)(y1 + (y2 - y1) * i / steps);
            cb->inject_touch(x, y);
            delay(10);
        }
        if (cb->release_touch) cb->release_touch();
        Serial.printf("[OK] swipe %d,%d -> %d,%d in %dms\n", x1, y1, x2, y2, ms);
    } else {
        Serial.println("[ERR] Touch not available");
    }
}

static void cmd_screen(void) {
    int id = (cb && cb->get_screen_id) ? cb->get_screen_id() : -1;
    int w = (cb && cb->get_screen_width) ? cb->get_screen_width() : -1;
    int h = (cb && cb->get_screen_height) ? cb->get_screen_height() : -1;
    Serial.printf("[DATA] screen_id=%d width=%d height=%d\n", id, w, h);
}

static void cmd_heap(void) {
    Serial.printf("[DATA] free_heap=%d largest_block=%d",
                  ESP.getFreeHeap(), ESP.getMaxAllocHeap());
#ifdef BOARD_HAS_PSRAM
    Serial.printf(" psram_free=%d psram_total=%d", ESP.getFreePsram(), ESP.getPsramSize());
#endif
    Serial.println();
}

static void cmd_uptime(void) {
    uint32_t ms = millis();
    Serial.printf("[DATA] uptime_ms=%lu uptime=%lu:%02lu:%02lu\n",
                  (unsigned long)ms,
                  (unsigned long)(ms / 3600000),
                  (unsigned long)((ms / 60000) % 60),
                  (unsigned long)((ms / 1000) % 60));
}

static void cmd_wifi(const char *args) {
    if (strncmp(args, "scan", 4) == 0) {
        if (cb && cb->wifi_scan_start) { cb->wifi_scan_start(); Serial.println("[OK] wifi scan started"); }
        else Serial.println("[ERR] wifi scan not available");
    } else if (strncmp(args, "stop", 4) == 0) {
        if (cb && cb->wifi_scan_stop) { cb->wifi_scan_stop(); Serial.println("[OK] wifi scan stopped"); }
        else Serial.println("[ERR] wifi stop not available");
    } else if (strncmp(args, "list", 4) == 0) {
        if (cb && cb->wifi_list) { cb->wifi_list(); }
        else Serial.println("[ERR] wifi list not available");
    } else if (strncmp(args, "connect ", 8) == 0) {
        // wifi connect <ssid> <password>
        char ssid[33] = {0}, pass[65] = {0};
        const char *p = args + 8;
        // Parse SSID (may be quoted)
        if (*p == '"') {
            p++;
            int i = 0;
            while (*p && *p != '"' && i < 32) ssid[i++] = *p++;
            if (*p == '"') p++;
            if (*p == ' ') p++;
        } else {
            int i = 0;
            while (*p && *p != ' ' && i < 32) ssid[i++] = *p++;
            if (*p == ' ') p++;
        }
        strncpy(pass, p, 64);
        WiFi.mode(WIFI_STA);
        WiFi.begin(ssid, pass);
        Serial.printf("[OK] Connecting to '%s'...\n", ssid);
        unsigned long start = millis();
        while (!WiFi.isConnected() && millis() - start < 15000) {
            delay(500);
            Serial.print(".");
        }
        Serial.println();
        if (WiFi.isConnected()) {
            Serial.printf("[OK] Connected! IP: %s\n", WiFi.localIP().toString().c_str());
        } else {
            Serial.println("[ERR] Connection failed (timeout)");
        }
    } else if (strcmp(args, "disconnect") == 0) {
        WiFi.disconnect();
        Serial.println("[OK] Disconnected");
    } else if (strcmp(args, "status") == 0) {
        if (WiFi.isConnected()) {
            Serial.printf("[DATA] Connected: %s  IP: %s  RSSI: %d  CH: %d\n",
                WiFi.SSID().c_str(), WiFi.localIP().toString().c_str(),
                WiFi.RSSI(), WiFi.channel());
        } else {
            Serial.println("[DATA] Not connected");
        }
    } else if (strcmp(args, "ip") == 0) {
        Serial.printf("[DATA] %s\n", WiFi.isConnected() ? WiFi.localIP().toString().c_str() : "not connected");
    } else {
        Serial.println("[ERR] wifi: scan|stop|list|connect <ssid> <pass>|disconnect|status|ip");
    }
}

static void cmd_ble(const char *args) {
    if (strncmp(args, "scan", 4) == 0) {
        if (cb && cb->ble_scan_start) { cb->ble_scan_start(); Serial.println("[OK] ble scan started"); }
        else Serial.println("[ERR] ble scan not available");
    } else if (strncmp(args, "stop", 4) == 0) {
        if (cb && cb->ble_scan_stop) { cb->ble_scan_stop(); Serial.println("[OK] ble scan stopped"); }
        else Serial.println("[ERR] ble stop not available");
    } else if (strncmp(args, "list", 4) == 0) {
        if (cb && cb->ble_list) { cb->ble_list(); }
        else Serial.println("[ERR] ble list not available");
    } else {
        Serial.println("[ERR] ble: scan|stop|list");
    }
}

static void cmd_chat(const char *args) {
    if (strncmp(args, "send ", 5) == 0) {
        if (cb && cb->chat_send) { cb->chat_send(args + 5); Serial.printf("[OK] chat sent: %s\n", args + 5); }
        else Serial.println("[ERR] chat send not available");
    } else if (strncmp(args, "ping", 4) == 0) {
        if (cb && cb->chat_ping) { cb->chat_ping(); Serial.println("[OK] ping sent"); }
        else Serial.println("[ERR] chat ping not available");
    } else if (strncmp(args, "nodes", 5) == 0) {
        if (cb && cb->chat_nodes) { cb->chat_nodes(); }
        else Serial.println("[ERR] chat nodes not available");
    } else if (strncmp(args, "channel ", 8) == 0) {
        int ch = parse_int(args + 8);
        if (cb && cb->chat_set_channel) { cb->chat_set_channel(ch); Serial.printf("[OK] channel %d\n", ch); }
        else Serial.println("[ERR] chat channel not available");
    } else if (strncmp(args, "name ", 5) == 0) {
        if (cb && cb->chat_set_name) { cb->chat_set_name(args + 5); Serial.printf("[OK] name '%s'\n", args + 5); }
        else Serial.println("[ERR] chat name not available");
    } else if (strncmp(args, "status", 6) == 0) {
        if (cb && cb->chat_status) { cb->chat_status(); }
        else Serial.println("[ERR] chat status not available");
    } else {
        Serial.println("[ERR] chat: send|ping|nodes|channel|name|status");
    }
}

static void cmd_hid(const char *args) {
    if (strncmp(args, "run", 3) == 0) {
        if (cb && cb->hid_run) { cb->hid_run(); Serial.println("[OK] hid run"); }
        else Serial.println("[ERR] hid run not available");
    } else if (strncmp(args, "type ", 5) == 0) {
        if (cb && cb->hid_type) { cb->hid_type(args + 5); Serial.printf("[OK] hid typed: %s\n", args + 5); }
        else Serial.println("[ERR] hid type not available");
    } else if (strncmp(args, "key ", 4) == 0) {
        uint8_t k = (uint8_t)parse_int(args + 4);
        if (cb && cb->hid_key) { cb->hid_key(k); Serial.printf("[OK] hid key %d\n", k); }
        else Serial.println("[ERR] hid key not available");
    } else {
        Serial.println("[ERR] hid: run|type|key");
    }
}

static bool cli_exec(const char *line) {
    /* Skip leading whitespace */
    while (*line == ' ') line++;
    if (*line == '\0' || *line == '#') return true;  /* empty or comment: consumed */

    if (strcmp(line, "quiet") == 0) {
        cli_quiet_mode = true;
        Serial.println("[OK] Quiet mode ON — debug output suppressed");
    }
    else if (strcmp(line, "verbose") == 0) {
        cli_quiet_mode = false;
        Serial.println("[OK] Verbose mode ON — all output shown");
    }
    else if (strncmp(line, "help", 4) == 0) { cmd_help(); }
    else if (strncmp(line, "tap ", 4) == 0) { cmd_tap(line + 4); }
    else if (strncmp(line, "hold ", 5) == 0) { cmd_hold(line + 5); }
    else if (strcmp(line, "release") == 0) { cmd_release(); }
    else if (strncmp(line, "swipe ", 6) == 0) { cmd_swipe(line + 6); }
    else if (strcmp(line, "screen") == 0) { cmd_screen(); }
    else if (strncmp(line, "brightness ", 11) == 0) {
        int b = parse_int(line + 11);
        if (cb && cb->set_brightness) { cb->set_brightness(b); Serial.printf("[OK] brightness %d\n", b); }
        else Serial.println("[ERR] brightness not available");
    }
    else if (strcmp(line, "heap") == 0) { cmd_heap(); }
    else if (strcmp(line, "uptime") == 0) { cmd_uptime(); }
    else if (strcmp(line, "reboot") == 0) {
        Serial.println("[OK] rebooting...");
        delay(100);
        if (cb && cb->reboot) cb->reboot();
        else ESP.restart();
    }
    // --- Power ---
    else if (strcmp(line, "power max") == 0) {
        esp_wifi_set_max_tx_power(80);
        NimBLEDevice::setPower(SQW_BLE_MAX_PWR);
        Serial.println("[OK] WiFi +20dBm, BLE +21dBm");
    }
    else if (strncmp(line, "power wifi ", 11) == 0) {
        int dbm = parse_int(line + 11);
        esp_wifi_set_max_tx_power(dbm * 4);
        Serial.printf("[OK] WiFi power set to +%ddBm\n", dbm);
    }
    else if (strncmp(line, "power ble ", 10) == 0) {
        int level = parse_int(line + 10);
        NimBLEDevice::setPower((esp_power_level_t)level);
        Serial.printf("[OK] BLE power level set to %d\n", level);
    }
    else if (strcmp(line, "power") == 0) {
        int8_t wifiPow;
        esp_wifi_get_max_tx_power(&wifiPow);
        Serial.printf("[DATA] WiFi: +%ddBm  BLE: max\n", wifiPow / 4);
    }

    // --- Simple command aliases ---
    else if (strncmp(line, "scan wifi", 9) == 0) { cmd_wifi("scan"); }
    else if (strncmp(line, "scan ble", 8) == 0) { cmd_ble("scan"); }
    else if (strncmp(line, "connect ", 8) == 0) { cmd_wifi(line); }  // "connect X Y" → wifi handler
    else if (strcmp(line, "disconnect") == 0) { cmd_wifi("disconnect"); }
    else if (strcmp(line, "status") == 0) { cmd_wifi("status"); }
    else if (strncmp(line, "hid type ", 9) == 0) { cmd_hid(line + 4); }

    // --- Attack tools (implemented in cli_bridge.cpp) ---
    else if (strncmp(line, "deauth ", 7) == 0 ||
             strncmp(line, "capture ", 8) == 0 ||
             strncmp(line, "clone ", 6) == 0 ||
             strncmp(line, "spam ", 5) == 0 ||
             strncmp(line, "evil portal ", 12) == 0 ||
             strncmp(line, "karma ", 6) == 0 ||
             strcmp(line, "dhcp starve") == 0 ||
             strncmp(line, "flipper ", 8) == 0 ||
             strncmp(line, "airtag ", 7) == 0 ||
             strncmp(line, "haystack ", 9) == 0 ||
             strcmp(line, "lan scan") == 0 ||
             strncmp(line, "badusb ", 7) == 0 ||
             strcmp(line, "stop") == 0 ||
             strcmp(line, "channel analyze") == 0 ||
             strcmp(line, "jam detect") == 0 ||
             strncmp(line, "socks ", 6) == 0 ||
             strncmp(line, "netcut ", 7) == 0 ||
             strncmp(line, "badble ", 7) == 0 ||
             strcmp(line, "ble sniff") == 0 ||
             strncmp(line, "ninebot ", 8) == 0 ||
             strncmp(line, "hfp probe ", 10) == 0)
    {
        // Forward to tool executor in cli_bridge.cpp
        extern void cli_tool_exec(const char* cmd);
        cli_tool_exec(line);
    }

    // --- Original commands ---
    else if (strncmp(line, "wifi ", 5) == 0) { cmd_wifi(line + 5); }
    else if (strncmp(line, "ble ", 4) == 0) { cmd_ble(line + 4); }
    else if (strncmp(line, "chat ", 5) == 0) { cmd_chat(line + 5); }
    else if (strncmp(line, "hid ", 4) == 0) { cmd_hid(line + 4); }
    else if (strncmp(line, "sleep ", 6) == 0) {
        int ms = parse_int(line + 6);
        delay(ms);
        Serial.printf("[OK] slept %dms\n", ms);
    }
    else if (strncmp(line, "echo ", 5) == 0) { Serial.printf("[DATA] %s\n", line + 5); }
    else if (strcmp(line, "batch") == 0) {
        batch_mode = true;
        batch_count = 0;
        Serial.println("[OK] Batch mode. Send commands, then 'end'.");
    }
    else {
        return false;   // not a toolbox command — let the caller try the base set
    }
    return true;
}
