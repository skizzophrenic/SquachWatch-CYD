// SquachWatch-CM0 -- ble_scan: passive LE scanning on the onboard
// controller over a raw HCI socket.
//
// Phase 3 of the port. The firmware's real detection.cpp drives a NimBLE
// scan (radio_stub/NimBLEScan.h); this is the Linux half. It sends the
// legacy LE Set Scan Parameters / Set Scan Enable commands to hci0 on an
// HCI_CHANNEL_RAW socket -- the same thing `hcitool lescan` does, which
// coexists with a running bluetoothd (verified on the CM0's CYW4343A2) --
// and reads LE Advertising Reports on a second raw socket.
//
// Threading (the port's invariant): commands go out synchronously on the
// UI thread (the firmware's start()/stop() calls); the feed thread only
// parses reports into a locked queue; radio_stub.cpp's cm0::blePump() hands
// them to the firmware's scan callbacks on the UI thread before loop().
//
// Privileges: sending HCI commands on a raw socket needs CAP_NET_RAW
// (root, or `make caps`).
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace cm0 {

// One legacy LE Advertising Report.
struct BleAdvRec {
    uint8_t addr[6];     // LSB first, as on the wire (= NimBLE's val order)
    uint8_t addrType;    // 0 public, 1 random, ...
    uint8_t evtType;     // 0 ADV_IND 1 DIRECT 2 SCAN_IND 3 NONCONN 4 SCAN_RSP
    int8_t  rssi;        // dBm; -127 when the controller had none (127)
    uint8_t len;
    uint8_t data[31];
};

// Configure and enable scanning. Opens the sockets and starts the feed
// thread the first time. `intervalMs`/`windowMs` are NimBLE-Arduino's
// millisecond units. False when there is no controller or it refused (the
// reason is logged once).
bool bleScanStart(bool active, uint16_t intervalMs, uint16_t windowMs, bool filterDup);

// Disable scanning (the thread and sockets stay for a restart).
void bleScanStop();

// Disable scanning, stop the thread, close the sockets. Idempotent.
void bleScanShutdown();

// UI thread: move up to `max` queued reports into `out` (appended).
size_t bleScanDrain(std::vector<BleAdvRec>& out, size_t max);

// UI thread: deliver queued reports to the firmware's scan callbacks.
// Defined in radio_stub.cpp (it owns the NimBLE stub objects).
size_t blePump(size_t maxReports = 256);

// The controller's public address (HCI Read_BD_ADDR), LSB first -- NimBLE's
// val order. False with no controller / no permission (logged once).
bool bleReadBdAddr(uint8_t out[6]);

// Legacy non-connectable advertising of `ad` (AD structures, <= 31 bytes)
// from the public address, intervals in 0.625 ms units. 1 = advertising,
// 0 = no controller (the stub radio: nothing sent), -1 = the controller
// refused (logged once per kind). UI thread only, like the scan commands.
int  bleAdvStart(const uint8_t* ad, uint8_t len, uint16_t minUnits, uint16_t maxUnits);
// Stop advertising (no-op when not on). bleScanShutdown() calls it too.
void bleAdvStop();

// ---- test hooks (ble_scan_test.cpp) ----

// Parse one HCI event packet (starting with the 0x04 packet indicator) and
// queue its advertising reports. Returns the number queued, -1 if the
// packet is not a well-formed LE Advertising Report event.
int bleScanTestParse(const uint8_t* pkt, size_t len);

// The command packets as sent, for pinning the wire format. Return length.
size_t bleScanTestBuildParams(uint8_t* out, bool active, uint16_t intervalMs,
                              uint16_t windowMs);
size_t bleScanTestBuildEnable(uint8_t* out, bool enable, bool filterDup);

// The advertising / Read_BD_ADDR command packets as sent.
size_t bleScanTestBuildAdvParams(uint8_t* out, uint16_t minUnits, uint16_t maxUnits);
size_t bleScanTestBuildAdvData(uint8_t* out, const uint8_t* ad, uint8_t len);
size_t bleScanTestBuildAdvEnable(uint8_t* out, bool enable);
size_t bleScanTestBuildReadBdAddr(uint8_t* out);

// The socket filter bytes (struct hci_filter, 14 bytes) for the report
// socket (`reports` true) or the command socket.
size_t bleScanTestFilter(uint8_t* out, bool reports);

}   // namespace cm0
