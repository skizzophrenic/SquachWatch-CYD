// SquachWatch-CYD — the SquachMesh phrase words and the canned lines.
//
// THE WORDS. A phrase is five of these, rolled by the device's hardware random
// number generator -- never chosen by a person, which is where nearly all of a
// phrase's strength would otherwise go. 314 words is about 41.5 bits per
// phrase. The rules, each checked by test/meshmsg_test.cpp except the last:
//
//   - capital letters only, three to eight of them, so every word fits a
//     picker cell and reads the same on every screen
//   - no two words share their first three letters, so a word can always be
//     named by its start, and a half-heard word is never two candidates
//   - no letter has more than 18 words, so each letter's words fit on one
//     screen of the picker at either rotation
//   - kept in alphabetical order, which is the order the picker shows them in
//   - spelled the way it is said, with no sound-alikes: read aloud across a
//     table, FREQ and PHREAK are the same word, so FREQ is not here; XENON
//     starts with a Z sound and the listener looks under the wrong letter,
//     so it is not here either
//
// Keys are derived from a phrase's TEXT, not from word positions, so words can
// be ADDED anywhere later without breaking a single existing phrase. Renaming
// or removing one breaks every phrase that used it. Don't.
//
// THE CANNED LINES are the other way round: a message carries an INDEX, so
// both ends must agree what index N means. Append only.
#include "meshmsg.h"

#if SQUACH_MESH

