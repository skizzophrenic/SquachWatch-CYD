// SquachWatch-CYD -- the wardrive log straight to WiGLE, over the home WiFi.
//
// Only on the boards with room for it (SQW_WIGLE_UPLOAD: the S3 boards with
// PSRAM that wardrive -- T-Deck Plus, T-Watch S3 Plus, the Freenove S3 GPS
// builds). WiGLE only takes HTTPS, and a TLS session wants about 45 KB of
// flash and a 40 KB block of RAM that the CYDs do not have; theirs is the
// UPLOAD TO WIGLE button on the web flasher instead.
//
// How it goes. The key is WiGLE's "Encoded for use" value from
// wigle.net/account -- the API name and token, already base64'd -- sent once
// from the flasher page over USB (WIGLEKEY on the console) and kept in NVS.
// UPLOAD NOW (or AUTO UPLOAD, on the charger) pauses the radios the way a
// WiFi update does, joins the strongest saved network (the same list the
// updates use), and posts each waiting file to api.wigle.net. The SD boards
// send their session files, oldest first, and move each to /wigle-sent once
// WiGLE says it has it. The watch sends what it has kept since the last
// upload, 20,000 rows to a request, and remembers how far it got.
//
// The card and the flash are only ever read on the loop task (the card shares
// a bus with the screen); a small task does the network and is fed through a
// stream buffer.
#pragma once
#include <stdint.h>
#include <stddef.h>

#if SQW_WIGLE_UPLOAD
namespace WigleUp {

enum class State : uint8_t { IDLE, JOINING, SENDING, DONE, FAILED };

void begin();
// From loop(). `autoOk`: the main screen is up, idle, and on the charger --
// when AUTO UPLOAD may go by itself.
void tick(uint32_t nowMs, bool autoOk);

// The radios, paused and given back the way a WiFi update does it.
void setRadioHooks(void (*pause)(), void (*resume)());

// The key: WiGLE's "Encoded for use" value. False if it does not decode to
// "name:token".
bool        setKey(const char* encoded);
void        clearKey();
bool        hasKey();
const char* keyName();          // the API name part, for the row: "AID0123..."

bool start();                   // UPLOAD NOW; false (and why in failText) if it cannot
void cancel();
State       state();
bool        busy();             // JOINING or SENDING
const char* status(char* buf, size_t n);   // the row's value, whatever the state
const char* failText();

bool     autoOn();
void     toggleAuto();
uint32_t lastEpoch();           // UTC of the last upload that sent anything, 0 if never
uint32_t lastRows();
uint32_t waiting();             // files waiting (SD) or rows waiting (the watch)

// The duress wipe and WIGLE CLEAR: the key, the progress mark, the history.
void wipe();
// After the watch's log is cleared, the mark means nothing.
void forgetProgress();

}  // namespace WigleUp
#endif
