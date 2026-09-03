// SquachWatch-CYD PC emulator — Preferences (NVS) shim.
//
// Covers the get*/put* surface settings.cpp, squachy.cpp and
// touch_cal.cpp actually call; add an overload here if a future one is
// missing rather than widening this comment's promise beyond what's
// implemented.
//
// Storage is in-process by default -- the one-shot renderer is a
// stateless preview tool, and settings.cpp's own defaults (theme,
// background, etc.) are exactly what you want a fresh render to start
// from. Set SQUACHSIM_NVS=<dir> and it persists to disk instead, one
// file per namespace, which is what the interactive emulator uses: a
// device that forgets its settings and replays the first-boot
// walkthrough on every launch isn't the device the firmware ships on,
// and "change a setting, restart, check it stuck" is exactly the kind
// of thing the emulator exists to test. Deleting that directory is a
// factory reset.
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>

class Preferences {
public:
    bool begin(const char* ns, bool) {
        _ns = ns ? ns : "";
        _path.clear();
        const char* dir = getenv("SQUACHSIM_NVS");
        if (dir && *dir) {
            _path = std::string(dir) + "/" + _ns + ".nvs";
            load();
        }
        return true;
    }
    void end() {}

    // touch_cal.cpp probes with isKey() before reading a stored
    // calibration. With nothing persisted it reports absent, so the
    // firmware falls back to its factory defaults -- which is what the
    // live harness's screen->raw inversion assumes. Nothing in the
    // emulator runs the calibration flow, so no calibration is ever
    // written even in persistent mode; that's the coupling to watch if
    // that ever changes.
    bool isKey(const char* k) const {
        return _b.count(k) || _u.count(k) || _ui.count(k) || _s.count(k) || _sh.count(k);
    }
    int16_t putShort(const char* k, int16_t v)  { _sh[k] = v; save(); return v; }
    int16_t getShort(const char* k, int16_t d = 0) const {
        auto it = _sh.find(k); return it == _sh.end() ? d : it->second;
    }
    bool remove(const char* k) {
        _b.erase(k); _u.erase(k); _ui.erase(k); _s.erase(k); _sh.erase(k);
        save(); return true;
    }
    bool clear() {
        _b.clear(); _u.clear(); _ui.clear(); _s.clear(); _sh.clear();
        save(); return true;
    }

    bool putBool(const char* k, bool v)         { _b[k] = v; save(); return true; }
    bool getBool(const char* k, bool d = false) const {
        auto it = _b.find(k); return it == _b.end() ? d : it->second;
    }
    uint8_t putUChar(const char* k, uint8_t v)  { _u[k] = v; save(); return true; }
    uint8_t getUChar(const char* k, uint8_t d = 0) const {
        auto it = _u.find(k); return it == _u.end() ? d : it->second;
    }
    uint32_t putUInt(const char* k, uint32_t v) { _ui[k] = v; save(); return true; }
    uint32_t getUInt(const char* k, uint32_t d = 0) const {
        auto it = _ui.find(k); return it == _ui.end() ? d : it->second;
    }
    size_t putString(const char* k, const char* v) { _s[k] = v; save(); return strlen(v); }
    size_t getString(const char* k, char* buf, size_t maxLen) const {
        auto it = _s.find(k);
        const std::string& v = (it == _s.end()) ? std::string() : it->second;
        size_t n = v.size() < maxLen - 1 ? v.size() : maxLen - 1;
        memcpy(buf, v.data(), n);
        buf[n] = 0;
        return n;
    }

private:
    // One line per key: "<type> <key> <value>". NVS keys are short
    // identifiers with no spaces, and only string values can contain
    // anything interesting -- they're last on the line, so splitting on
    // the first two spaces is enough and nothing needs escaping.
    void save() const {
        if (_path.empty()) return;
        FILE* f = fopen(_path.c_str(), "wb");
        if (!f) return;
        for (auto& kv : _b)  fprintf(f, "b %s %d\n", kv.first.c_str(), kv.second ? 1 : 0);
        for (auto& kv : _u)  fprintf(f, "u %s %u\n", kv.first.c_str(), (unsigned)kv.second);
        for (auto& kv : _ui) fprintf(f, "i %s %u\n", kv.first.c_str(), (unsigned)kv.second);
        for (auto& kv : _sh) fprintf(f, "h %s %d\n", kv.first.c_str(), (int)kv.second);
        for (auto& kv : _s)  fprintf(f, "s %s %s\n", kv.first.c_str(), kv.second.c_str());
        fclose(f);
    }

    void load() {
        _b.clear(); _u.clear(); _ui.clear(); _s.clear(); _sh.clear();
        FILE* f = fopen(_path.c_str(), "rb");
        if (!f) return;
        char line[512];
        while (fgets(line, sizeof(line), f)) {
            size_t n = strlen(line);
            while (n && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = 0;
            if (n < 4 || line[1] != ' ') continue;
            char* key = line + 2;
            char* val = strchr(key, ' ');
            if (!val) continue;
            *val++ = 0;
            switch (line[0]) {
                case 'b': _b[key]  = atoi(val) != 0; break;
                case 'u': _u[key]  = (uint8_t)strtoul(val, nullptr, 10); break;
                case 'i': _ui[key] = (uint32_t)strtoul(val, nullptr, 10); break;
                case 'h': _sh[key] = (int16_t)atoi(val); break;
                case 's': _s[key]  = val; break;
                default: break;
            }
        }
        fclose(f);
    }

    std::string _ns, _path;
    std::map<std::string, bool>     _b;
    std::map<std::string, uint8_t>  _u;
    std::map<std::string, uint32_t> _ui;
    std::map<std::string, std::string> _s;
    std::map<std::string, int16_t>  _sh;
};
