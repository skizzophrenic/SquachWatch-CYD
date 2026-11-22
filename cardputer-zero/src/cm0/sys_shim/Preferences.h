// SquachWatch-CM0 -- Preferences (ESP32 NVS) on the device's filesystem.
//
// Shadows the firmware's sim/Preferences.h (sys_shim is first on the include
// path; every firmware file includes <Preferences.h>). Same surface the sim
// shim covers -- and the same values for the same calls, since the firmware
// is tested against that shim -- with three differences that matter on a
// device that has a battery and runs under sudo:
//
//  - one store per NAMESPACE, shared by every Preferences object that opens
//    it, as NVS is. The sim gives each object its own maps, so two objects
//    on one namespace (wardrive is opened from two places) each saved their
//    own copy over the other's.
//  - writes go through prefs_store: temp file + fsync + rename, never a
//    truncated namespace after a power cut; files owned by SUDO_USER.
//  - begin(ns, readOnly=true) on a namespace that was never written returns
//    false, as nvs_open(NVS_READONLY) does; puts on a read-only handle
//    return 0 and change nothing.
//
// Persistence is off unless cm0::prefsSetDir() was called (the live app);
// --shot / --selftest run in memory. The file format is the sim's
// "<type> <key> <value>" lines, so a sim NVS directory and this one are
// interchangeable; 'w' (u16), 'l' (u32 via putULong) and 'n' (i32) are
// additions the sim never needed.
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>

#include "prefs_store.h"

namespace cm0 {
struct PrefsNs {
    bool loaded = false;    // file read (or found absent) once
    bool exists = false;    // on disk or written this run
    std::map<std::string, std::pair<char, std::string>> kv;   // key -> (type, text)
};
inline std::map<std::string, PrefsNs>& prefsNamespaces() {
    static std::map<std::string, PrefsNs> m;
    return m;
}
}   // namespace cm0

class Preferences {
public:
    bool begin(const char* ns, bool readOnly = false) {
        if (!ns || !*ns || strlen(ns) > 15) return false;   // NVS name limit
        cm0::PrefsNs& s = cm0::prefsNamespaces()[ns];
        if (!s.loaded) {
            s.loaded = true;
            std::string blob;
            if (cm0::prefsLoad(ns, blob)) { s.exists = true; parse(s, blob); }
        }
        if (readOnly && !s.exists) return false;
        _ns = ns; _s = &s; _ro = readOnly;
        return true;
    }
    void end() { _s = nullptr; }

    bool isKey(const char* k) const { return _s && k && _s->kv.count(k); }
    bool remove(const char* k) {
        if (!writable() || !k) return false;
        _s->kv.erase(k); save(); return true;
    }
    bool clear() {
        if (!writable()) return false;
        _s->kv.clear(); save(); return true;
    }

    size_t putBool(const char* k, bool v)         { return putNum(k, 'b', v ? 1 : 0, 1); }
    size_t putUChar(const char* k, uint8_t v)     { return putNum(k, 'u', v, 1); }
    size_t putShort(const char* k, int16_t v)     { return putNum(k, 'h', v, 2); }
    size_t putUShort(const char* k, uint16_t v)   { return putNum(k, 'w', v, 2); }
    size_t putInt(const char* k, int32_t v)       { return putNum(k, 'n', v, 4); }
    size_t putUInt(const char* k, uint32_t v)     { return putNum(k, 'i', v, 4); }
    size_t putULong(const char* k, uint32_t v)    { return putNum(k, 'l', v, 4); }

    bool     getBool(const char* k, bool d = false) const      { return getNum(k, 'b', d ? 1 : 0) != 0; }
    uint8_t  getUChar(const char* k, uint8_t d = 0) const      { return (uint8_t)getNum(k, 'u', d); }
    int16_t  getShort(const char* k, int16_t d = 0) const      { return (int16_t)getNum(k, 'h', d); }
    uint16_t getUShort(const char* k, uint16_t d = 0) const    { return (uint16_t)getNum(k, 'w', d); }
    int32_t  getInt(const char* k, int32_t d = 0) const        { return (int32_t)getNum(k, 'n', d); }
    uint32_t getUInt(const char* k, uint32_t d = 0) const      { return (uint32_t)getNum(k, 'i', d); }
    uint32_t getULong(const char* k, uint32_t d = 0) const     { return (uint32_t)getNum(k, 'l', d); }

