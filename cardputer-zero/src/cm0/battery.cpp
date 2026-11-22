// SquachWatch-CM0 -- battery: see battery.h.
#include "battery.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <dirent.h>

namespace {

// One line of a sysfs attribute, trailing newline cut. False if unreadable.
bool readLine(const std::string& path, char* out, size_t n) {
    FILE* f = std::fopen(path.c_str(), "r");
    if (!f) return false;
    const bool ok = std::fgets(out, (int)n, f) != nullptr;
    std::fclose(f);
    if (!ok) return false;
    out[strcspn(out, "\r\n")] = 0;
    return true;
}

// The first power_supply whose type is "Battery" (bq27220-0 on the CM0),
// found once. A gauge that is not there at boot stays not there: the
// driver is built in and probes before userspace.
const std::string& batteryDir() {
    static bool probed = false;
    static std::string dir;
    if (probed) return dir;
    probed = true;
    const char* base = "/sys/class/power_supply";
    DIR* d = opendir(base);
    if (!d) return dir;
    while (struct dirent* e = readdir(d)) {
        if (e->d_name[0] == '.') continue;
        const std::string p = std::string(base) + "/" + e->d_name;
        char type[32];
        if (readLine(p + "/type", type, sizeof type) && strcmp(type, "Battery") == 0) {
            dir = p;
            break;
        }
    }
    closedir(d);
    std::fprintf(stderr, "[batt] %s\n", dir.empty() ? "no battery in /sys/class/power_supply; no lamp"
                                                    : dir.c_str());
    return dir;
}

}   // namespace

namespace cm0 {

bool batteryOnCable(const char* status, long currentUa) {
    if (!status) return false;
    if (!strcmp(status, "Charging") || !strcmp(status, "Full") ||
        !strcmp(status, "Not charging"))   // full (or held) on USB
        return true;
    if (!strcmp(status, "Discharging")) return false;
    return currentUa > 0;                  // "Unknown": trust the current's sign
}

bool batteryState(uint8_t& pct, bool& charging) {
    const std::string& dir = batteryDir();
    if (dir.empty()) return false;
    char buf[32];
    // `present` is optional in the class; when it is there and 0, no cell.
    if (readLine(dir + "/present", buf, sizeof buf) && atoi(buf) == 0) return false;
    if (!readLine(dir + "/capacity", buf, sizeof buf)) return false;
    const int cap = atoi(buf);
    pct = (uint8_t)(cap < 0 ? 0 : cap > 100 ? 100 : cap);
    char status[32] = "";
    long cur = 0;
    (void)readLine(dir + "/status", status, sizeof status);
    if (readLine(dir + "/current_now", buf, sizeof buf)) cur = strtol(buf, nullptr, 10);
    charging = batteryOnCable(status, cur);
    return true;
}

}   // namespace cm0
