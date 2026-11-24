// SquachWatch-CYD — detection type explanations
#include "detection_info.h"
#include "detection.h"
#include "device_info.h"
#include <stdio.h>
#include <string.h>

namespace DetectionInfo {

// Indexed by DetectionType -- keep in the exact same order as the enum
// in state.h (UNKNOWN..DEAUTH), one entry per value up to COUNT.
static const char* const EXPLAIN_TEXT[] = {
    // UNKNOWN
    "Something the scan heard that matches nothing on SquachWatch's lists. Most are ordinary: phones, earbuds, watches, smart bulbs. Its name and maker are the best clues. If it keeps turning up wherever you go, WATCH it.",
    // FLOCK
    "Flock Safety makes automated license-plate-reader cameras, usually mounted on poles at neighborhood entrances. They log every plate that passes, suspect or not. Flock says they do not do facial recognition, but reporting by 404 Media and WIRED found the cameras detect people too, and police use Flock's tools to search for specific people by description and follow them across towns. Only one of the 32 address blocks filed here belongs to Flock itself; the rest are radio chips plenty of other gadgets use too, so a LOW reading means a chip Flock might use, not a Flock camera.",
    // AXON
    "Police body cameras and the gear around them: Axon, Motorola's WatchGuard, Digital Ally and BodyWorn, plus holster sensors and patrol-car routers. It means police gear is near, not where an officer is standing.",
    // META
    "Camera glasses -- Ray-Ban Meta, Snap Spectacles and the like. They record video and photos, and the little LED that's supposed to warn you is easy to miss and easier to cover. Careful with this one: Meta puts the same Bluetooth ID on Quest headsets, so it can also just be a VR headset in a bag.",
    // SKIMMER
    "A Bluetooth card skimmer, usually wired into an ATM or gas pump reader. It sends the stolen card numbers over Bluetooth to the crook's phone, so they never have to come back for it.",
    // RAVEN
    "A Raven gunshot-detection sensor, usually mounted on a streetlight or rooftop. It listens constantly, not just after something happens. Matched on Raven's own Bluetooth service IDs, which have not been checked against real hardware -- hence the middling confidence. ShotSpotter sensors are filed here too, on ShotSpotter's own registered WiFi prefix, but only a sensor with its WiFi up can be heard: most backhaul over cellular and say nothing.",
    // AIRTAG
    "An Apple AirTag on Apple's Find My network. Fine on keys and luggage; also a known way to track a person or car. Not yours and been with you a while? Check bags and the car. On boards with RING, it can be made to beep once it has been away from its owner.",
    // DRONE
    "A drone broadcasting Remote ID, the wireless 'license plate' the FAA requires most drones to transmit. It is decoded, not just spotted: where the aircraft is, and often where the person flying it is standing. Heard over Bluetooth and in WiFi beacons. Not its camera feed -- that stays private, which is rather the point of the complaint. SquachWatch does not listen for the newer Bluetooth 5 long-range form yet, so some drones will not show.",
    // ALPR
    "An automated license-plate reader from a vendor other than Flock. Same idea: logs every plate that passes, usually feeding a shared database.",
    // CAMERA
    "A camera-brand WiFi or Bluetooth radio, matched by hardware vendor rather than a specific known network. Could be a doorbell, a security cam, almost anything with a lens. Dashcams count: the hotspot a Viofo, BlackVue, 70mai or Thinkware puts up for its app, and the fleet cameras in delivery vans. So do the cheap hidden cameras that sit in setup mode under a name like ABCD-123456-ABCDE until somebody pairs them -- the one to look for in a rental.",
    // SAMSUNG_TAG
    "A Samsung Galaxy SmartTag, riding Samsung's own item-finder network. Same tracking-without-consent concern as an AirTag, different ecosystem.",
    // GOOGLE_TAG
    "A tracker on Google's Find My Device network -- Chipolo, Pebblebee, Moto Tag and others all ride the same system. Android's answer to Find My.",
    // TILE
    "A Tile Bluetooth tracker, one of the original item-finders. Its own finder network, owned by Life360, separate from Apple's, Google's and Samsung's. Same capability either way.",
    // RING
    "A Ring doorbell or camera, Amazon's video doorbell line. Owners share clips in the Neighbors app, and police can ask owners for footage through it again.",
    // DEAUTH
    "Not a device -- a burst of WiFi deauthentication frames, the kind used to forcibly knock devices off a network. One frame is normal traffic; a flood like this usually isn't.",
    // EVILTWIN
    "Two different boxes are broadcasting the same network name, and they disagree about security -- one wants a password, the other is wide open. That's how a fake hotspot lures you on. A mesh system never argues with itself about encryption, which is what separates this from your own router.",
    // IBEACON
    "A proximity beacon, the kind bolted inside shops, stadiums and airports. It does not track you by itself -- it shouts an ID, and an app you already installed notices and reports where you are. The number shown is which deployment and which unit, so the same first half in two places is the same operator. This is the one detection that ships switched OFF, and about volume rather than importance: one shop can put more beacons in range than this device would otherwise see all week. Turn it on in DETECTION FILTER.",
    // HACKER
    "Wireless testing hardware: a Flipper Zero, a Pwnagotchi, a WiFi Pineapple or an ESP deauther. These are legitimate tools and most owners are hobbyists or people paid to break things -- but unlike everything else here, this is gear that transmits at other radios rather than just watching. A Pwnagotchi reports its own name and how many WiFi handshakes it has captured, because it is trying to be seen by others like it. Nothing here flags a bare ESP32 dev board: that would flag half the electronics in the room, and this detector too.",
    // LORAWAN
    "A LoRaWAN network is in range: the long-range, low-power radio that utility meters, parking sensors and city infrastructure report home over. The name is the OPERATOR, read from the network prefix every device address starts with -- not a device, and not who owns one. Everything such a network carries is encrypted and stays that way; this is the envelope, not the letter. One alert per operator, however many of its devices are in earshot. Only a board with a LoRa radio hears it, and only while LORA is listening on a LoRaWAN profile -- the RX2 downlink profiles hear the most.",
    // MINE
    "A WiFi network whose name matches one of YOUR patterns, from Settings > MY ALERTS. Nothing in the built-in tables says what it is: you told the board to watch for this name, and here it is. The label on the alert is the one you gave the pattern. Network names are chosen by whoever runs the router, so anyone can name theirs to match.",
    // LISTENER
    "A smart speaker with microphones, the kind that waits all day for its wake word. Sonos speakers are known by their maker's addresses and Bluetooth ID; some older and smaller Sonos models have no microphone, so a Sonos match means a mic is likely, not certain. A HomePod is known only when someone nearby talks to Siri, and then it is certain. An Echo or a Google speaker looks like every other Amazon or Google device, so it shows only while it is being set up. Ships switched OFF: turn it on in DETECTION FILTER for a hotel or rental check.",
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

const char* explainLive(DetectionType t, const DetectionEngine& eng, const uint8_t* mac) {
    if (t != DetectionType::DRONE) return explain(t);
    if (mac && memcmp(mac, eng.remoteIdMac(), 6) != 0) return explain(t);

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

const char* explainFor(DetectionType t, const char* vendor, const char* name,
                       const DetectionEngine& eng, const uint8_t* mac) {
    if (t == DetectionType::DRONE) return explainLive(t, eng, mac);
    const DeviceInfo::Device* d = DeviceInfo::find(t, vendor, name);
    return d ? d->text : explain(t);
}

// ---- the info card's short lines --------------------------------------------
struct CardType { const char* head; const char* why; const char* tip; uint16_t a, b; };
struct CardPage { const char* title; const char* head; const char* why; const char* tip; };
#include "card_text.inc"
static_assert(sizeof(CARD_TYPES) / sizeof(CARD_TYPES[0]) == (size_t)DetectionType::COUNT,
              "every DetectionType needs a line in tools/card_text.json, in enum order");

CardLines cardLines(DetectionType t, const char* vendor, const char* name) {
    uint8_t idx = (uint8_t)t;
    if (idx >= (uint8_t)DetectionType::COUNT) idx = 0;
    const CardType& ct = CARD_TYPES[idx];
    CardLines c = { ct.head, ct.why, ct.tip, ct.a, ct.b };
    if (const DeviceInfo::Device* d = DeviceInfo::find(t, vendor, name)) {
        for (const CardPage& p : CARD_PAGES)
            if (strcmp(p.title, d->title) == 0) { c.head = p.head; c.why = p.why; c.tip = p.tip; break; }
    }
    return c;
}

const char* titleFor(DetectionType t, const char* vendor, const char* name) {
    const DeviceInfo::Device* d = DeviceInfo::find(t, vendor, name);
    return d ? d->title : detectionTypeName(t);
}

const char* rssiConfidencePrimer() {
    // Trimmed to fit the bigger text size this now renders at --
    // shorter sentences, same two facts.
    return "RSSI is signal strength in dBm -- closer to zero means closer, more negative means farther. "
           "Confidence is how sure the match is: HIGH is a strong match, MED/LOW are looser guesses.";
}

}
