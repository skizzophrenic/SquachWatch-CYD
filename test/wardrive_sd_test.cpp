// Actual CYD SD writer against a temporary host card; no copied writer logic.
// Catches drain-time GNSS substitution, stale/future positions and source leaks.
#include "wardrive.h"
#include "wardrive_capture.h"
#include "gnss.h"
#include "Arduino.h"
#include "test_util.h"
#include <unistd.h>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

static std::string card;
static uint32_t before;
static void at(uint32_t ms) { SimClock::nowMs = ms; }
static void nav(const char* body, uint32_t ms) {
    char buf[128]; unsigned char cs = 0;
    for (const char* p = body; *p; ++p) cs ^= *p;
    snprintf(buf, sizeof buf, "$%s*%02X", body, cs);
    Gnss::sentence(buf, ms);
}
static void position(uint32_t ms, bool moved = false) {
    nav("GNRMC,120000.00,A,4000.0000,N,07400.0000,W,0,0,281026,,,A", ms);
    nav(moved ? "GNGGA,120001.00,4100.0000,N,07400.0000,W,1,06,2.0,15.0,M,0.0,M,," :
                "GNGGA,120000.00,4000.0000,N,07400.0000,W,1,06,1.0,5.0,M,0.0,M,,", ms);
}
static void begin(uint32_t ms = 1000) {
    Wardrive::setEnabled(false); Wardrive::sdTick(ms, true);
    if (!card.empty()) std::filesystem::remove_all(card);
    char path[] = "/tmp/sqw-wardrive-test-XXXXXX";
    card = mkdtemp(path); setenv("SQUACHSIM_SD", card.c_str(), 1);
    Gnss::reset(); at(ms); before = Wardrive::written();
    Wardrive::setEnabled(true); Wardrive::sdTick(ms, true);
}
static void note(bool ble = false) {
    const uint8_t mac[6] = {2, 3, 4, 5, 6, uint8_t(ble ? 8 : 7)};
    if (ble) Wardrive::noteBle(mac, "radio", -51, true, 76);
    else Wardrive::noteWifi(mac, "radio", 0, 6, -48);
}
static std::string finish(uint32_t ms) {
    at(ms); Wardrive::sdTick(ms, true);
    at(ms + 2001); Wardrive::sdTick(ms + 2001, true);
    std::ifstream in(card + Wardrive::sdFileName());
    return std::string(std::istreambuf_iterator<char>(in), {});
}
static bool wrote(unsigned count) { return Wardrive::written() - before == count; }