namespace MeshMsg {

const char* const WORDS[] = {
    "ABDUCT",   "ACIDBURN", "ALIEN",    "AMBER",    "ANALOG",   "ANTENNA",
    "APOLLO",   "ARCADE",   "ARGON",    "ASTRO",    "ATLAS",    "AURORA",
    "AVALON",   "AXIOM",
    "BACKDOOR", "BASILISK", "BAUD",     "BEACON",   "BIGFOOT",  "BINARY",
    "BLACKICE", "BLUEBOX",  "BOOTLEG",  "BRAMBLE",  "BUFFER",   "BUNYIP",
    "BYPASS",
    "CABLE",    "CARRIER",  "CASSETTE", "CEREAL",   "CHANNEL",  "CHIPTUNE",
    "CIPHER",   "CIRCUIT",  "COBALT",   "CODEC",    "COMET",    "CONSOLE",
    "CORTEX",   "COYOTE",   "CRASH",    "CRYPTID",  "CURSOR",   "CYBER",
    "DAEMON",   "DAVINCI",  "DECODER",  "DEEPWOOD", "DELTA",    "DIALTONE",
    "DIGITAL",  "DISKETTE", "DOGMAN",   "DOPPLER",  "DOVER",    "DRONE",
    "DUSK",     "DYNAMO",
    "ECHO",     "ECLIPSE",  "EMBER",    "ENCODE",   "ENIGMA",   "EPOCH",
    "ESCAPE",   "ETHER",    "EXODUS",   "EXPLOIT",  "EYESHINE",
    "FALCON",   "FIBER",    "FIREWALL", "FLATWOOD", "FLOCK",    "FLUX",
    "FOGBANK",  "FORTRAN",  "FOXFIRE",  "FROST",    "FUSION",   "FUZZ",
    "GADGET",   "GAMMA",    "GARBAGE",  "GHOST",    "GIBSON",   "GLITCH",
    "GOBLIN",   "GOPHER",   "GRAVITY",  "GRID",     "GRUNGE",   "GYRO",
    "HACKER",   "HALO",     "HANDLE",   "HARBOR",   "HEX",      "HIDEOUT",
    "HODAG",    "HOLLOW",   "HORIZON",  "HOTWIRE",  "HOWLER",   "HUNTER",
    "HYPER",
    "ICEBOX",   "IMPULSE",  "INFRARED", "INSOMNIA", "ION",      "IRIDIUM",
    "IRONWOOD", "ISOTOPE",  "IVORY",
    "JACKPOT",  "JAMMER",   "JERSEY",   "JETSET",   "JIGSAW",   "JOEY",
    "JOYSTICK", "JUKEBOX",  "JUNGLE",   "JUPITER",
    "KERNEL",   "KEYGEN",   "KILOBYTE", "KINETIC",  "KITE",     "KLAXON",
    "KRAKEN",   "KRYPTON",  "KUDZU",
    "LANTERN",  "LASER",    "LATENCY",  "LEGEND",   "LICHEN",   "LIMINAL",
    "LITHIUM",  "LOCKPICK", "LOGIC",    "LUNAR",    "LURKER",   "LYNX",
    "MAGNET",   "MALWARE",  "MANTIS",   "MATRIX",   "MEGABYTE", "MERIDIAN",
    "MESH",     "MICRO",    "MIDNIGHT", "MIRAGE",   "MODEM",    "MOKELE",
    "MONOLITH", "MOTHMAN",  "MUTANT",   "MYSTERY",
    "NANO",     "NEBULA",   "NEON",     "NESSIE",   "NETWORK",  "NEXUS",
    "NIKON",    "NIMBUS",   "NOISE",    "NOMAD",    "NORTH",    "NOVA",
    "NULL",
    "OBSIDIAN", "OCTANE",   "ODYSSEY",  "OGOPOGO",  "OMEGA",    "ONYX",
    "OPCODE",   "ORACLE",   "ORBIT",    "OSMIUM",   "OUTPOST",  "OVERRIDE",
    "OWLMAN",   "OXYGEN",   "OZONE",
    "PACKET",   "PAGER",    "PARADOX",  "PATCH",    "PAYLOAD",  "PHANTOM",
    "PHREAK",   "PINBALL",  "PIXEL",    "PLAGUE",   "PLUTO",    "POLARIS",
    "PORTAL",   "PROXY",    "PULSAR",   "PYLON",
    "QUANTUM",  "QUEST",    "QUIVER",   "QWERTY",
    "RADAR",    "RAINFALL", "RANGER",   "RAVEN",    "RECON",    "REDWOOD",
    "RELAY",    "REMOTE",   "RETRO",    "RIDDLE",   "ROBOT",    "ROCKET",
    "ROGUE",    "ROOTKIT",  "ROUTER",   "RUNWAY",   "RUSTLE",
    "SALVAGE",  "SATURN",   "SCANNER",  "SECTOR",   "SENTINEL", "SERVER",
    "SHADOW",   "SIGNAL",   "SILICON",  "SKUNKAPE", "SLEUTH",   "SMOKE",
    "SONAR",    "SPECTRUM", "SQUACHY",  "STATIC",   "SUBNET",   "SYNTH",
    "TACHYON",  "TALON",    "TANGO",    "TELNET",   "TERMINAL", "TESLA",
    "THUNDER",  "TIMBER",   "TOKEN",    "TORRENT",  "TOTEM",    "TRACKER",
    "TRIGGER",  "TROJAN",   "TUNDRA",   "TURBO",    "TWILIGHT",
    "UFO",      "ULTRA",    "UMBRA",    "UNDERTOW", "UNICODE",  "UPLINK",
    "URANIUM",  "UTOPIA",
    "VACUUM",   "VALVE",    "VAPOR",    "VECTOR",   "VELVET",   "VENOM",
    "VERTEX",   "VHS",      "VIPER",    "VIRUS",    "VISOR",    "VOLTAGE",
    "VORTEX",   "VOXEL",    "VULCAN",
    "WALKMAN",  "WARDRIVE", "WAVEFORM", "WENDIGO",  "WEREWOLF", "WHISPER",
    "WIDGET",   "WILDFIRE", "WINDMILL", "WIRETAP",  "WIZARD",   "WOLFPACK",
    "WORMHOLE",
    "XEROX",    "XRAY",
    "YETI",     "YONDER",   "YOWIE",    "YUKON",
    "ZENITH",   "ZEPHYR",   "ZEROCOOL", "ZIGZAG",   "ZIPPER",   "ZODIAC",
    "ZOMBIE",   "ZONE",     "ZULU",
};
const uint16_t WORD_N = (uint16_t)(sizeof(WORDS) / sizeof(WORDS[0]));

// Short enough for one line of one speech bubble at either rotation. Practical
// first, because a message is for saying something; a few in character,
// because it is still Squachy saying it.
const char* const CANNED[] = {
    "On my way.",
    "Where are you?",
    "All clear here.",
    "Something's nearby.",
    "Heading out.",
    "Be right back.",
    "Yes.",
    "No.",
    "Maybe.",
    "Meet at the car.",
    "Watch your back.",
    "Camera on my left.",
    "Found a Flock cam.",
    "Stay put.",
    "Come to me.",
    "Leaving now.",
    "Five minutes.",
    "Running late.",
    "Ha.",
    "Nice.",
    "Thanks.",
    "Snacks?",
    "Is it following you?",
    "Going dark.",
    // ---- 24..47, added 2026-09-12 --------------------------------------------
    // The first twenty-four were written to prove messaging worked. These are
    // written to be USED: what you actually need to say to somebody else
    // carrying one of these, in the situations this device exists for.
    // Appended, never inserted -- see the index note at the top of this file.
    "Cop car ahead.",
    "Drone overhead.",
    "ALPR on the pole.",
    "Two of them now.",
    "It's gone now.",
    "I'm safe.",
    "Don't come here.",
    "Turn around.",
    "Being followed.",
    "You okay?",
    "Still there?",
    "Can you talk?",
    "Which way?",
    "How many?",
    "Need a ride?",
    "Call when you can.",
    "Got it.",
    "On it.",
    "Not yet.",
    "Copy that.",
    "Squatch out.",
    "Big if true.",
    "Beep boop.",
    "Stay squachy.",
};
const uint8_t CANNED_N = (uint8_t)(sizeof(CANNED) / sizeof(CANNED[0]));

// ---- the picker's tabs -------------------------------------------------------
// PRESENTATION ONLY. Unlike the indices above, nothing here goes on the air, so
// these may be reordered, renamed or regrouped in any release without breaking
// a single board. The layout exists because forty-eight lines paged four at a
// time is a worse way to find "Turn around." than six labelled tabs.
//
// The tabs are the same shape as the emote picker's, deliberately: both halves
// of the message screen then work the same way, and the one you learn first
// teaches the other.
const char* const CANNED_TAB_NAME[CANNED_TABS] = {
    "GOING", "SEEN", "SAFE", "ASK", "REPLY", "SQUACH",
};

// Each row is one tab: which CANNED indices it shows, in the order shown.
// 0xFF leaves a slot empty, which nothing uses yet but costs nothing to allow.
static const uint8_t CANNED_TAB[CANNED_TABS][CANNED_PER_TAB] = {
    // GOING -- movement and timing
    {  0,  4,  5,  9, 14, 15, 16, 17 },
    // SEEN -- what you spotted, which is what this device is for
    {  3, 11, 12, 24, 25, 26, 27, 28 },
    // SAFE -- status, and the lines that matter most if it goes wrong
    {  2, 10, 13, 23, 29, 30, 31, 32 },
    // ASK -- questions
    {  1, 22, 33, 34, 35, 36, 37, 38 },
    // REPLY -- short answers
    {  6,  7,  8, 20, 39, 40, 41, 42 },
    // SQUACH -- he is still a sasquatch about it
    { 18, 19, 21, 43, 44, 45, 46, 47 },
};

uint8_t cannedAtTab(uint8_t tab, uint8_t slot) {
    if (tab >= CANNED_TABS || slot >= CANNED_PER_TAB) return 0xFF;
    const uint8_t idx = CANNED_TAB[tab][slot];
    return (idx < CANNED_N) ? idx : 0xFF;
}

} // namespace MeshMsg
#endif // SQUACH_MESH
