// SquachWatch-CYD — the device pages. See include/device_info.h.
//
// Every claim here has to survive being read by somebody standing next to the
// thing. Where the evidence is a chip rather than a product, the page says so:
// an Espressif prefix under FLOCK is "maybe a Flock camera", never "a Flock
// camera", and the grades in docs/DETECTIONS.md are the source for which is
// which. Kept to seven lines of 34 characters -- the MORE INFO panel in
// portrait -- which the host test checks by wrapping each one.
#include "device_info.h"
#include <string.h>
#include <strings.h>

namespace DeviceInfo {

static const char* const AXON_BODY =
    "An Axon body camera's own WiFi, up while it pairs or offloads video. Worn by police officers: it means a camera is near, not necessarily one pointed at you.";

const Device kDevices[] = {
    // ---- HACKER -------------------------------------------------------------
    { DetectionType::HACKER, "FLIPPER ZERO", "Flipper", nullptr,
      "A Flipper Zero: a pocket tool for radio, NFC, RFID and infrared. Mostly a hobby toy, but it can replay signals, copy badges and flood phones with fake Bluetooth popups." },
    { DetectionType::HACKER, "PWNAGOTCHI", "Pwnagotchi", nullptr,
      "A Pwnagotchi: a tiny computer that collects WiFi handshakes so passwords can be cracked later. It broadcasts its own name and catch count to find others like it -- that is what matched." },
    { DetectionType::HACKER, "PINEAPPLE", "Pineapple", nullptr,
      "A Hak5 WiFi Pineapple, spotted by its setup network. It pretends to be WiFi your phone already trusts and watches whatever joins. Security testers carry them; so do people up to no good." },
    { DetectionType::HACKER, "DEAUTHER", "Deauther", nullptr,
      "An ESP8266 or ESP32 deauther, spotted by its default network name, \"pwned\". It kicks devices off WiFi on command. Popular with hobbyists -- or it is just somebody's joke network name." },
    { DetectionType::HACKER, "HAK5 ADDRESS", "Hak5-LA", nullptr,
      "A made-up MAC address Hak5 gear likes to use. Anyone can set one, so it is a hint rather than an ID: maybe a Pineapple, maybe a coincidence. Graded low on purpose." },

    // ---- FLOCK: names first, then the one registered block, then the maybes --
    { DetectionType::FLOCK, "FLOCK POWER", nullptr, "FS Ext Battery",
      "The Bluetooth name of a Flock Safety external battery, which runs cameras on poles with no mains power. One of these nearby means a Flock unit is close." },
    { DetectionType::FLOCK, "FLOCK SETUP", "Flock-Setup", "Flock_Setup",
      "A Flock camera's setup network, used while an installer configures it. Near a new pole it usually means a camera going in, or one being serviced." },
    { DetectionType::FLOCK, "FLOCK CAMERA", "Flock-MA-L", nullptr,
      "A Flock Safety camera, on Flock's own radio block. Sold as a plate reader, but reporting found it spots people too, and police use it to search for and follow specific people." },
    { DetectionType::FLOCK, "FLOCK BLE", "Flock-BLE", nullptr,
      "Bluetooth from Flock gear: a XUNTONG radio (Flock's supplier), Flock's own accessory service, or a battery pack named by a bare serial number. XUNTONG sells to others and a number is just a number -- a lead." },
    { DetectionType::FLOCK, "FLOCK HOTSPOT", "Flock-Cam", nullptr,
      "A network named \"Flock Camera net.\", which Flock cameras have been caught putting up. Near a pole camera, it is almost certainly that camera." },
    { DetectionType::FLOCK, "LITEON CHIP", "Flock-Liteo", nullptr,
      "A Liteon wireless module. Flock hardware has used them, and so have millions of laptops. A maybe, not a match." },
    { DetectionType::FLOCK, "WIFI MODULE", "Flock-UGSI|Flock-SiLab", nullptr,
      "A USI or Silicon Labs wireless module. Other Flock detectors have caught Flock cameras on these blocks, and the same modules sit in plenty of other gadgets. A maybe, not a match." },
    { DetectionType::FLOCK, "FACTORY MAC", "Flock-QCA", nullptr,
      "An address still set to the Qualcomm chip's factory default, which a Flock camera's WiFi chip ships with. Most finished products replace it, so it is rare to see -- but not only Flock forgets to." },
    { DetectionType::FLOCK, "FLOCK MAYBE", "Flock|Flock-OEM|Flock-DeFlk", nullptr,
      "An address other Flock detectors list, but whose registration does not say Flock -- or names nobody. Kept because it has turned up on Flock gear; graded low because nothing proves it." },

    // ---- AXON ---------------------------------------------------------------
    { DetectionType::AXON, "AXON BODY 2", "Axon-Body2", nullptr, AXON_BODY },
    { DetectionType::AXON, "AXON BODY 3", "Axon-Body3", nullptr, AXON_BODY },
    { DetectionType::AXON, "AXON BODY 4", "Axon-Body4", nullptr, AXON_BODY },
    { DetectionType::AXON, "AXON NETWORK", "Axon-Field", nullptr,
      "A network named AXON-, which Axon uses on its field equipment. It points to police gear nearby: a camera, a charging dock or an in-car system." },
    { DetectionType::AXON, "AXON TASER", "Axon", nullptr,
      "A radio on Axon's own registered block, from its days as TASER International. Axon body cameras and TASERs both carry these." },
    { DetectionType::AXON, "AXON BODYCAM", "Axon-Body", nullptr,
      "An address other detectors tie to modern Axon body cameras. A lead rather than proof, so it is graded low." },
    { DetectionType::AXON, "FLEET ROUTER", "Cradlepoint|AirLink", nullptr,
      "A rugged in-vehicle router, the kind that gives a patrol car its own WiFi and links its cameras and laptop to the station. Buses, ambulances and utility trucks carry the same boxes, so it is a hint, graded low." },
    { DetectionType::AXON, "AXON SIGNAL", "Axon-Signal", nullptr,
      "Axon Signal: small Bluetooth sensors on holsters and patrol cars that tell nearby body cameras to start recording when a gun is drawn or the lights go on. Graded low." },
    { DetectionType::AXON, "AXON BODY CAM", "Axon-BWC", nullptr,
      "An Axon body camera, by the BWCDEVICE tag it puts in its own Bluetooth broadcast. Real cams carried it in field tests, and it survives Axon changing addresses. If a camera is recording, it is this." },
    { DetectionType::AXON, "BODYWORN", "BodyWorn", "BodyWorn",
      "Utility's BodyWorn: a police body camera system. This is its activation remote, which officers clip on to start the camera recording." },
    { DetectionType::AXON, "WATCHGUARD", "WatchGuard", nullptr,
      "A radio on WatchGuard Video's block: Motorola's police body cameras and in-car video. Not the firewall company of the same name." },
    { DetectionType::AXON, "DIGITAL ALLY", "DigitalAlly", nullptr,
      "A radio on Digital Ally's block: body cameras and in-car video for police, ambulance and security fleets." },

    // ---- ALPR ---------------------------------------------------------------
    { DetectionType::ALPR, "MOTOROLA", "ALPR-Mtrla", nullptr,
      "A radio on a Motorola Solutions block. Motorola owns Vigilant, a big plate-reader maker, but also makes police radios and much else: a possible plate reader, not a sure one." },
    { DetectionType::ALPR, "GENETEC", "ALPR-Gentec", nullptr,
      "A radio on a Genetec block. Genetec's AutoVu reads license plates for police and parking enforcement, often from cameras mounted on patrol cars." },

    // ---- SKIMMER: names first -- over Bluetooth the label is just "BLE" -------
    { DetectionType::SKIMMER, "HC-05 MODULE", "Skim-HC05", "HC-0",
      "An HC-series Bluetooth serial module: the $3 part hidden in gas-pump and ATM skimmers. Hobby projects use it too, so where you are matters. At a pump, take it seriously." },
    { DetectionType::SKIMMER, "RN42 MODULE", nullptr, "RN42",
      "An RN42 Bluetooth serial module, common in electronics projects and in older card skimmers. Near a card reader, worth a second look." },
    { DetectionType::SKIMMER, "BT04-A", nullptr, "BT04",
      "A BT04-A Bluetooth serial module, a cheap HC-05 alternative. Fine inside a robot; suspicious inside a card reader." },
    { DetectionType::SKIMMER, "LINVOR", "Skim-Linvor", "linvor",
      "An HC-06-style module on the prefix its linvor boards use: a Bluetooth serial link of the kind skimmers use to hand over stolen card numbers." },
    { DetectionType::SKIMMER, "BT SERIAL", "Skim-SPP|Skim-CSR", nullptr,
      "Something offering a Bluetooth serial port, the link skimmers use to pass on stolen card data. Printers, scanners and car dongles use it too -- where you are is the tell." },

    // ---- CAMERA -------------------------------------------------------------
    { DetectionType::CAMERA, "WYZE", "Wyze", nullptr,
      "A Wyze camera or smart-home device, on Wyze's own registered block. Cheap, popular home cameras, often pointed at front doors and driveways." },
    { DetectionType::CAMERA, "SIMPLISAFE", "SimpliSafe", nullptr,
      "A SimpliSafe home-security device, on SimpliSafe's own registered block. It may be one of their cameras or their video doorbell, or the alarm's base station or keypad: they all share the block." },
    { DetectionType::CAMERA, "WYZE MODULE", "Wyze-Mod", nullptr,
      "A wireless module of the kind Wyze builds its cameras around. Other gadgets use it too, so this one is a maybe." },
    { DetectionType::CAMERA, "AMAZON", "Amazon", nullptr,
      "A radio on an Amazon block. Could be a Blink camera or a Ring device -- or an Echo, Fire TV or Kindle. Amazon's range is wide, so this is a medium guess." },
    { DetectionType::CAMERA, "HIKVISION", "Hikvision", nullptr,
      "A Hikvision camera: the world's biggest CCTV maker, barred from US government use over security and human-rights concerns, and common in shops and apartments." },
    { DetectionType::CAMERA, "REALTEK", "Realtek", nullptr,
      "A Realtek WiFi chip. Plenty of cheap IP cameras use one -- so do routers, TVs and laptops. A weak camera guess." },
    { DetectionType::CAMERA, "ARLO", "Arlo", nullptr,
      "An Arlo wireless security camera, battery powered and usually mounted outside homes." },
    { DetectionType::CAMERA, "BLINK", "Blink", nullptr,
      "A Blink camera or doorbell, Amazon's budget home-camera brand, usually battery powered on porches and windowsills." },
    { DetectionType::CAMERA, "TUYA", "Tuya", nullptr,
      "A Tuya-based smart device. Tuya makes the module inside countless no-name cameras, plugs and bulbs sold under hundreds of brands: a camera is one possibility." },
    { DetectionType::CAMERA, "VERKADA", "Verkada", nullptr,
      "A Verkada camera: cloud-managed cameras in schools, offices and shops. A 2021 breach exposed live feeds from about 150,000 of them." },
    { DetectionType::CAMERA, "AVIGILON", "Avigilon", nullptr,
      "An Avigilon camera, Motorola Solutions' business video line, found in offices, campuses and city systems." },
    { DetectionType::CAMERA, "AXIS", "Axis", nullptr,
      "An Axis Communications network camera, from one of the oldest professional CCTV makers. Common in shops, transit and city surveillance." },
    { DetectionType::CAMERA, "I-PRO", "i-PRO", nullptr,
      "An i-PRO camera: Panasonic's old security business, making CCTV and police body cameras. Found in cities, campuses and patrol cars." },
    { DetectionType::CAMERA, "DASHCAM", "Viofo|BlackVue|70mai|Thinkware|Nextbase|Vantrue|Rove|Redtiger", nullptr,
      "A dashcam's own WiFi, up for its phone app: the car it sits in is close and its camera is rolling. They film whatever is ahead, you included; some film the cabin too. Most only put the network up while the app is open." },
    { DetectionType::CAMERA, "FLEET CAMERA", "Lytx|Samsara", nullptr,
      "A fleet camera of the kind in delivery vans and trucks: road-facing, often driver-facing too, uploading clips over cellular. Samsara's block also covers its plain vehicle trackers, so that one is a weaker guess." },
    { DetectionType::CAMERA, "HIDDEN CAMERA", "P2P-Cam|HDWifiCam|LookCam|TinyCam|V380|YCC365|Sricam|Yoosee|Zosi", nullptr,
      "A cheap WiFi camera in setup mode, putting up its own network under a name from its app's firmware. The mini and hidden cameras sold for home security do this until paired. In a rental, worth a look round the room." },

    // ---- META ---------------------------------------------------------------
    { DetectionType::META, "RAY-BAN META", "RayBanMeta", nullptr,
      "Ray-Ban Meta glasses, matched on the Bluetooth ID only they send. They take photos and video from the wearer's eye line; a small white LED is the only warning." },
    { DetectionType::META, "META DEVICE", "Meta|Meta-Tech", nullptr,
      "A Bluetooth radio from Meta. It could be camera glasses -- or a Quest headset or controller, which carry the same ID. Look for glasses before worrying." },
    { DetectionType::META, "LUXOTTICA", "Luxottica", nullptr,
      "A radio from Luxottica, the eyewear giant behind Ray-Ban and Oakley, which builds Meta's camera glasses." },
    { DetectionType::META, "SPECTACLES", "Snap", nullptr,
      "Snap Spectacles: camera glasses from Snapchat's maker that record clips from the wearer's point of view." },
    { DetectionType::META, "ROKID", "Rokid", nullptr,
      "A radio on the block registered to Rokid's parent company. Rokid Glasses carry a camera; its other headsets do not. A weak guess, graded low." },
    { DetectionType::META, "VUZIX", "Vuzix", nullptr,
      "Vuzix smart glasses, sold mostly to warehouses, hospitals and police. Many models have a camera." },
};
const uint8_t kDeviceCount = sizeof(kDevices) / sizeof(kDevices[0]);

// Is `s` in the '|'-separated list -- exactly, or as a prefix of it when
// `prefix` is set, ignoring case for names (a person typed those).
static bool inList(const char* list, const char* s, bool prefix) {
    if (!list || !s || !s[0]) return false;
    const size_t sl = strlen(s);
    while (*list) {
        const char* end = strchr(list, '|');
        const size_t n = end ? (size_t)(end - list) : strlen(list);
        if (prefix ? (sl >= n && strncasecmp(s, list, n) == 0)
                   : (sl == n && strncmp(s, list, n) == 0))
            return true;
        if (!end) break;
        list = end + 1;
    }
    return false;
}

const Device* find(DetectionType t, const char* vendor, const char* name) {
    for (uint8_t i = 0; i < kDeviceCount; i++)
        if (kDevices[i].type == t && inList(kDevices[i].names, name, true)) return &kDevices[i];
    for (uint8_t i = 0; i < kDeviceCount; i++)
        if (kDevices[i].type == t && inList(kDevices[i].vendors, vendor, false)) return &kDevices[i];
    return nullptr;
}

}
