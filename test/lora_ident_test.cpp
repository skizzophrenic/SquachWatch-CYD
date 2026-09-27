// The offline half of node enrichment -- src/lora_ident.cpp -- against known
// answers rather than against itself.
//
// Grid squares are checked against two locators established elsewhere: one
// returned by a callsign database for a real licence, one published for W1AW.
// Callsign extraction is checked against real corpus strings, including the
// four self-referential-hex cases that are the dominant false positive, and
// the one genuine false positive a live 30-token check turned up.
#include "lora_ident.h"
#include "test_util.h"
#include <cstring>

using namespace Lora;

static bool gridIs(double lat, double lon, const char* want) {
    char g[8] = {0};
    Ident::grid((int32_t)(lat * 1e7), (int32_t)(lon * 1e7), g, sizeof g);
    return strcmp(g, want) == 0;
}

// One name in, the tokens out, as a single "A,B" string so a test reads as one
// line. `self` is Ident::selfHex's answer for the row, or -1.
static void extract(const char* name, int32_t self, char* out, size_t cap) {
    Ident::Call c[Ident::CALL_MAX];
    const uint8_t n = Ident::calls(name, self, c, Ident::CALL_MAX);
    out[0] = '\0';
    size_t o = 0;
    for (uint8_t i = 0; i < n && i < Ident::CALL_MAX; i++) {
        const size_t l = strlen(c[i].text);
        if (o + l + 2 >= cap) break;
        if (o) out[o++] = ',';
        memcpy(out + o, c[i].text, l);
        o += l;
        out[o] = '\0';
    }
}

static bool yields(const char* name, int32_t self, const char* want) {
    char got[64];
    extract(name, self, got, sizeof got);
    return strcmp(got, want) == 0;
}

