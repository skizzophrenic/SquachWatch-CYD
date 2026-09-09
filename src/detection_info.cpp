// SquachWatch-CYD — detection type explanations
#include "detection_info.h"
#include "detection.h"
#include <stdio.h>

namespace DetectionInfo {

// Indexed by DetectionType -- keep in the exact same order as the enum
// in state.h (UNKNOWN..DEAUTH), one entry per value up to COUNT.
static const char* const EXPLAIN_TEXT[] = {
    // UNKNOWN
    "Nothing in the signature tables matched this one. It is the fallback the panel falls back TO, so if you are reading it about a real sighting, that sighting got logged under a type with no explanation of its own -- which is a bug worth reporting.",
    // FLOCK
    "Flock Safety makes automated license-plate-reader cameras, usually mounted on poles at neighborhood entrances. They log every plate that passes, suspect or not. Check the confidence: of the 29 hardware prefixes filed here exactly ONE is registered to Flock, and the rest are the generic Espressif and Liteon parts they build on -- shared with every dev board and smart plug on earth. A LOW reading here means a radio Flock might use, not a Flock camera.",
    // AXON
    "Axon makes body cameras and TASERs for law enforcement. This picks up a body cam's own wireless signal, not necessarily an officer's exact location.",
    // META
    "Camera glasses -- Ray-Ban Meta, Snap Spectacles and the like. They record video and photos, and the little LED that's supposed to warn you is easy to miss and easier to cover. Careful with this one: Meta puts the same Bluetooth ID on Quest headsets, so it can also just be a VR headset in a bag.",
    // SKIMMER
    "A Bluetooth card skimmer, usually wired into an ATM or gas pump reader. It quietly exfiltrates stolen card data over BLE instead of needing physical pickup.",
    // RAVEN
    "A Raven gunshot-detection sensor, usually mounted on a streetlight or rooftop. It listens constantly, not just after something happens. Matched on Raven's own Bluetooth service IDs, which have not been checked against real hardware -- hence the middling confidence. NOT ShotSpotter: those hold no registered hardware ID at all and backhaul over cellular rather than broadcasting, so nothing here can see one.",
    // AIRTAG
    "An Apple AirTag, riding Apple's Find My network. Legitimate for keys and luggage -- also a known method for tracking a person or vehicle without consent.",
    // DRONE
    "A drone broadcasting Remote ID, the wireless 'license plate' the FAA requires most drones to transmit. It is decoded, not just spotted: where the aircraft is, and often where the person flying it is standing. Not its camera feed -- that stays private, which is rather the point of the complaint. Bluetooth Legacy only: this chip is BLE 4.2, so a drone using the Bluetooth 5 long-range form is invisible to it and always will be.",
    // ALPR
    "An automated license-plate reader from a vendor other than Flock. Same idea: logs every plate that passes, usually feeding a shared database.",
    // CAMERA
    "A camera-brand WiFi or Bluetooth radio, matched by hardware vendor rather than a specific known network. Could be a doorbell, a security cam, almost anything with a lens.",
    // SAMSUNG_TAG
    "A Samsung Galaxy SmartTag, riding Samsung's own item-finder network. Same tracking-without-consent concern as an AirTag, different ecosystem.",
    // GOOGLE_TAG
    "A tracker on Google's Find My Device network -- Chipolo, Pebblebee, Moto Tag and others all ride the same system. Android's answer to Find My.",
    // TILE
    "A Tile Bluetooth tracker, one of the original item-finders. Independent of Apple/Google/Samsung's networks, same basic capability either way.",
    // RING
    "A Ring doorbell or camera, Amazon's video doorbell line. Often networked into neighborhood-wide sharing through the Neighbors app.",
    // DEAUTH
    "Not a device -- a burst of WiFi deauthentication frames, the kind used to forcibly knock devices off a network. One frame is normal traffic; a flood like this usually isn't.",
    // EVILTWIN
    "Two different boxes are broadcasting the same network name, and they disagree about security -- one wants a password, the other is wide open. That's how a fake hotspot lures you on. A mesh system never argues with itself about encryption, which is what separates this from your own router.",
    // IBEACON
    "A proximity beacon, the kind bolted inside shops, stadiums and airports. It does not track you by itself -- it shouts an ID, and an app you already installed notices and reports where you are. The number shown is which deployment and which unit, so the same first half in two places is the same operator. This is the one detection that ships switched OFF, and about volume rather than importance: one shop can put more beacons in range than this device would otherwise see all week. Turn it on in DETECTION FILTER.",
    // HACKER
    "Wireless testing hardware: a Flipper Zero, a Pwnagotchi, a WiFi Pineapple or an ESP deauther. These are legitimate tools and most owners are hobbyists or people paid to break things -- but unlike everything else here, this is gear that transmits at other radios rather than just watching. A Pwnagotchi reports its own name and how many WiFi handshakes it has captured, because it is trying to be seen by others like it. Nothing here flags a bare ESP32 dev board: that would flag half the electronics in the room, and this detector too.",
};
static const uint8_t EXPLAIN_TEXT_N = sizeof(EXPLAIN_TEXT) / sizeof(EXPLAIN_TEXT[0]);

// Add a DetectionType without writing its MORE INFO paragraph and this
// fails the build. Without it explain() quietly clamps out-of-range to 0,
// so the new detection would show UNKNOWN's text -- wrong, plausible, and
// invisible until somebody happened to tap it.
static_assert(EXPLAIN_TEXT_N == (uint8_t)DetectionType::COUNT,
              "every DetectionType needs a MORE INFO entry, in enum order");

const char* explain(DetectionType t) {
    uint8_t idx = (uint8_t)t;
    if (idx >= EXPLAIN_TEXT_N) idx = 0;
    return EXPLAIN_TEXT[idx];
}

const char* explainLive(DetectionType t, const DetectionEngine& eng) {
    if (t != DetectionType::DRONE) return explain(t);

    const RemoteId::Info& rid = eng.remoteId();
    if (!rid.haveBasic && !rid.haveLoc && !rid.haveOperator) return explain(t);

    // Replaces the paragraph rather than appending to it: Theme::wrapText
    // caps at 320 characters and the stock DRONE text already spends 185.
    // Only the parts actually received are printed -- the three messages
    // arrive as separate adverts, and 0,0 is a real place off Africa rather
    // than a safe stand-in for "not known yet".
    static char buf[320];
    int n = 0;
    if (rid.haveBasic)
        n += snprintf(buf + n, sizeof(buf) - n, "ID %s, a %s. ",
                      rid.serial, RemoteId::uaTypeName(rid.uaType));
    if (rid.haveLoc && n < (int)sizeof(buf))
        n += snprintf(buf + n, sizeof(buf) - n, "AIRCRAFT %.5f, %.5f at %dm. ",
                      (double)rid.lat, (double)rid.lon, (int)rid.altM);
    if (rid.haveOperator && n < (int)sizeof(buf))
        n += snprintf(buf + n, sizeof(buf) - n,
                      "OPERATOR %.5f, %.5f -- that is where the pilot is standing.",
                      (double)rid.opLat, (double)rid.opLon);
    buf[sizeof(buf) - 1] = '\0';
    return buf;
}

const char* rssiConfidencePrimer() {
    // Trimmed to fit the bigger text size this now renders at --
    // shorter sentences, same two facts.
    return "RSSI is signal strength in dBm -- closer to zero means closer, more negative means farther. "
           "Confidence is how sure the match is: HIGH is a strong match, MED/LOW are looser guesses.";
}

}
