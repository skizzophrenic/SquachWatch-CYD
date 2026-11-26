// SquachWatch-CYD — LORA SETTINGS, the T-Deck's LoRa settings page.
//
// Reached ONLY from the LORA page (the menu icon in its top-left corner, where
// the main screen keeps its settings button), and BACK returns there. The main
// Settings keeps its own LoRa rows as they were. Asked for 2026-10-21: "a
// settings page for the LoRa dashboard so we can set everything".
//
// One scrolling list, 320x240, in the LORA page's 6x8 font, by section:
//   NETWORK     LISTEN, MESHTASTIC PRESET, TX POWER, HOPS, REGION, CHANNELS >
//   NAME        NAME, SHORT NAME (typed on the keyboard), ANNOUNCE NAME
//   CHATS       MESSAGE SOUND
//   DETECTIONS  SQUACHWATCH DETECTIONS (ON / OFF ON LORA PAGE / OFF),
//               ON-SCREEN DETECTIONS
//   LOOKUPS     ONLINE LOOKUPS and its three switches
//   CLEAN UP    CLEAR CHATS, FORGET NAMES (a second tap on CONFIRM? does it)
// CHANNELS opens a second list of the decoders' channel keys: add one (name,
// then key, on the keyboard) or remove one of the user's; a saved key is only
// ever shown as its first and last four characters.
//
// Scrolls by a finger's drag, or by W and S on the keyboard. A NAME row
// tapped turns into an edit line: Enter saves, Backspace deletes, an empty
// line means the default, and a tap anywhere else cancels.
//
// Built with SQUACH_LORA on the T-Deck, and in the emulator (SQUACH_LORA_FAKE).
#pragma once
#include <TFT_eSPI.h>
#include <stdint.h>

enum class LoraSetHit : uint8_t { NONE = 0, BACK };

// `fresh`: from the LORA page (top of the main list); false keeps where it
// was, for coming back from an ALERT card.
void       uiLoraSettingsInit(TFT_eSPI& t, bool fresh);
// `resting`: whether WiFi and Bluetooth scanning are resting right now
// (DetectionEngine::radiosResting()), for the DETECTIONS row to say so.
void       uiLoraSettingsTick(TFT_eSPI& t, uint32_t now, bool resting);
// A tap that was not a drag. BACK means leave for the LORA page; everything
// else is handled here.
LoraSetHit uiLoraSettingsTap(int x, int y, uint32_t now);
// A drag, in pixels: positive moves the list up (shows what is further down).
void       uiLoraSettingsScrollBy(int px);
// The keyboard: W/S scroll, unless an edit line is open, which takes every key.
void       uiLoraSettingsKey(char c, uint32_t now);
bool       uiLoraSettingsEditing();
// MESHTASTIC PRESET's tap: the next preset this band plan has (round to
// LongFast), saved, the radio re-tuned, the NodeInfo due again. Also the
// bench's LORASET PRESET.
void       uiLoraSettingsNextPreset();

// The name both chats send with: LORA SETTINGS' NAME, else the squad
// nickname, else SQUACHY (include/lora_myname.h).
const char* uiLoraMyName();