int main() {
    suite("Maidenhead, against locators established elsewhere");
    {
        // The grid a callsign database returned for DL1TMA's registered QTH.
        ck("51.424655, 8.019218 is JO41ak", gridIs(51.424655, 8.019218, "JO41ak"));
        // W1AW, from callook.info.
        ck("41.714707, -72.728411 is FN31pr", gridIs(41.714707, -72.728411, "FN31pr"));
        // The advert this bench actually heard: the node's OWN position, which
        // is a different subsquare from its licensee's registered one above.
        // Showing the second in place of the first would move a repeater to
        // somebody's house, so the two being different is the point.
        ck("the advert's 51.3435, 8.0845 is JO41bi", gridIs(51.3435, 8.0845, "JO41bi"));
        ck("0, 0 is JJ00aa", gridIs(0.0, 0.0, "JJ00aa"));
        ck("the south-west corner is AA00aa", gridIs(-90.0, -180.0, "AA00aa"));
        // Clamped rather than refused: 90 N indexes past field R.
        ck("the north pole clamps into RR", gridIs(90.0, 179.999999, "RR99xx"));
        ck("the antimeridian clamps in longitude", gridIs(0.0, 180.0, "RJ90xa"));
        char g[8];
        ck("a buffer too small is refused", !Ident::grid(0, 0, g, 6));
    }

    suite("DXCC entity from a prefix");
    {
        Ident::Dxcc d;
        ck("DL1TMA is Germany in EU", Ident::dxcc("DL1TMA", d) &&
                                     strstr(d.entity, "Germany") && strcmp(d.continent, "EU") == 0);
        ck("W1AW is the United States in NA", Ident::dxcc("W1AW", d) &&
                                     strcmp(d.entity, "United States") == 0 && strcmp(d.continent, "NA") == 0);
        ck("VK3GRK is Australia in OC", Ident::dxcc("VK3GRK", d) &&
                                     strcmp(d.entity, "Australia") == 0 && strcmp(d.continent, "OC") == 0);
        // A valid UK Foundation call that a live callsign database did not
        // have. The offline table and the online lookup disagree, and the
        // offline one is right, which is why the row must stay.
        ck("M7GVK is in the table even though QRZ has not got it",
           Ident::dxcc("M7GVK", d) && strcmp(d.continent, "EU") == 0);
        // Longest prefix wins, or islands become their mainland.
        ck("BV is Taiwan", Ident::dxcc("BV1AB", d) && strcmp(d.entity, "Taiwan") == 0);
        ck("BV9P is Pratas, not Taiwan", Ident::dxcc("BV9P", d) && strstr(d.entity, "Pratas"));
        // The one genuine false positive from a 30-token live check.
        ck("1C8WR has no allocation", !Ident::dxcc("1C8WR", d));
        ck("an SSID does not confuse the prefix", Ident::dxcc("DH5DAX-7", d) && strstr(d.entity, "Germany"));
        ck("lower case resolves the same", Ident::dxcc("dl1tma", d) && strstr(d.entity, "Germany"));
        ck("an empty string is not an entity", !Ident::dxcc("", d));
    }

    suite("callsigns out of real corpus names");
    {
        ck("DE-NW-HSK-AR-DL1TMA", yields("DE-NW-HSK-AR-DL1TMA", -1, "DL1TMA"));
        ck("KY4TB-R keeps the call and drops the -R", yields("KY4TB-R", -1, "KY4TB"));
        ck("Tbeam Repeater DO2DNI A", yields("Tbeam Repeater DO2DNI A", -1, "DO2DNI"));
        ck("PARC (W3YJ) -- a four-character club call", yields("PARC (W3YJ)", -1, "W3YJ"));
        ck("ZS1MJT -Home", yields("ZS1MJT -Home", -1, "ZS1MJT"));
        ck("HB9Y Roc Blanc", yields("HB9Y Roc Blanc", -1, "HB9Y"));
        ck("DE-BY-DB7SG-Langdorf", yields("DE-BY-DB7SG-Langdorf", -1, "DB7SG"));
        // Rule 4: two tokens is an owner/operator pair. Both, in order.
        ck("N7JMV/KC7OOU-ROSEHILL-R gives two", yields("N7JMV/KC7OOU-ROSEHILL-R", -1, "N7JMV,KC7OOU"));
        ck("K4FAU (de N1AVI) gives two", yields("K4FAU (de N1AVI)", -1, "K4FAU,N1AVI"));
        // An SSID of the kind APRS appends is part of the token.
        ck("an SSID survives", yields("DH5DAX-7", -1, "DH5DAX-7"));
        ck("a plain name yields nothing", yields("Bergstation", -1, ""));
        ck("nothing in an empty name", yields("", -1, ""));
        // Word boundaries: a callsign-shaped run inside a longer word is not a
        // token, which is what keeps this off model numbers.
        ck("no token inside SE-VINSLOV02", yields("SE-Vinslov02", -1, ""));
        ck("five trailing letters is not a callsign", yields("DL1TMAXX", -1, ""));
    }

    suite("rule 2: the node's own id in hex");
    {
        // Meshtastic: the low 16 bits of the node number.
        ck("Meshtastic d9ec rejected beside node !3b1cd9ec", yields("Meshtastic d9ec", 0xD9EC, ""));
        ck("Dashtastic ec3f rejected beside its own ec3f", yields("Dashtastic ec3f", 0xEC3F, ""));
        // MeshCore: the first two bytes of the advert key.
        ck("DE-NW-GM-0C0A rejected beside key 0C0A4232", yields("DE-NW-GM-0C0A", 0x0C0A, ""));
        ck("SE-Vinslov02-1e6e rejected beside key 1E6E311B", yields("SE-Vinslov02-1e6e", 0x1E6E, ""));
        ck("FNL-LOVE-CNTRRA-RE-0A8F rejected beside 0A8F8327",
           yields("FNL-LOVE-CNTRRA-RE-0A8F", 0x0A8F, ""));
        // THE TEST THAT PROVES IT IS SELF-REFERENCE AND NOT A HEX BAN. EC3F is
        // a plausible Spanish call, and it survives beside a different id.
        ck("EC3F kept beside a node whose id is not ec3f", yields("Dashtastic ec3f", 0x1234, "EC3F"));
        ck("a hex token survives when the row has no id at all", yields("Dashtastic ec3f", -1, "EC3F"));
        // The rule is four characters only: five characters starting with the
        // id is still a callsign, and the name's own call comes first.
        ck("f57ca is five characters, so rule 2 leaves it",
           yields("KA4NQS f57ca", 0xF57C, "KA4NQS,F57CA"));
        ck("KA4NQS f57c drops only the hex", yields("KA4NQS f57c", 0xF57C, "KA4NQS"));
    }

    suite("selfHex reads the right identifier per network");
    {
        Nodes::Node n;
        memset(&n, 0, sizeof n);
        n.proto = Proto::MESHTASTIC; n.id = 0x3B1CD9ECull;
        ck("Meshtastic: the low 16 bits of the node number", Ident::selfHex(n) == 0xD9EC);
        n.proto = Proto::MESHCORE;   n.id = 0x0C0A423212345678ull;
        ck("MeshCore: the top two bytes of the key prefix", Ident::selfHex(n) == 0x0C0A);
        n.proto = Proto::APRS;
        ck("APRS appends nothing, so there is nothing to reject", Ident::selfHex(n) == -1);
        n.proto = Proto::LORAWAN;
        ck("LoRaWAN likewise", Ident::selfHex(n) == -1);
    }

    suite("describe(): claimed against transmitted");
    {
        Nodes::Node n;
        Ident::Ident id;
        // MeshCore: the callsign is a substring of a name a stranger typed.
        memset(&n, 0, sizeof n);
        n.proto = Proto::MESHCORE;
        n.id = 0x61A1B7A7437459FBull;
        strcpy(n.name, "DE-NW-HSK-AR-DL1TMA");
        n.hasPos = true; n.latE7 = 513435000; n.lonE7 = 80845000;
        Ident::describe(n, id);
        ck("a MeshCore advert name yields one claimed callsign",
           id.callCount == 1 && strcmp(id.call[0].text, "DL1TMA") == 0 && id.claimed);
        ck("...with the entity from the prefix", id.call[0].entity && strstr(id.call[0].entity, "Germany"));
        ck("...and the grid from the ADVERT's own position", strcmp(id.grid, "JO41bi") == 0);

        // LoRa APRS: the callsign is a field, transmitted as station
        // identification. Not claimed, and not extracted.
        memset(&n, 0, sizeof n);
        n.proto = Proto::APRS;
        strcpy(n.tag, "DH5DAX-7");
        Ident::describe(n, id);
        ck("an APRS tag is the callsign itself, not a claim",
           id.callCount == 1 && strcmp(id.call[0].text, "DH5DAX-7") == 0 && !id.claimed);
        ck("...and its length excludes the SSID", id.call[0].len == 6);

        // FANET names an aircraft, LoRaWAN a thing: no callsign either way.
        memset(&n, 0, sizeof n);
        n.proto = Proto::FANET;
        strcpy(n.name, "Peak4");
        n.hasPos = true; n.latE7 = 0; n.lonE7 = 0;
        Ident::describe(n, id);
        ck("FANET has no callsign but still has a grid",
           id.callCount == 0 && strcmp(id.grid, "JJ00aa") == 0);

        memset(&n, 0, sizeof n);
        n.proto = Proto::MESHCORE;
        strcpy(n.name, "Bergstation");
        Ident::describe(n, id);
        ck("no position, no grid", id.grid[0] == '\0');
    }

    return report();
}
