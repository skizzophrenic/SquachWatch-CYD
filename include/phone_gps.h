// SquachWatch-CYD — the Waveshare 2.06's GPS, borrowed from a phone over Bluetooth.
//
// The watch has no GPS, and an iPhone cannot send the squad's FIX advert (iOS
// apps cannot put manufacturer data in an advert). So while WARDRIVE is on the
// watch also advertises as "SquachWatch GPS", connectable, with one service. A
// web page on the phone (web-flasher/wardrive/index.html, opened in a Web
// Bluetooth browser such as Bluefy on iOS) connects to it and writes the phone's
// position about once a second:
//
//   LOC   (write, write without response), 16 bytes little-endian:
//         int32 lat7, int32 lon7   degrees x 10^7
//         uint16 accM              horizontal accuracy, metres
//         int16 altM               altitude, metres
//         uint32 epoch             the phone's UTC seconds
//   STAT  (read, notify), text: "state=3;rows=123;wifi=45;fix=1;file=/wigle-..."
//
// Each position goes to Gnss::phone(), so the wardrive rows on the card get
// real coordinates while one is fresh (WS_FIX_MS in wardrive.cpp). A phone
// clock more than two seconds from the watch's sets the watch's.
//
// While the link owns the advertiser the squad's beacon is off (owner()).
#pragma once
#include <stdint.h>

namespace PhoneGps {

// Registers the service. Needs the BLE scan stopped (main.cpp brackets it
// with the engine's update-radio calls); once per boot is enough.
bool registerService();
bool registered();
// Advertising on or off; off also drops the phone.
void setOn(bool on);
bool on();
// True while this owns the advertiser: the mesh's beacon stays off.
bool owner();
bool connected();
// From loop(): applies a position the phone wrote, keeps the advert up
// between connections, and sends STAT every two seconds.
void tick(uint32_t nowMs);
// millis() of the last position the phone sent; 0 if none this boot.
uint32_t lastFixMs();

}  // namespace PhoneGps
