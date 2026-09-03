// SquachWatch-CYD PC emulator — Preferences (NVS) shim.
// In-process only, nothing persisted to disk -- the emulator is a
// stateless preview tool, and settings.cpp's own defaults (theme,
// background, etc.) are exactly what you want a fresh render to start
// from anyway. Covers the get*/put* surface settings.cpp and squachy.cpp
// actually call; add an overload here if a future one is missing rather
// than widening this comment's promise beyond what's implemented.
#pragma once
#include <cstdint>
#include <cstring>
#include <map>
#include <string>

class Preferences {
public:
    bool begin(const char*, bool) { return true; }
    void end() {}

    bool putBool(const char* k, bool v)         { _b[k] = v; return true; }
    bool getBool(const char* k, bool d = false) const {
        auto it = _b.find(k); return it == _b.end() ? d : it->second;
    }
    uint8_t putUChar(const char* k, uint8_t v)  { _u[k] = v; return true; }
    uint8_t getUChar(const char* k, uint8_t d = 0) const {
        auto it = _u.find(k); return it == _u.end() ? d : it->second;
    }
    uint32_t putUInt(const char* k, uint32_t v) { _ui[k] = v; return true; }
    uint32_t getUInt(const char* k, uint32_t d = 0) const {
        auto it = _ui.find(k); return it == _ui.end() ? d : it->second;
    }
    size_t putString(const char* k, const char* v) { _s[k] = v; return strlen(v); }
    size_t getString(const char* k, char* buf, size_t maxLen) const {
        auto it = _s.find(k);
        const std::string& v = (it == _s.end()) ? std::string() : it->second;
        size_t n = v.size() < maxLen - 1 ? v.size() : maxLen - 1;
        memcpy(buf, v.data(), n);
        buf[n] = 0;
        return n;
    }

private:
    std::map<std::string, bool>     _b;
    std::map<std::string, uint8_t>  _u;
    std::map<std::string, uint32_t> _ui;
    std::map<std::string, std::string> _s;
};
