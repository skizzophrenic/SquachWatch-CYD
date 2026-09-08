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
#include <vector>

#ifdef __EMSCRIPTEN__
// Browser backing store. localStorage rather than IDBFS on purpose:
// save() is called from inside every put*(), synchronously, and
// localStorage is synchronous too -- so the file-backed logic below
// keeps its exact shape and only swaps where the bytes land. IDBFS is
// the more "correct" Emscripten answer, but FS.syncfs is async: it
// would need a debounce plus a visibilitychange handler and would still
// drop the last write if the tab were killed.
//
// The blob is the same "<type> <key> <value>" text the native path
// writes, which is already plain ASCII, so nothing needs encoding.
//
// DECLARED here, DEFINED once in main_wasm.cpp. EM_JS emits its symbols
// into every translation unit that sees it, so putting the bodies in
// this header makes the link fail with a duplicate symbol for every
// .cpp that includes it.
extern "C" {
int  squachsim_nvs_read(const char* key, char* buf, int cap);
void squachsim_nvs_write(const char* key, const char* val);
}
#endif

class Preferences {
public:
    bool begin(const char* ns, bool) {
        _ns = ns ? ns : "";
        _path.clear();
#ifdef __EMSCRIPTEN__
        // Always on in the browser: there is no shell to set the env var
        // from, and a demo that replays the colour check and the whole
        // walkthrough on every page load is a worse demo. _path is a
        // localStorage key here, not a filesystem path. Scoped per
        // namespace, same as the one-file-per-namespace native layout.
        _path = "squachsim.nvs." + _ns;
        load();
#else
        const char* dir = getenv("SQUACHSIM_NVS");
        if (dir && *dir) {
            _path = std::string(dir) + "/" + _ns + ".nvs";
            load();
        }
#endif
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
    // Blob API. The real Preferences has had this all along -- the shim
    // never needed it because the only callers (touch calibration) sit
    // behind board guards the simulator does not build. IgnoreList stores
    // its whole MAC list as one blob, so it does.
    //
    // Backed by the existing string map with the bytes hex-encoded, so it
    // rides the same localStorage save/load path as everything else
    // instead of needing a second serialisation format.
    size_t putBytes(const char* k, const void* v, size_t len) {
        // The real Preferences::putBytes rejects a zero-length value and
        // returns without touching NVS, leaving whatever was stored under
        // the key intact. Reproduced here rather than "fixed", because a
        // shim that is more forgiving than the hardware hides exactly the
        // bugs the emulator exists to find -- this one shipped for eleven
        // releases because emptying the ignore list worked in the sim.
        if (!k || !v || !len) return 0;
        static const char* HEX = "0123456789abcdef";
        const uint8_t* p = (const uint8_t*)v;
        std::string out;
        out.reserve(len * 2);
        for (size_t i = 0; i < len; i++) {
            out += HEX[(p[i] >> 4) & 0xF];
            out += HEX[p[i] & 0xF];
        }
        _s[k] = out;
        save();
        return len;
    }
    size_t getBytesLength(const char* k) const {
        auto it = _s.find(k);
        return it == _s.end() ? 0 : it->second.size() / 2;
    }
    size_t getBytes(const char* k, void* out, size_t maxLen) const {
        auto it = _s.find(k);
        if (it == _s.end()) return 0;
        const std::string& h = it->second;
        size_t n = h.size() / 2;
        if (n > maxLen) n = maxLen;
        uint8_t* o = (uint8_t*)out;
        for (size_t i = 0; i < n; i++) {
            auto nyb = [](char c) -> uint8_t {
                if (c >= '0' && c <= '9') return (uint8_t)(c - '0');
                if (c >= 'a' && c <= 'f') return (uint8_t)(c - 'a' + 10);
                if (c >= 'A' && c <= 'F') return (uint8_t)(c - 'A' + 10);
                return 0;
            };
            o[i] = (uint8_t)((nyb(h[i * 2]) << 4) | nyb(h[i * 2 + 1]));
        }
        return n;
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
    // Serialising to a string first is what lets the browser and the
    // native path share one format -- the only thing that differs below
    // is where the blob is put.
    std::string serialize() const {
        std::string out;
        char line[576];
        for (auto& kv : _b)  { snprintf(line, sizeof(line), "b %s %d\n", kv.first.c_str(), kv.second ? 1 : 0);   out += line; }
        for (auto& kv : _u)  { snprintf(line, sizeof(line), "u %s %u\n", kv.first.c_str(), (unsigned)kv.second); out += line; }
        for (auto& kv : _ui) { snprintf(line, sizeof(line), "i %s %u\n", kv.first.c_str(), (unsigned)kv.second); out += line; }
        for (auto& kv : _sh) { snprintf(line, sizeof(line), "h %s %d\n", kv.first.c_str(), (int)kv.second);      out += line; }
        for (auto& kv : _s)  { snprintf(line, sizeof(line), "s %s %s\n", kv.first.c_str(), kv.second.c_str());   out += line; }
        return out;
    }

    void save() const {
        if (_path.empty()) return;
        const std::string blob = serialize();
#ifdef __EMSCRIPTEN__
        squachsim_nvs_write(_path.c_str(), blob.c_str());
#else
        FILE* f = fopen(_path.c_str(), "wb");
        if (!f) return;
        fwrite(blob.data(), 1, blob.size(), f);
        fclose(f);
#endif
    }

    void deserialize(const std::string& blob) {
        size_t pos = 0;
        char line[576];
        while (pos < blob.size()) {
            size_t nl = blob.find('\n', pos);
            if (nl == std::string::npos) nl = blob.size();
            size_t n = nl - pos;
            if (n >= sizeof(line)) n = sizeof(line) - 1;
            memcpy(line, blob.data() + pos, n);
            line[n] = 0;
            pos = nl + 1;
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
    }

    void load() {
        _b.clear(); _u.clear(); _ui.clear(); _s.clear(); _sh.clear();
#ifdef __EMSCRIPTEN__
        std::vector<char> buf(1024);
        int n = squachsim_nvs_read(_path.c_str(), buf.data(), (int)buf.size());
        if (n < 0) return;                       // key absent -- first run
        if (n + 1 > (int)buf.size()) {           // outgrew the initial guess
            buf.assign(n + 1, 0);
            if (squachsim_nvs_read(_path.c_str(), buf.data(), (int)buf.size()) < 0) return;
        }
        deserialize(std::string(buf.data()));
#else
        FILE* f = fopen(_path.c_str(), "rb");
        if (!f) return;
        std::string blob;
        char chunk[512];
        size_t got;
        while ((got = fread(chunk, 1, sizeof(chunk), f)) > 0) blob.append(chunk, got);
        fclose(f);
        deserialize(blob);
#endif
    }

    std::string _ns, _path;
    std::map<std::string, bool>     _b;
    std::map<std::string, uint8_t>  _u;
    std::map<std::string, uint32_t> _ui;
    std::map<std::string, std::string> _s;
    std::map<std::string, int16_t>  _sh;
};
