// SquachWatch-CM0 -- Serial1/Serial2 on real serial ports (the GPS).
//
// The firmware's CYD_GPS path reads NMEA from CYD_GPS_SERIAL (Serial2):
// begin(baud) / available() / read() / end(), and updateBaudRate() when it
// hunts for the module's speed. It never transmits (TX is -1 on the CYD), so
// this is receive-only.
//
//   Serial2 -> the GPS: SQUACH_GPS_DEV (ignored when AT_SECURE) or
//              /dev/serial0 -- the Cardputer Zero's Grove UART (ttyS0).
//   Serial1 -> nothing (available() is 0).
//
// With SQUACHSIM_NMEA set, both replay that file exactly as the firmware's
// sim does (the sim's own class, renamed by arduino_cm0.h) -- the test path
// for wardriving without sky.
//
// The port is opened ONCE, by gpsSerialPreopen() before the UI thread drops
// to the user's file identity (user_fs.h): /dev/ttyS0 is root:dialout 0660,
// and an fsuid/fsgid switch keeps root's supplementary groups, so an open
// after it would be refused under sudo. begin()/end()/updateBaudRate() only
// reconfigure that descriptor. Without that call (--shot, --selftest) the
// real port is never touched; the NMEA replay still works.
#pragma once
#include <cstddef>
#include <cstdint>

class HardwareSerial {
public:
    explicit HardwareSerial(int which) : _which(which) {}
    void begin(unsigned long baud, uint32_t config = 0, int8_t rx = -1, int8_t tx = -1);
    void end();
    void updateBaudRate(unsigned long baud);
    // The firmware enlarges the ESP32 UART's 256-byte RX FIFO for a
    // 115200-baud GPS (v1.43). The tty's kernel buffer is 4 KB already.
    size_t setRxBufferSize(size_t n) { return n; }
    int  available();
    int  read();
    size_t write(const uint8_t* data, size_t len) { (void)data; return len; }   // receive-only
    size_t write(uint8_t b) { (void)b; return 1; }
    void flush() {}

private:
    int     _which;
    bool    _open = false;    // between begin() and end()
    uint8_t _buf[512];
    size_t  _pos = 0, _len = 0;
    void    fill();
};

inline HardwareSerial Serial1(1);
inline HardwareSerial Serial2(2);

namespace cm0 {
// Open the GPS port now (see above). Logs what it found; false = no port.
bool gpsSerialPreopen();
}
