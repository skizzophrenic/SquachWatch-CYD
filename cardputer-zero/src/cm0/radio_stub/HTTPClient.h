// SquachWatch-CM0 -- the ESP32 core's HTTPClient, the part of it the
// LoRa enrichment transport uses (firmware/src/lora_enrich.cpp):
// begin(client, url), the two timeouts, setUserAgent, one plain-HTTP
// GET, the body's stream and size. No TLS, no redirects: both
// enrichment hosts answer over plain HTTP (the reason ota_wifi.cpp gives
// for the firmware image itself), and neither reply is a secret.
#pragma once
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include "WiFi.h"
#include "WiFiClient.h"

class HTTPClient {
public:
    bool begin(WiFiClient& client, const String& url) {
        s_client = &client;
        const char* p = url.c_str();
        if (strncmp(p, "http://", 7) != 0) return false;
        p += 7;
        const char* slash = strchr(p, '/');
        std::string hostport(slash ? std::string(p, slash) : std::string(p));
        s_path = slash ? std::string(slash) : "/";
        s_port = 80;
        const size_t colon = hostport.rfind(':');
        if (colon != std::string::npos) {
            s_port = (uint16_t)atoi(hostport.c_str() + colon + 1);
            hostport.resize(colon);
        }
        s_host = std::move(hostport);
        return !s_host.empty();
    }
    void setTimeout(uint32_t) {}            // the socket's SO_RCVTIMEO
    void setConnectTimeout(uint32_t) {}     // the connect()
    void setUserAgent(const char* ua) { s_ua = ua ? ua : ""; }

    int GET() {
        s_code = -1;
        s_size = -1;
        if (!s_client || !s_client->connect(s_host.c_str(), s_port)) return -1;
        char req[640];
        const int n = snprintf(req, sizeof req,
                "GET %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: %s\r\n"
                "Accept: */*\r\nConnection: close\r\n\r\n",
                s_path.c_str(), s_host.c_str(), s_ua.c_str());
        if (n < 0 || (size_t)n >= sizeof req || !writeAll((const uint8_t*)req, (size_t)n)) {
            s_client->stop();
            return -1;
        }
        // Headers, one byte at a time: small, and it lands the carry
        // exactly at the blank line, with nothing left in a buffer.
        std::string head;
        char b;
        for (;;) {
            const int r = s_client->read((uint8_t*)&b, 1);
            if (r <= 0) { s_client->stop(); return -1; }
            head += b;
            if (head.size() >= 4 && head.compare(head.size() - 4, 4, "\r\n\r\n") == 0)
                break;
            if (head.size() > 8192) { s_client->stop(); return -1; }
        }
        s_code = (int)strtol(head.c_str() + (head.find("HTTP/") == 0 ? 5 : 0), nullptr, 10);
        const size_t cl = head.find("Content-Length:");
        if (cl != std::string::npos)
            s_size = (int)strtol(head.c_str() + cl + 15, nullptr, 10);
        return s_code;
    }
    WiFiClient* getStreamPtr() { return s_client; }
    int getSize() { return s_size; }
    bool connected() { return s_client && s_client->connected(); }
    void end() { if (s_client) s_client->stop(); }

private:
    bool writeAll(const uint8_t* p, size_t n) {
        for (size_t off = 0; off < n;) {
            const int w = s_client->write(p + off, n - off);
            if (w <= 0) return false;
            off += (size_t)w;
        }
        return true;
    }
    WiFiClient* s_client = nullptr;
    std::string s_host, s_path, s_ua;
    uint16_t s_port = 80;
    int s_code = -1, s_size = -1;
};
