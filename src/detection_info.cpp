// SquachWatch-CYD — detection type explanations
#include "detection_info.h"

namespace DetectionInfo {

// Indexed by DetectionType -- keep in the exact same order as the enum
// in state.h (UNKNOWN..DEAUTH), one entry per value up to COUNT.
static const char* const EXPLAIN_TEXT[] = {
    // UNKNOWN
    "This matched a known surveillance-hardware fingerprint, but not a specific brand I recognize. Still worth knowing it's there.",
    // FLOCK
    "Flock Safety makes automated license-plate-reader cameras, usually mounted on poles at neighborhood entrances. They log every plate that passes, suspect or not.",
    // AXON
    "Axon makes body cameras and TASERs for law enforcement. This picks up a body cam's own wireless signal, not necessarily an officer's exact location.",
    // META
    "Ray-Ban Meta smart glasses can record video and photos. There's a small LED that's supposed to light up while recording -- easy to miss, easier to cover.",
    // SKIMMER
    "A Bluetooth card skimmer, usually wired into an ATM or gas pump reader. It quietly exfiltrates stolen card data over BLE instead of needing physical pickup.",
    // RAVEN
    "A Raven/ShotSpotter-style gunshot-detection sensor, usually mounted on a streetlight or rooftop. It listens constantly, not just after something happens.",
    // AIRTAG
    "An Apple AirTag, riding Apple's Find My network. Legitimate for keys and luggage -- also a known method for tracking a person or vehicle without consent.",
    // DRONE
    "A drone broadcasting Remote ID, the wireless 'license plate' the FAA now requires most drones to transmit. That's what's detected, not the drone's camera feed.",
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
};
static const uint8_t EXPLAIN_TEXT_N = sizeof(EXPLAIN_TEXT) / sizeof(EXPLAIN_TEXT[0]);

const char* explain(DetectionType t) {
    uint8_t idx = (uint8_t)t;
    if (idx >= EXPLAIN_TEXT_N) idx = 0;
    return EXPLAIN_TEXT[idx];
}

const char* rssiConfidencePrimer() {
    // Trimmed to fit the bigger text size this now renders at --
    // shorter sentences, same two facts.
    return "RSSI is signal strength in dBm -- closer to zero means closer, more negative means farther. "
           "Confidence is how sure the match is: HIGH is a strong match, MED/LOW are looser guesses.";
}

}
