// SquachWatch-CM0 -- Serial1/Serial2 on real serial ports (see gps_serial.h).
#include "gps_serial.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <sys/auxv.h>
#include <termios.h>
#include <unistd.h>

namespace {

int  s_fd = -1;
bool s_enabled = false;   // the live harness only (gpsSerialPreopen)
bool s_tried = false, s_loggedFail = false;

bool secure() { return getauxval(AT_SECURE) != 0; }

const char* replayPath() {
    if (secure()) return nullptr;
    const char* p = getenv("SQUACHSIM_NMEA");
    return p && *p ? p : nullptr;
}

std::string devPath() {
    const char* e = secure() ? nullptr : getenv("SQUACH_GPS_DEV");
    return (e && *e) ? std::string(e) : std::string("/dev/serial0");
}

speed_t speedFor(unsigned long baud) {
    switch (baud) {
        case 4800:   return B4800;
        case 9600:   return B9600;
        case 19200:  return B19200;
        case 38400:  return B38400;
        case 57600:  return B57600;
        case 115200: return B115200;
        case 230400: return B230400;
        case 460800: return B460800;
        default:     return 0;
    }
}

// Raw 8N1, receive enabled, no flow control, non-blocking reads.
bool configure(unsigned long baud) {
    if (s_fd < 0) return false;
    const speed_t sp = speedFor(baud);
    if (!sp) { std::fprintf(stderr, "[gps] unsupported baud %lu\n", baud); return false; }
    struct termios t;
    if (tcgetattr(s_fd, &t) != 0) return false;
    cfmakeraw(&t);
    t.c_cflag |= CLOCAL | CREAD;
    t.c_cflag &= ~(CSTOPB | CRTSCTS | PARENB);
    t.c_cc[VMIN] = 0;
    t.c_cc[VTIME] = 0;
    cfsetispeed(&t, sp);
    cfsetospeed(&t, sp);
    if (tcsetattr(s_fd, TCSANOW, &t) != 0) return false;
    tcflush(s_fd, TCIFLUSH);   // what arrived at the old speed is noise
    return true;
}

bool openPort() {
    if (s_fd >= 0) return true;
    if (!s_enabled || s_tried) return false;
    s_tried = true;
    const std::string p = devPath();
    const int fd = open(p.c_str(), O_RDONLY | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        if (!s_loggedFail) {
            s_loggedFail = true;
            std::fprintf(stderr, "[gps] %s: %s%s\n", p.c_str(), strerror(errno),
                         errno == EACCES ? " (needs the dialout group)" : "");
        }
        return false;
    }
    if (!isatty(fd)) {
        close(fd);
        std::fprintf(stderr, "[gps] %s is not a serial port\n", p.c_str());
        return false;
    }
    s_fd = fd;
    std::fprintf(stderr, "[gps] port %s\n", p.c_str());
    return true;
}

}   // namespace

namespace cm0 {
bool gpsSerialPreopen() {
    s_enabled = true;
    if (replayPath()) return false;   // replaying a file instead
    return openPort();
}
}

// The sim's class (arduino_cm0.h renamed it) does the replay.
static SimHardwareSerial& replayFor(int which) { return which == 1 ? SimSerial1 : SimSerial2; }

void HardwareSerial::begin(unsigned long baud, uint32_t config, int8_t rx, int8_t tx) {
    _pos = _len = 0;
    if (replayPath()) { replayFor(_which).begin(baud, config, rx, tx); _open = true; return; }
    if (_which != 2) { _open = false; return; }
    _open = openPort() && configure(baud);
    if (_open) std::fprintf(stderr, "[gps] listening at %lu baud\n", baud);
}

void HardwareSerial::end() {
    if (replayPath()) { replayFor(_which).end(); }
    _open = false;
    _pos = _len = 0;
}

void HardwareSerial::updateBaudRate(unsigned long baud) {
    if (replayPath()) { replayFor(_which).updateBaudRate(baud); return; }
    _pos = _len = 0;
    if (_open && configure(baud)) std::fprintf(stderr, "[gps] now %lu baud\n", baud);
}

void HardwareSerial::fill() {
    if (_pos < _len || !_open || s_fd < 0 || _which != 2) return;
    for (;;) {
        const ssize_t n = ::read(s_fd, _buf, sizeof _buf);
        if (n < 0 && errno == EINTR) continue;
        _pos = 0;
        _len = n > 0 ? (size_t)n : 0;
        return;
    }
}

int HardwareSerial::available() {
    if (replayPath()) return _open ? replayFor(_which).available() : 0;
    fill();
    return (int)(_len - _pos);
}

int HardwareSerial::read() {
    if (replayPath()) return _open ? replayFor(_which).read() : -1;
    fill();
    return _pos < _len ? _buf[_pos++] : -1;
}