int main() {
    SimClock::virtualTime = true;
    suite("Observation-time location, not drain-time location");
    begin(); note(); position(1100); finish(1100);
    ck("a pre-fix observation is never backfilled", wrote(0));
    begin(); position(1000); at(2100); note(); note(true); position(2200, true);
    std::string csv = finish(8000);
    ck("valid observations survive a stale fix at drain", wrote(2));
    ck("both radios retain the observed coordinates/metadata", csv.find(",40.0000000,-74.0000000,5,4,") != std::string::npos && csv.find(",41.0000000,") == std::string::npos);
    ck("UTC follows observation time, not the later drain", csv.find("2026-10-28 12:00:01") != std::string::npos && csv.find("2026-10-28 12:00:05") == std::string::npos);
    begin(); position(1000); at(6000); note(); position(6100, true); finish(6100);
    ck("stale at observation is not revived by a new fix", wrote(0));
    begin(); position(1100); at(1099); note(); finish(1200);
    ck("a fix timestamp after the observation is rejected", wrote(0));

    suite("Loss and source transitions before next drain");
    begin(); position(1000); at(1100); note();
    nav("GNGGA,120001.00,,,,,0,00,99.9,,,,,,", 1150); at(1200); note(true); finish(1300);
    ck("queued location survives loss; new no-fix radio does not", wrote(1));
    begin(); position(1000); nav("GNRMC,120001.00,V,,,,,,,281026,,,N", 1100); at(1101); note(); finish(1200);
    ck("void receiver status invalidates before another sdTick", wrote(0));
    begin(); Gnss::phone(420000000, -740000000, 7, 1793188800u, 1000); at(1100); note(); finish(1200);
#if defined(CYD_GPS_ELECROW)
    ck("Elecrow output requires its physical receiver", wrote(0));
#else
    ck("other CYD profiles preserve deliberate phone support", wrote(1));
#endif
    begin(); position(1000); at(1100); note();
    Gnss::phone(420000000, -740000000, 7, 1793188806u, 6100); at(6101); note(true); csv = finish(6200);
#if defined(CYD_GPS_ELECROW)
    ck("phone transition cannot relabel queued receiver observations", wrote(1) && csv.find(",40.0000000,") != std::string::npos && csv.find(",42.0000000,") == std::string::npos);
#else
    ck("phone transition retains each source's observed position", wrote(2) && csv.find(",40.0000000,") != std::string::npos && csv.find(",42.0000000,") != std::string::npos);
#endif
    begin(); Gnss::fake(430000000, -740000000, 1793188800u, 1000); note(); finish(1100);
    ck("bench coordinates never enter real WiGLE output", wrote(0));
    begin(); position(1000); Gnss::reset(); at(1100); note(); finish(1200);
    ck("receiver reset invalidates published capture position", wrote(0));

    suite("Coherent receiver position and UTC");
    begin();
    nav("GNGGA,120000.00,4000.0000,N,07400.0000,W,1,06,1.0,5.0,M,0.0,M,,", 1000);
    at(1001); note();
    nav("GNRMC,120000.00,A,4000.0000,N,07400.0000,W,0,0,281026,,,A", 1100);
    at(1101); note(true); csv = finish(1200);
    ck("GGA before RMC becomes usable only for later observations", wrote(1) && csv.find("2026-10-28 12:00:00") != std::string::npos);
    begin();
    nav("GNRMC,120000.00,A,4000.0000,N,07400.0000,W,0,0,281026,,,A", 1000);
    nav("GNGGA,120006.00,4000.0000,N,07400.0000,W,1,06,1.0,5.0,M,0.0,M,,", 7000);
    at(7001); note(); finish(7100);
    ck("a stale RMC cannot date a fresh GGA", wrote(0));
    begin();
    nav("GNGGA,120000.00,4000.0000,N,07400.0000,W,1,06,1.0,5.0,M,0.0,M,,", 1000);
    nav("GNRMC,130000.00,A,4000.0000,N,07400.0000,W,0,0,281026,,,A", 1100);
    at(1101); note(); finish(1200);
    ck("mismatched receiver times do not fabricate a timestamp", wrote(0));
    begin();
    nav("GNRMC,235959.00,A,4000.0000,N,07400.0000,W,0,0,281026,,,A", 1000);
    nav("GNGGA,000000.00,4000.0000,N,07400.0000,W,1,06,1.0,5.0,M,0.0,M,,", 2000);
    at(2001); note(); csv = finish(2100);
    ck("GGA crossing midnight uses the following date", wrote(1) && csv.find("2026-10-29 00:00:00") != std::string::npos);
    begin(); position(1000);
    nav("GNGGA,120001.00,4060.0000,N,07400.0000,W,1,06,1.0,5.0,M,0.0,M,,", 1100);
    at(1101); note(); finish(1200);
    ck("malformed latitude minutes invalidate the receiver fix", wrote(0));
    begin(); position(1000);
    nav("GNGGA,xxxxxx,4000.0000,N,07400.0000,W,1,06,1.0,5.0,M,0.0,M,,", 1100);
    at(1101); note(); finish(1200);
    ck("invalid receiver time cannot inherit an older timestamp", wrote(0));

    suite("Monotonic wrap");
    begin(0xfffffe00u); position(0xfffffe00u); at(0x00000200u); note(); csv = finish(0x00002000u);
    ck("valid across wrap, even when stale at drain", wrote(1) && csv.find("2026-10-28 12:00:01") != std::string::npos);
    begin(0xfffffe00u); position(0xfffffe00u); at(0x00002000u); note(); position(0x00002100u); finish(0x00002200u);
    ck("stale across wrap cannot acquire a newer position", wrote(0));

    Wardrive::setEnabled(false); Wardrive::sdTick(millis(), true);
    std::filesystem::remove_all(card);
    return report();
}