    // Strings: stored as-is (last field on the line). A value with a line
    // break could not round-trip, so -- like a too-long NVS string -- it is
    // refused rather than silently cut.
    size_t putString(const char* k, const char* v) {
        if (!writable() || !k || !v || strchr(v, '\n') || strchr(v, '\r')) return 0;
        _s->kv[k] = { 's', v };
        save();
        return strlen(v);
    }
    // The sim's semantics (what the firmware was tested against): copy up to
    // maxLen-1 bytes, always terminate, return the copied length.
    size_t getString(const char* k, char* buf, size_t maxLen) const {
        if (!buf || !maxLen) return 0;
        const std::string* v = find(k, 's');
        const size_t n = v ? (v->size() < maxLen - 1 ? v->size() : maxLen - 1) : 0;
        if (n) memcpy(buf, v->data(), n);
        buf[n] = 0;
        return n;
    }

    // Blobs, hex in the 's' slot as the sim does (getBytes on a putString
    // key would read it as hex there too). Zero length is rejected without
    // touching the stored value -- the real API's behaviour, which the sim
    // reproduces on purpose (see its comment about the ignore list).
    size_t putBytes(const char* k, const void* v, size_t len) {
        if (!writable() || !k || !v || !len) return 0;
        static const char* HEX = "0123456789abcdef";
        const uint8_t* p = (const uint8_t*)v;
        std::string out;
        out.reserve(len * 2);
        for (size_t i = 0; i < len; i++) { out += HEX[p[i] >> 4]; out += HEX[p[i] & 0xF]; }
        _s->kv[k] = { 's', out };
        save();
        return len;
    }
    size_t getBytesLength(const char* k) const {
        const std::string* v = find(k, 's');
        return v ? v->size() / 2 : 0;
    }
    size_t getBytes(const char* k, void* out, size_t maxLen) const {
        const std::string* v = find(k, 's');
        if (!v || !out) return 0;
        size_t n = v->size() / 2;
        if (n > maxLen) n = maxLen;
        uint8_t* o = (uint8_t*)out;
        for (size_t i = 0; i < n; i++) o[i] = (uint8_t)((nyb((*v)[2 * i]) << 4) | nyb((*v)[2 * i + 1]));
        return n;
    }

private:
    bool writable() const { return _s && !_ro; }

    // Typed as NVS is: a key written as one type reads as the default
    // through another type's getter (the sim keeps a map per type).
    const std::string* find(const char* k, char t) const {
        if (!_s || !k) return nullptr;
        auto it = _s->kv.find(k);
        return (it == _s->kv.end() || it->second.first != t) ? nullptr : &it->second.second;
    }
    size_t putNum(const char* k, char t, long long v, size_t size) {
        if (!writable() || !k) return 0;
        _s->kv[k] = { t, std::to_string(v) };
        save();
        return size;
    }
    long long getNum(const char* k, char t, long long d) const {
        const std::string* v = find(k, t);
        return v ? strtoll(v->c_str(), nullptr, 10) : d;
    }
    static uint8_t nyb(char c) {
        if (c >= '0' && c <= '9') return (uint8_t)(c - '0');
        if (c >= 'a' && c <= 'f') return (uint8_t)(c - 'a' + 10);
        if (c >= 'A' && c <= 'F') return (uint8_t)(c - 'A' + 10);
        return 0;
    }

    void save() {
        _s->exists = true;
        std::string out;
        for (auto& kv : _s->kv) {
            out += kv.second.first; out += ' ';
            out += kv.first;        out += ' ';
            out += kv.second.second; out += '\n';
        }
        (void)cm0::prefsSave(_ns, out);
    }

    static void parse(cm0::PrefsNs& s, const std::string& blob) {
        size_t pos = 0;
        while (pos < blob.size()) {
            size_t nl = blob.find('\n', pos);
            if (nl == std::string::npos) nl = blob.size();
            std::string line = blob.substr(pos, nl - pos);
            pos = nl + 1;
            while (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.size() < 4 || line[1] != ' ') continue;
            const size_t sp = line.find(' ', 2);
            if (sp == std::string::npos || sp == 2) continue;
            const char t = line[0];
            if (!strchr("buihswln", t)) continue;
            s.kv[line.substr(2, sp - 2)] = { t, line.substr(sp + 1) };
        }
    }

    std::string _ns;
    cm0::PrefsNs* _s = nullptr;
    bool _ro = false;
};
