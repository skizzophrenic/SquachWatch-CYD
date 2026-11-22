// SquachWatch-CYD — the SQUACHY-DEX's entries and this board's record.
#include "dex.h"
#include "clock.h"
#include "detection.h"
#include <Preferences.h>
#include <Arduino.h>
#include <string.h>

namespace Dex {

namespace {

struct Entry {
    DetectionType type;
    Rarity        rarity;
    const char*   shortName;
    const char*   kind;
    const char*   radio;
    const char*   lore;
    const char*   habitat;
    const char*   quip;
    const char*   hint;
};

// In type order, so indexOf() is arithmetic. Rarity is a judgement about
// the world, not a count: a Tile is on every third keyring, a Raven is bolted
// to a pole in a city that paid for it.
const Entry ENTRY[ENTRIES] = {
    { DetectionType::FLOCK, Rarity::UNCOMMON, "FLOCK", "POLICE KIT", "WIFI",
      "A plate-reading camera on a pole, sold to towns by the subscription. Photographs every car that passes, keeps the plate, and shares the list with anyone else who pays.",
      "Intersections. Car park exits. The one road into a subdivision.",
      "It photographs cars. I am not a car. Checkmate, Flock.",
      "Looks at every car that passes. On a pole, with a solar panel." },
    { DetectionType::AXON, Rarity::RARE, "AXON", "POLICE KIT", "BLUETOOTH",
      "The body camera on an officer's chest, and the holster that wakes it up. Talks Bluetooth to the phone on their belt, which is how it gives itself away.",
      "Traffic stops. Anywhere with a badge in it.",
      "Say nothing. Say it slowly. I have practiced this.",
      "Worn on a chest. Comes with a badge attached." },
    { DetectionType::META, Rarity::UNCOMMON, "GLASS", "POLICE KIT", "BLUETOOTH",
      "Sunglasses with a camera in the hinge, always paired to a phone. A small light says it is recording. The light is small on purpose.",
      "Coffee shops. Tourist spots. The face of someone keen to tell you.",
      "They built cameras into glasses. I built a better hiding spot.",
      "Somebody is wearing this one. Look for the tiny light." },
    { DetectionType::SKIMMER, Rarity::RARE, "SKIM", "SKIMMER", "BLUETOOTH",
      "A card reader hiding inside a real one, usually at a pump. A cheap Bluetooth module inside so the thief can collect the cards from the car park without touching it.",
      "Fuel pumps. The cash machine nobody else uses.",
      "Card skimmer! Do not swipe there. Do not swipe anywhere. Barter.",
      "Lives inside a card slot. Named like a hobby kit." },
    { DetectionType::RAVEN, Rarity::RARE, "RAVEN", "SENSOR", "WIFI",
      "A gunshot detector. Microphones on a pole listen for bangs and tell a control room where they came from. It is also listening the rest of the time.",
      "Street lights in the parts of town that got picked.",
      "Never seen one. Never heard one. That is kind of its whole deal.",
      "Listens for loud bangs. On poles in cities that can afford it." },
    { DetectionType::AIRTAG, Rarity::COMMON, "TAG", "TRACKER", "BLUETOOTH",
      "Apple's little white puck. Chirps every two seconds to every iPhone in earshot, so Apple can tell its owner where it went. Changes its address every fifteen minutes to keep strangers guessing.",
      "Keychains. Luggage. The bottom of your car, if you are unlucky.",
      "Not one of these was lost. Suspicious.",
      "Small, white, round. Half the airport has one." },
    { DetectionType::DRONE, Rarity::UNCOMMON, "DRONE", "TRACKER", "WIFI",
      "A drone announcing its own serial number, position and pilot, because the law now says it has to. The DEX reads it straight off the air.",
      "Parks. Beaches. Above your fence.",
      "Who keeps launching those things? Tell me. I can take it.",
      "Flies. Tells you where its pilot is standing. Rude, honestly." },
    { DetectionType::ALPR, Rarity::UNCOMMON, "ALPR", "SENSOR", "WIFI",
      "A plate reader that is not a Flock: the older kind on a police car or a tow truck, reading every plate it drives past and keeping the ones it wants.",
      "The roof of a patrol car. The back of a tow truck.",
      "Plate reader spotted. Classic. I have no plate. Classic.",
      "Reads plates from a moving car. Motorola makes a lot of them." },
    { DetectionType::CAMERA, Rarity::COMMON, "CAM", "CAMERA", "WIFI",
      "A network camera of the ordinary kind, from a vendor known for making them. Some watch a driveway. Some watch a whole car park. All of them are online.",
      "Doorframes. Eaves. The corner of every shop.",
      "Camera detected. Smile, legend. Or do not. Your call.",
      "The plain kind. Under an eave, pointed at a door." },
    { DetectionType::SAMSUNG_TAG, Rarity::COMMON, "STAG", "TRACKER", "BLUETOOTH",
      "Samsung's answer to the AirTag, found by every Galaxy phone that walks past. Same trick, different network, same rolling address.",
      "Keyrings. Bags. Dog collars.",
      "Same trick as the apple one. I am fooled twice a day by it.",
      "A tracker that phones home through Galaxy phones." },
    { DetectionType::GOOGLE_TAG, Rarity::UNCOMMON, "GTAG", "TRACKER", "BLUETOOTH",
      "A tracker on Google's Find My Device network: Chipolo, Pebblebee, Moto Tag. Every Android phone in range reports it, whether its owner is anywhere near.",
      "Wallets. Bikes. The remote.",
      "Third network, same idea. Everyone wants to find the keys.",
      "Found by Android phones. Several brands, one network." },
    { DetectionType::TILE, Rarity::COMMON, "TILE", "TRACKER", "BLUETOOTH",
      "The tracker that came before the others. A small square that chirps when its app asks, and announces itself to every phone with the app installed.",
      "Keys. Wallets. Attached to a cat, at least once.",
      "A tile. Not a floor tile. I checked. Twice.",
      "The square one. Older than the apple." },
    { DetectionType::RING, Rarity::COMMON, "RING", "CAMERA", "WIFI",
      "A doorbell with a camera in it, and the floodlight kind on the garage. Owned by Amazon. Footage has been handed to police without the owner being asked.",
      "Front doors. The whole street, one door at a time.",
      "Doorbell camera. I do not knock. I loom.",
      "On a front door. Has a button. Watches the street." },
    { DetectionType::DEAUTH, Rarity::RARE, "DEAUTH", "ATTACK", "WIFI",
      "Not a device: an attack. Somebody flooding the air with disconnect frames to knock devices off their WiFi. Cameras drop. Doorbells go blind. Nobody nearby means well.",
      "Wherever somebody wants a camera to stop working for a minute.",
      "Somebody is throwing everyone off the WiFi. Not me. I only sniff.",
      "Not a thing you can hold. A flood of go-away frames." },
    { DetectionType::EVILTWIN, Rarity::RARE, "TWIN", "ATTACK", "WIFI",
      "A network wearing another network's name from a second, different box. Phones join it because the name matches, and then everything they send goes through the imposter.",
      "Airports. Cafes. Anywhere a free network is expected.",
      "A network in disguise. I know a disguise when I see one. I am one.",
      "Two networks, one name, two different boxes." },
    { DetectionType::IBEACON, Rarity::COMMON, "BEACON", "TRACKER", "BLUETOOTH",
      "A proximity beacon. Shops and venues plant them so an app on your phone knows which aisle you are standing in. It exists to notice you walk past.",
      "Shelves. Museum walls. Stadium concourses.",
      "It is not following me. It is waiting for me. That is worse.",
      "Fixed to a shelf or a wall. Waits for phones." },
    { DetectionType::HACKER, Rarity::RARE, "HACK", "ATTACK", "WIFI",
      "Radio-attack hardware announcing itself: a Flipper Zero, a Pwnagotchi, a WiFi Pineapple, an ESP deauther. Exact signatures only, so a match means the tool is in the room.",
      "Conferences. The back of a hoodie. Two tables over.",
      "A radio-attack tool is in the room. Act natural. I am a lamp.",
      "A pocket toy for attacking radios. It announces itself." },
    { DetectionType::LORAWAN, Rarity::UNCOMMON, "LWAN", "NETWORK", "LORA",
      "A long-range radio network for things that report home twice an hour on one battery: meters, bins, parking bays. The card is whoever runs it, read off the front of every address.",
      "Meter cupboards. Under streets. A mast you never noticed.",
      "A network for water meters. They never write to me. I checked.",
      "Quiet, slow and everywhere. Named for whoever runs it." },
    { DetectionType::LISTENER, Rarity::UNCOMMON, "MIC", "LISTENER", "WIFI",
      "A smart speaker: microphones that wait all day for their name. Sonos and HomePod can be told apart. An Echo or a Nest only while it is being set up.",
      "Hotel rooms. Rentals. Every other kitchen counter.",
      "It hears everything. I only hear radios. We are not the same.",
      "A speaker on a shelf. It waits for its own name." },
};

Record   s_rec[ENTRIES];
bool     s_newBest[ENTRIES];
bool     s_dirty   = false;
uint32_t s_changed = 0;
bool     s_began   = false;
Preferences s_prefs;

const char* NS  = "dex";
const char* KEY = "rec";

void emptyRecord(Record& r) { r.firstEpoch = 0; r.lastEpoch = 0; r.bestRssi = -128; r.night = 0; }

const Entry& entry(DetectionType t) {
    const uint8_t i = indexOf(t);
    return ENTRY[i < ENTRIES ? i : 0];
}

}  // namespace

DetectionType typeAt(uint8_t i) { return catalogueAt(i); }
uint8_t indexOf(DetectionType t) { return catalogueIndex(t); }

Rarity rarity(DetectionType t)       { return entry(t).rarity; }
const char* rarityName(Rarity r) {
    switch (r) {
        case Rarity::RARE:     return "RARE";
        case Rarity::UNCOMMON: return "UNCOMMON";
        default:               return "COMMON";
    }
}
uint8_t stars(Rarity r) { return r == Rarity::RARE ? 3 : r == Rarity::UNCOMMON ? 2 : 1; }

const char* shortName(DetectionType t) { return entry(t).shortName; }
const char* kind(DetectionType t)      { return entry(t).kind; }
const char* lore(DetectionType t)      { return entry(t).lore; }
const char* habitat(DetectionType t)   { return entry(t).habitat; }
const char* quip(DetectionType t)      { return entry(t).quip; }
const char* hint(DetectionType t)      { return entry(t).hint; }
const char* radio(DetectionType t)     { return entry(t).radio; }

void begin() {
    for (uint8_t i = 0; i < ENTRIES; i++) { emptyRecord(s_rec[i]); s_newBest[i] = false; }
    s_prefs.begin(NS, false);
    // A short read means the type list has grown since it was written; take
    // what is there and leave the rest empty.
    size_t have = s_prefs.getBytesLength(KEY);
    if (have > sizeof s_rec) have = sizeof s_rec;
    if (have >= sizeof(Record)) s_prefs.getBytes(KEY, s_rec, have - have % sizeof(Record));
    s_began = true;
}

void note(DetectionType t, int8_t rssi) {
    const uint8_t i = indexOf(t);
    if (i >= ENTRIES) return;
    Record& r = s_rec[i];
    // Only a clock that has been set gives a date worth keeping; a sighting
    // before that still updates the signal, which needs no clock.
    if (Clock::trusted()) {
        const uint32_t e = Clock::nowEpoch();
        if (!r.firstEpoch) r.firstEpoch = e;
        r.lastEpoch = e;
        if (Clock::night() && r.night < 0xFFFF) r.night++;
    }
    if (rssi > r.bestRssi) {
        if (r.bestRssi > -128) s_newBest[i] = true;   // an improvement, not a first
        r.bestRssi = rssi;
    }
    s_dirty = true;
    s_changed = millis();
}

// Ten seconds after the last change, not on every sighting: a beacon that
// bobs in and out of range would otherwise write flash all afternoon.
void tick(uint32_t now) {
    if (!s_began || !s_dirty || now - s_changed < 10000) return;
    s_dirty = false;
    s_prefs.putBytes(KEY, s_rec, sizeof s_rec);
}

const Record& record(DetectionType t) {
    const uint8_t i = indexOf(t);
    static Record none;
    if (i >= ENTRIES) { emptyRecord(none); return none; }
    return s_rec[i];
}

bool takeNewClosest(DetectionType t) {
    const uint8_t i = indexOf(t);
    if (i >= ENTRIES || !s_newBest[i]) return false;
    s_newBest[i] = false;
    return true;
}

DetectionType nemesis(const DetectionEngine& eng) {
    DetectionType best = DetectionType::UNKNOWN;
    uint32_t n = 9;   // ten or more to count
    for (uint8_t i = 0; i < ENTRIES; i++) {
        const uint32_t c = eng.lifetimeTypeCount(typeAt(i));
        if (c > n) { n = c; best = typeAt(i); }
    }
    return best;
}

void reset() {
    for (uint8_t i = 0; i < ENTRIES; i++) { emptyRecord(s_rec[i]); s_newBest[i] = false; }
    s_dirty = false;
    if (s_began) s_prefs.putBytes(KEY, s_rec, sizeof s_rec);
}

}  // namespace Dex
