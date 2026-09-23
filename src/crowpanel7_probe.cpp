#if defined(CROWPANEL7) && defined(CROWPANEL7_PERIPH_PROBE)
#include "crowpanel7_probe.h"
#include "crowpanel7_board.h"
#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <esp_heap_caps.h>
#include <WiFi.h>
#include <esp_wifi.h>

static bool i2cRead(uint8_t addr, uint8_t reg, uint8_t* out, size_t n) {
    Wire.beginTransmission(addr); Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom((int)addr, (int)n) != (int)n) return false;
    for (size_t i = 0; i < n; i++) out[i] = Wire.read();
    return true;
}
static int bcd(uint8_t v) { return (v >> 4) * 10 + (v & 0x0F); }

void crowPeriphProbe() {
    Serial.println(F("[probe] ---- CrowPanel 7 peripherals ----"));
    Serial.printf("[probe] internal RAM: %u free, largest block %u; PSRAM %u free\n",
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    // PCF8563 at 0x51: time at 0x02..0x08 (BCD); bit 7 of the seconds is VL,
    // "voltage low, time not trusted" -- set when there is no backup cell.
    uint8_t t[7];
    if (i2cRead(PCF8563_ADDR, 0x02, t, 7)) {
        const bool vl = t[0] & 0x80;
        Serial.printf("[probe] PCF8563: answers; %02d:%02d:%02d  20%02d-%02d-%02d  (VL=%d -> %s)\n",
                      bcd(t[2] & 0x3F), bcd(t[1] & 0x7F), bcd(t[0] & 0x7F),
                      bcd(t[6]), bcd(t[5] & 0x1F), bcd(t[3] & 0x3F), (int)vl,
                      vl ? "no backup cell, time not held across power-off" : "time held");
    } else {
        Serial.println(F("[probe] PCF8563: no answer at 0x51"));
    }

    // SD on GPIO 6/4/5, which the card slot shares with the I2S amplifier
    // and the wireless header through a CH486F analog switch. The switch's
    // select lines are K1, a two-position DIP switch on the board (10K
    // pull-ups; both open = the card), NOT anything the helper MCU can set.
    // A raw CMD0 first: R1 0x01 = a card in idle state; 0x00 = the line held
    // low, i.e. K1 is on the I2S position; 0xFF = nothing on the bus.
    {
        SPIClass raw(FSPI);
        raw.begin(5, 4, 6, -1);
        pinMode(0, OUTPUT); digitalWrite(0, HIGH);
        raw.beginTransaction(SPISettings(400000, MSBFIRST, SPI_MODE0));
        for (int i = 0; i < 12; i++) raw.transfer(0xFF);
        digitalWrite(0, LOW);
        const uint8_t c[6] = { 0x40, 0, 0, 0, 0, 0x95 };
        for (int i = 0; i < 6; i++) raw.transfer(c[i]);
        uint8_t r1 = 0xFF; int tries = 0;
        do { r1 = raw.transfer(0xFF); } while ((r1 & 0x80) && ++tries < 16);
        digitalWrite(0, HIGH); raw.transfer(0xFF);
        raw.endTransaction(); raw.end();
        Serial.printf("[probe] SD raw CMD0: R1 = 0x%02X (%s)\n", r1,
                      r1 == 0x01 ? "a card answered" : r1 == 0x00 ? "line held low: K1 is on the I2S position" : r1 == 0xFF ? "nothing on the bus" : "unexpected");
        if (r1 == 0x01) {
            SPIClass* sdspi = new SPIClass(FSPI);
            sdspi->begin(5, 4, 6, 0);
            if (SD.begin(0, *sdspi, 40000000)) {
                Serial.printf("[probe] SD: mounted, type %u, %llu MB, %llu MB used\n",
                              (unsigned)SD.cardType(), SD.cardSize() >> 20, SD.usedBytes() >> 20);
                SD.end();
            } else {
                Serial.println(F("[probe] SD: card answers but no filesystem mounted (exFAT? >32 GB?)"));
            }
            sdspi->end(); delete sdspi;
        }
    }

    // The helper's buzzer: 246 on, 247 off. A short chirp.
    Wire.beginTransmission(STC8_ADDR); Wire.write((uint8_t)246); const bool on = Wire.endTransmission() == 0;
    delay(120);
    Wire.beginTransmission(STC8_ADDR); Wire.write((uint8_t)247); const bool off = Wire.endTransmission() == 0;
    Serial.printf("[probe] STC8 buzzer: on %s, off %s\n", on ? "ack" : "NAK", off ? "ack" : "NAK");

    // WiFi reception through the sniffer's own driver, with names: the number
    // to compare with a laptop's scan from the same desk. On 2026-09-23 this
    // read 0 on every boot until the tick after esp_wifi_deinit() went into
    // DetectionEngine::init() (detection.cpp); a deaf sniffer starts here.
    {
        esp_wifi_set_promiscuous(false);
        wifi_scan_config_t c = {}; c.scan_type = WIFI_SCAN_TYPE_ACTIVE; c.scan_time.active.min = 100; c.scan_time.active.max = 300;
        uint16_t n = 0; esp_err_t e = esp_wifi_scan_start(&c, true); esp_wifi_scan_get_ap_num(&n);
        Serial.printf("[probe] WiFi scan through the sniffer's driver: %s, %u APs\n", esp_err_to_name(e), (unsigned)n);
        if (n) {
            wifi_ap_record_t* recs = (wifi_ap_record_t*)calloc(n, sizeof(wifi_ap_record_t));
            if (recs && esp_wifi_scan_get_ap_records(&n, recs) == ESP_OK)
                for (uint16_t i = 0; i < n && i < 8; i++)
                    Serial.printf("[probe]   %-24s ch %2u  %d dBm\n", (const char*)recs[i].ssid, recs[i].primary, recs[i].rssi);
            free(recs);
        }
        esp_wifi_set_promiscuous(true);
    }
    Serial.println(F("[probe] ---- end ----"));
}
#endif
