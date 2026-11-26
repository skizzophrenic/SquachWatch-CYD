// SquachWatch-CYD — the GNSS sentence parser, src/gnss.cpp.
//
// A position a few hundred metres out is not an error anybody would see on a
// watch: it would just put every network on WiGLE's map in the wrong street.
// So the coordinates are checked to the last digit the format carries, and
// the date arithmetic against dates worked out by hand.
#include "gnss.h"
#include "test_util.h"
#include <cstdio>
#include <cstring>

// "$" + body + "*hh", with the checksum computed, as the module sends it.
static const char* nmea(const char* body) {
    static char buf[128];
    uint8_t sum = 0;
    for (const char* p = body; *p; p++) sum ^= (uint8_t)*p;
    snprintf(buf, sizeof buf, "$%s*%02X\r\n", body, sum);
    return buf;
}

int main() {
    suite("Checksums");
    Gnss::reset();
    ck("a good sentence is taken", Gnss::sentence(nmea("GNRMC,123519.00,A,4807.03800,N,01131.00000,E,0.0,0.0,280926,,,A"), 0));
    ck("a flipped character is refused", !Gnss::sentence("$GNRMC,123519.00,A,4807.03800,N,01131.00000,E,0.0,0.0,280926,,,A*00", 0));
    ck("no checksum at all is refused", !Gnss::sentence("$GNGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,", 0));
    ck("counted", Gnss::good() == 1 && Gnss::bad() == 2);

    suite("A fix: GGA after RMC");
    Gnss::reset();
    Gnss::sentence(nmea("GNRMC,123519.00,A,4807.03800,N,01131.00000,E,0.0,0.0,280926,,,A"), 1000);
    ck("RMC gives UTC", Gnss::utcEpoch() == Gnss::toEpoch(2026, 9, 28, 12, 35, 19));
    ck("no fix from RMC alone", !Gnss::fix().valid);
    Gnss::sentence(nmea("GNGGA,123520.00,4807.03800,N,01131.00000,E,1,08,0.9,545.4,M,46.9,M,,"), 2000);
    const Gnss::Fix& f = Gnss::fix();
    ck("valid", f.valid);
    // 48 deg 07.038 min = 48.1173 deg; 11 deg 31.000 min = 11.5166666 deg.
    ck("latitude to the 7th place", f.lat7 == 481173000);
    ck("longitude to the 7th place", f.lon7 == 115166666);
    ck("altitude", f.altM == 545);
    ck("used", f.used == 8);
    ck("accuracy is HDOP x 4, rounded", f.accM == 4);
    ck("the fix is dated by the RMC and timed by the GGA", f.epoch == Gnss::toEpoch(2026, 9, 28, 12, 35, 20));
    ck("fresh at once", Gnss::fresh(2000 + 100));
    ck("stale after FRESH_MS", !Gnss::fresh(2000 + Gnss::FRESH_MS + 1));
    ck("fresh when asked with a time a little before the fix landed", Gnss::fresh(2000 - 3));

    suite("Hemispheres");
    Gnss::sentence(nmea("GNGGA,000000.00,3351.00000,S,15112.00000,W,1,05,1.2,10.0,M,0.0,M,,"), 3000);
    ck("south is negative", Gnss::fix().lat7 == -338500000);
    ck("west is negative", Gnss::fix().lon7 == -1512000000);
    Gnss::sentence(nmea("GNGGA,000000.00,0000.00060,S,00000.00060,W,1,05,1.2,10.0,M,0.0,M,,"), 3000);
    ck("a tiny south latitude keeps its sign", Gnss::fix().lat7 == -100);

    suite("No fix");
    Gnss::sentence(nmea("GNGGA,000001.00,,,,,0,03,99.9,,,,,,"), 4000);
    ck("quality 0 is no fix", !Gnss::fix().valid);
    ck("but the count of satellites used still updates", Gnss::fix().used == 3);
    Gnss::sentence(nmea("GNRMC,000002.00,V,,,,,,,280926,,,N"), 4000);
    ck("a void RMC does not move the clock", Gnss::utcEpoch() == Gnss::toEpoch(2026, 9, 28, 12, 35, 19));

    suite("Satellites in view and heard (GSV)");
    Gnss::reset();
    // GPS: 6 in view over two messages, four with a signal.
    Gnss::sentence(nmea("GPGSV,2,1,06,01,40,083,46,02,17,308,,03,07,344,39,04,22,228,42"), 0);
    Gnss::sentence(nmea("GPGSV,2,2,06,05,50,100,,06,10,200,33"), 0);
    // Galileo: 2 in view, none heard.
    Gnss::sentence(nmea("GAGSV,1,1,02,11,30,090,,12,20,180,"), 0);
    Gnss::Sky k = Gnss::sky();
    ck("in view, all constellations", k.view == 8);
    ck("heard: only the ones with an SNR", k.heard == 4);
    // A new cycle replaces the old one rather than adding to it.
    Gnss::sentence(nmea("GPGSV,1,1,01,01,40,083,40"), 0);
    k = Gnss::sky();
    ck("a new GPS cycle replaces the last", k.view == 3 && k.heard == 1);

    suite("A character at a time, as the UART delivers it");
    Gnss::reset();
    const char* a = nmea("GNRMC,010203.00,A,5130.00000,N,00007.00000,W,0.0,0.0,280926,,,A");
    for (const char* p = a; *p; p++) Gnss::feed(*p, 0);
    const char* b = nmea("GNGGA,010204.00,5130.00000,N,00007.00000,W,1,09,0.7,20.0,M,45.0,M,,");
    for (const char* p = b; *p; p++) Gnss::feed(*p, 5000);
    ck("fixed", Gnss::fix().valid && Gnss::fix().lat7 == 515000000 && Gnss::fix().lon7 == -1166666);
    ck("2026-09-28 01:02:04 UTC", Gnss::fix().epoch == 1790557324u);

    suite("Dates");
    ck("the Unix epoch", Gnss::toEpoch(1970, 1, 1, 0, 0, 0) == 0);
    ck("2000-03-01 (after a leap day)", Gnss::toEpoch(2000, 3, 1, 0, 0, 0) == 951868800u);
    ck("2026-09-28 00:00", Gnss::toEpoch(2026, 9, 28, 0, 0, 0) == 1790553600u);

    suite("A bench fix");
    Gnss::reset();
    Gnss::fake(407128000, -740060000, 1790553600u, 100);
    ck("fake is a fix", Gnss::fix().valid && Gnss::faked());
    Gnss::sentence(nmea("GNGGA,000000.00,4042.76800,N,07400.36000,W,1,06,1.0,5.0,M,0.0,M,,"), 200);
    ck("a real fix ends the fake", !Gnss::faked() && Gnss::fix().valid);

    return report();
}
