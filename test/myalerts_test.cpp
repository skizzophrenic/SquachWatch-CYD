// SquachWatch-CYD — MY ALERTS (include/my_alerts.h): the pattern rules, and
// the list's slots surviving a restart.
#include "test_util.h"
#include "my_alerts.h"
#include <cstring>
#include <string>

using namespace MyAlerts;

int main() {
    suite("Plain text is a prefix");
    ck("veh catches veh912", nameMatches("veh", "veh912"));
    ck("veh9 catches veh912", nameMatches("veh9", "veh912"));
    ck("and the name itself", nameMatches("veh912", "veh912"));
    ck("case does not matter", nameMatches("VEH", "veh912") && nameMatches("veh", "VEH912"));
    ck("not in the middle", !nameMatches("veh", "myveh912"));
    ck("not longer than the name", !nameMatches("veh9123", "veh912"));
    ck("veh8 is not veh912", !nameMatches("veh8", "veh912"));

    suite("Wildcards match the whole name");
    ck("veh* is the prefix again", nameMatches("veh*", "veh912") && nameMatches("veh*", "veh"));
    ck("veh9* catches the 900s", nameMatches("veh9*", "veh912") && !nameMatches("veh9*", "veh812"));
    ck("veh9?? is three digits", nameMatches("veh9??", "veh912") && !nameMatches("veh9??", "veh9123") &&
                                 !nameMatches("veh9??", "veh91"));
    ck("*veh is a suffix", nameMatches("*veh", "county-veh") && !nameMatches("*veh", "veh912"));
    ck("*pd* is anywhere", nameMatches("*pd*", "city-pd-12") && nameMatches("*PD*", "pd") &&
                           !nameMatches("*pd*", "city-p-d"));
    ck("? needs a character", !nameMatches("veh?", "veh"));
    ck("stars run backwards when they must", nameMatches("*a*b", "aaab") && nameMatches("a*b*c", "axxbyyc") &&
                                              !nameMatches("a*b*c", "axxbyy"));
    ck("a pathological pattern still finishes", !nameMatches("*a*a*a*a*a*a*a*b", "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"));
    ck("nothing matches an empty name", !nameMatches("veh", "") && !nameMatches("*", ""));
    ck("an empty pattern matches nothing", !nameMatches("", "veh912"));

    suite("How wide a pattern is");
    ck("* alone is refused", check("*") == Check::TOO_WIDE && check("**?") == Check::TOO_WIDE);
    ck("one letter is refused", check("a*") == Check::TOO_WIDE && check("a") == Check::TOO_WIDE);
    ck("two letters are saved with a warning", check("ve*") == Check::WIDE && check("pd") == Check::WIDE);
    ck("veh is fine", check("veh") == Check::OK && check("veh9??") == Check::OK);

    suite("The list");
    begin();
    clear();
    ck("starts empty", count() == 0 && match("veh912") == -1 && nth(0) == -1);
    ck("refuses a pattern that is too wide", add("*", "") == -1 && count() == 0);
    const int8_t a = add("veh9*", "CITY PD");
    const int8_t b = add("cty*", "");
    ck("adds into the first slots", a == 0 && b == 1 && count() == 2);
    ck("matches by slot", match("veh912") == a && match("CTY-44") == b && match("home-wifi") == -1);
    ck("the tag is the label, or the pattern without one",
       std::string(tag((uint8_t)a)) == "CITY PD" && std::string(tag((uint8_t)b)) == "cty*");
    const char* rowVendor = tag((uint8_t)a);   // what a LOG row holds
    remove((uint8_t)a);
    ck("a removed slot goes empty, it does not move", !used((uint8_t)a) && used((uint8_t)b) && nth(0) == b);
    ck("and the row that pointed at it reads blank, not the next one's label", rowVendor[0] == '\0');
    const int8_t c = add("veh8*", "COUNTY");
    ck("the empty slot is reused", c == a && count() == 2);
    ck("a label is cut to fit the plate", setLabel((uint8_t)b, "A VERY LONG LABEL INDEED") &&
                                          strlen(label((uint8_t)b)) == LABEL_MAX);
    char longPat[64];
    memset(longPat, 'x', sizeof longPat - 1);
    longPat[sizeof longPat - 1] = '\0';
    for (int i = 0; i < 6; i++) add(longPat, "");
    ck("eight and no more", count() == MAX && add("veh7*", "") == -1);
    ck("a long pattern is cut at an SSID's length", strlen(pattern(2)) == PAT_MAX);
    return report();
}
