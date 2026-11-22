// SquachWatch-CM0 -- ble_scan: see ble_scan.h.
//
// The Bluetooth socket ABI is not in the kernel's exported UAPI headers
// (it lives in BlueZ's <bluetooth/hci.h>, which the device build does not
// need to install), so the handful of values used are spelled out here,
// each with where it comes from. ble_scan_test.cpp pins every one of them
// as literal bytes -- this port has had hand-rolled constants wrong before.
#include "ble_scan.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <atomic>
#include <deque>
#include <mutex>

#include <poll.h>
#include <pthread.h>
#include <sys/auxv.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

namespace {

// <bluetooth/bluetooth.h>, <bluetooth/hci.h> (BlueZ) / linux net/bluetooth:
const int      BT_AF_BLUETOOTH  = 31;   // AF_BLUETOOTH (include/linux/socket.h)
const int      BT_BTPROTO_HCI   = 1;    // BTPROTO_HCI
const int      BT_SOL_HCI       = 0;    // SOL_HCI
const int      BT_HCI_FILTER    = 2;    // HCI_FILTER sockopt
const uint16_t BT_CHANNEL_RAW   = 0;    // HCI_CHANNEL_RAW
// H:4 packet indicators (Core Vol 4 Part A §2).
const uint8_t  PKT_COMMAND = 0x01, PKT_EVENT = 0x04;
// Events (Core Vol 4 Part E §7.7).
const uint8_t  EVT_CMD_COMPLETE = 0x0E, EVT_CMD_STATUS = 0x0F, EVT_LE_META = 0x3E;
const uint8_t  LE_SUB_ADV_REPORT = 0x02;
// LE Controller commands, OGF 0x08 (§7.8.10, §7.8.11).
const uint16_t OP_LE_SET_SCAN_PARAMS = (0x08 << 10) | 0x000B;   // 0x200B
const uint16_t OP_LE_SET_SCAN_ENABLE = (0x08 << 10) | 0x000C;   // 0x200C
// Legacy advertising, OGF 0x08 (§7.8.5, §7.8.7, §7.8.9).
const uint16_t OP_LE_SET_ADV_PARAMS  = (0x08 << 10) | 0x0006;   // 0x2006
const uint16_t OP_LE_SET_ADV_DATA    = (0x08 << 10) | 0x0008;   // 0x2008
const uint16_t OP_LE_SET_ADV_ENABLE  = (0x08 << 10) | 0x000A;   // 0x200A
// Informational, OGF 0x04 (§7.4.6).
const uint16_t OP_READ_BD_ADDR       = (0x04 << 10) | 0x0009;   // 0x1009

struct sockaddr_hci_t {          // struct sockaddr_hci
    sa_family_t hci_family;
    unsigned short hci_dev;
    unsigned short hci_channel;
};
struct hci_filter_t {            // struct hci_filter / kernel hci_ufilter
    uint32_t type_mask;
    uint32_t event_mask[2];
    uint16_t opcode;
};

hci_filter_t makeFilter(bool reports) {
    hci_filter_t f;
    memset(&f, 0, sizeof f);
    f.type_mask = 1u << PKT_EVENT;
    auto ev = [&f](uint8_t e) { f.event_mask[e >> 5] |= 1u << (e & 31); };
    if (reports) ev(EVT_LE_META);
    else { ev(EVT_CMD_COMPLETE); ev(EVT_CMD_STATUS); }
    return f;
}

// NimBLE-Arduino's own conversion (NimBLEScan::setInterval: ms * 16 / 10),
// clamped to the spec's 0x0004..0x4000 (2.5 ms .. 10.24 s).
uint16_t msToUnits(uint16_t ms) {
    uint32_t u = (uint32_t)ms * 16u / 10u;
    if (u < 0x0004) u = 0x0004;
    if (u > 0x4000) u = 0x4000;
    return (uint16_t)u;
}

size_t buildCmd(uint8_t* out, uint16_t op, const uint8_t* params, uint8_t plen) {
    out[0] = PKT_COMMAND;
    out[1] = (uint8_t)(op & 0xFF);
    out[2] = (uint8_t)(op >> 8);
    out[3] = plen;
    if (plen) memcpy(out + 4, params, plen);
    return 4u + plen;
}

size_t buildParams(uint8_t* out, bool active, uint16_t intervalMs, uint16_t windowMs) {
    uint16_t itvl = msToUnits(intervalMs), win = msToUnits(windowMs);
    if (win > itvl) win = itvl;                  // spec: window <= interval
    const uint8_t p[7] = {
        (uint8_t)(active ? 0x01 : 0x00),         // LE_Scan_Type
        (uint8_t)(itvl & 0xFF), (uint8_t)(itvl >> 8),
        (uint8_t)(win & 0xFF),  (uint8_t)(win >> 8),
        0x00,                                    // Own_Address_Type: public
        0x00,                                    // Scanning_Filter_Policy: all
    };
    return buildCmd(out, OP_LE_SET_SCAN_PARAMS, p, sizeof p);
}

size_t buildEnable(uint8_t* out, bool enable, bool filterDup) {
    const uint8_t p[2] = { (uint8_t)(enable ? 1 : 0), (uint8_t)(filterDup ? 1 : 0) };
    return buildCmd(out, OP_LE_SET_SCAN_ENABLE, p, sizeof p);
}

// The squad advert: ADV_NONCONN_IND (the firmware sets both modes NON),
// public own address (NimBLE's default -- the BD_ADDR the mesh nonce
// carries), all three channels, no whitelist. Intervals in 0.625 ms units
// (the firmware already converts), clamped to the legacy 0x0020..0x4000.
size_t buildAdvParams(uint8_t* out, uint16_t minUnits, uint16_t maxUnits) {
    auto clamp = [](uint16_t u) -> uint16_t { return u < 0x0020 ? 0x0020 : (u > 0x4000 ? 0x4000 : u); };
    uint16_t lo = clamp(minUnits), hi = clamp(maxUnits);
    if (hi < lo) hi = lo;
    const uint8_t p[15] = {
        (uint8_t)(lo & 0xFF), (uint8_t)(lo >> 8),
        (uint8_t)(hi & 0xFF), (uint8_t)(hi >> 8),
        0x03,                                    // ADV_NONCONN_IND
        0x00,                                    // Own_Address_Type: public
        0x00, 0, 0, 0, 0, 0, 0,                  // Peer (unused for NONCONN)
        0x07,                                    // channels 37, 38, 39
        0x00,                                    // filter policy: none
    };
    return buildCmd(out, OP_LE_SET_ADV_PARAMS, p, sizeof p);
}

// Data_Length then always 31 bytes (zero-padded), as the spec lays it out.
size_t buildAdvData(uint8_t* out, const uint8_t* ad, uint8_t len) {
    uint8_t p[32] = { 0 };
    if (len > 31) len = 31;
    p[0] = len;
    if (len) memcpy(p + 1, ad, len);
    return buildCmd(out, OP_LE_SET_ADV_DATA, p, sizeof p);
}

size_t buildAdvEnable(uint8_t* out, bool enable) {
    const uint8_t p[1] = { (uint8_t)(enable ? 1 : 0) };
    return buildCmd(out, OP_LE_SET_ADV_ENABLE, p, sizeof p);
}

// ------------------------------------------------------------------ state

struct State {
    int  cmdSock = -1, rptSock = -1;
    int  dev = -1;
    pthread_t thread = 0;
    bool threadStarted = false;
    bool loggedFail = false;
    unsigned long long queued = 0, dropped = 0;
};
State s;
std::atomic<bool> s_running{false};
std::atomic<bool> s_enabled{false};

const size_t Q_CAP = 1024;
std::mutex s_qMu;
std::deque<cm0::BleAdvRec> s_q;

// LE Meta / Advertising Report (§7.7.65.2): subevent, Num_Reports, then
// each report as {Event_Type, Address_Type, Address[6], Data_Length,
// Data[len], RSSI} -- the per-report layout every controller sends and
// BlueZ/the kernel parse (Num_Reports is 1 in practice). Returns reports
// queued, -1 when the packet is not a well-formed report event.
int parseEvent(const uint8_t* p, size_t n) {
    if (n < 5 || p[0] != PKT_EVENT || p[1] != EVT_LE_META) return -1;
    const size_t plen = p[2];
    if (3 + plen > n || plen < 2 || p[3] != LE_SUB_ADV_REPORT) return -1;
    const uint8_t* q = p + 3;
    const uint8_t* end = q + plen;
    const uint8_t num = q[1];
    q += 2;
    int got = 0;
    for (uint8_t i = 0; i < num; i++) {
        if (q + 9 > end) return got ? got : -1;
        cm0::BleAdvRec r;
        r.evtType  = q[0];
        r.addrType = q[1];
        memcpy(r.addr, q + 2, 6);
        r.len = q[8];
        q += 9;
        if (r.len > 31 || q + r.len + 1 > end) return got ? got : -1;
        memcpy(r.data, q, r.len);
        q += r.len;
        const int8_t rssi = (int8_t)q[0];
        r.rssi = rssi == 127 ? (int8_t)-127 : rssi;   // 127 = not available
        q += 1;
        std::lock_guard<std::mutex> lk(s_qMu);
        if (s_q.size() >= Q_CAP) { s.dropped++; continue; }
        s_q.push_back(r);
        s.queued++;
        got++;
    }
    return got;
}

void* feedThread(void*) {
    uint8_t buf[300];
    while (s_running.load()) {
        struct pollfd pfd = { s.rptSock, POLLIN, 0 };
        const int pr = poll(&pfd, 1, 100);
        if (pr <= 0) continue;
        for (int i = 0; i < 64; i++) {
            const ssize_t n = recv(s.rptSock, buf, sizeof buf, MSG_DONTWAIT);
            if (n < 0) break;
            if (s_enabled.load()) (void)parseEvent(buf, (size_t)n);
        }
    }
    return nullptr;
}

int openSock(int dev, bool reports) {
    const int fd = socket(BT_AF_BLUETOOTH, SOCK_RAW | SOCK_CLOEXEC, BT_BTPROTO_HCI);
    if (fd < 0) return -1;
    sockaddr_hci_t a;
    memset(&a, 0, sizeof a);
    a.hci_family = BT_AF_BLUETOOTH;
    a.hci_dev = (unsigned short)dev;
    a.hci_channel = BT_CHANNEL_RAW;
    const hci_filter_t f = makeFilter(reports);
    if (bind(fd, (struct sockaddr*)&a, sizeof a) < 0 ||
        setsockopt(fd, BT_SOL_HCI, BT_HCI_FILTER, &f, sizeof f) < 0) {
        const int e = errno;
        close(fd);
        errno = e;
        return -1;
    }
    return fd;
}

// Send one command on the command socket and wait for its Command Complete
// (status = first return parameter) or a failing Command Status. Returns
// the HCI status (0 = success), or -errno for a socket failure/timeout.
// `ret`, when given, receives the return parameters after the status (up
// to `retCap` bytes; `*retLen` how many).
int command(const uint8_t* pkt, size_t len, uint8_t* ret = nullptr,
            size_t retCap = 0, size_t* retLen = nullptr) {
    const uint16_t op = (uint16_t)(pkt[1] | (pkt[2] << 8));
    // Drop stale events (a completion to an earlier timed-out command).
    uint8_t buf[260];
    while (recv(s.cmdSock, buf, sizeof buf, MSG_DONTWAIT) > 0) {}
    if (write(s.cmdSock, pkt, len) != (ssize_t)len) return -errno;
    struct timespec t0, now;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (;;) {
        clock_gettime(CLOCK_MONOTONIC, &now);
        const long el = (now.tv_sec - t0.tv_sec) * 1000L + (now.tv_nsec - t0.tv_nsec) / 1000000L;
        if (el >= 1000) return -ETIMEDOUT;
        struct pollfd pfd = { s.cmdSock, POLLIN, 0 };
        if (poll(&pfd, 1, (int)(1000 - el)) <= 0) continue;
        const ssize_t n = recv(s.cmdSock, buf, sizeof buf, 0);
        if (n < 0) { if (errno == EINTR) continue; return -errno; }
        if (n >= 7 && buf[0] == PKT_EVENT && buf[1] == EVT_CMD_COMPLETE &&
            (uint16_t)(buf[4] | (buf[5] << 8)) == op) {
            if (ret) {
                size_t m = (size_t)n - 7;
                if (m > retCap) m = retCap;
                memcpy(ret, buf + 7, m);
                if (retLen) *retLen = m;
            }
            return buf[6];
        }
        if (n >= 7 && buf[0] == PKT_EVENT && buf[1] == EVT_CMD_STATUS &&
            (uint16_t)(buf[5] | (buf[6] << 8)) == op && buf[3] != 0)
            return buf[3];
    }
}

// hci0 unless SQUACH_BLE_DEV=hciN (ignored in a privileged process, like
// SQUACH_WIFI_IF). -1 when the controller is not there.
int findDev() {
    int dev = 0;
    const char* e = getauxval(AT_SECURE) ? nullptr : getenv("SQUACH_BLE_DEV");
    if (e && *e) {
        if (strncmp(e, "hci", 3) != 0 || !e[3] || strlen(e) > 6) return -1;
        for (const char* c = e + 3; *c; c++) if (*c < '0' || *c > '9') return -1;
        dev = atoi(e + 3);
    }
    char path[48];
    snprintf(path, sizeof path, "/sys/class/bluetooth/hci%d", dev);
    return access(path, F_OK) == 0 ? dev : -1;
}

void failOnce(const char* what, int err) {
    if (s.loggedFail) return;
    s.loggedFail = true;
    if (err < 0)
        std::fprintf(stderr, "[ble] %s: %s%s\n", what, strerror(-err),
                     (err == -EPERM || err == -EACCES) && geteuid() != 0
                         ? " (not root: run with sudo, or `make caps`)" : "");
    else
        std::fprintf(stderr, "[ble] %s: HCI status 0x%02X\n", what, err);
}

// Advertising refusals: one line per distinct (command, status), so a
// controller that keeps refusing does not flood the log as the firmware
// retries -- but a new kind of refusal is still seen.
bool s_advOn = false;
void advFail(const char* what, int st) {
    static const char* lastWhat = nullptr;
    static int lastSt = 0;
    if (what == lastWhat && st == lastSt) return;
    lastWhat = what; lastSt = st;
    if (st < 0) std::fprintf(stderr, "[ble] %s: %s\n", what, strerror(-st));
    else        std::fprintf(stderr, "[ble] %s refused: HCI status 0x%02X\n", what, st);
}

bool openAll() {
    if (s.threadStarted) return true;
    s.dev = findDev();
    if (s.dev < 0) { failOnce("no Bluetooth controller (hci0 / SQUACH_BLE_DEV)", -ENODEV); return false; }
    s.cmdSock = openSock(s.dev, false);
    if (s.cmdSock < 0) { failOnce("HCI command socket", -errno); return false; }
    s.rptSock = openSock(s.dev, true);
    if (s.rptSock < 0) {
        failOnce("HCI report socket", -errno);
        close(s.cmdSock); s.cmdSock = -1;
        return false;
    }
    s_running = true;
    if (pthread_create(&s.thread, nullptr, feedThread, nullptr) != 0) {
        s_running = false;
        close(s.cmdSock); close(s.rptSock);
        s.cmdSock = s.rptSock = -1;
        failOnce("feed thread", -EAGAIN);
        return false;
    }
    s.threadStarted = true;
    return true;
}

}   // namespace

namespace cm0 {

bool bleScanStart(bool active, uint16_t intervalMs, uint16_t windowMs, bool filterDup) {
    if (!openAll()) return false;
    uint8_t pkt[16];
    // Parameters may only change while scanning is off; turn it off first
    // (ours from a previous start, or the kernel's own background scan).
    // Its status is not interesting: "already off" is fine.
    (void)command(pkt, buildEnable(pkt, false, false));
    s_enabled = false;
    int st = command(pkt, buildParams(pkt, active, intervalMs, windowMs));
    if (st != 0) { failOnce("LE Set Scan Parameters refused", st); return false; }
    st = command(pkt, buildEnable(pkt, true, filterDup));
    if (st != 0) { failOnce("LE Set Scan Enable refused", st); return false; }
    s_enabled = true;
    std::fprintf(stderr, "[ble] scanning on hci%d (%s, %u/%u ms, dup filter %s)\n",
                 s.dev, active ? "active" : "passive", (unsigned)intervalMs,
                 (unsigned)windowMs, filterDup ? "on" : "off");
    return true;
}

void bleScanStop() {
    if (!s.threadStarted || !s_enabled.load()) return;
    s_enabled = false;
    uint8_t pkt[8];
    (void)command(pkt, buildEnable(pkt, false, false));
    std::lock_guard<std::mutex> lk(s_qMu);
    s_q.clear();
}

void bleScanShutdown() {
    if (!s.threadStarted) return;
    bleAdvStop();   // never leave the controller beaconing after exit
    bleScanStop();
    s_running = false;
    pthread_join(s.thread, nullptr);
    s.threadStarted = false;
    close(s.cmdSock); close(s.rptSock);
    s.cmdSock = s.rptSock = -1;
    std::fprintf(stderr, "[ble] down (%llu reports queued, %llu dropped)\n", s.queued, s.dropped);
}

size_t bleScanDrain(std::vector<BleAdvRec>& out, size_t max) {
    std::lock_guard<std::mutex> lk(s_qMu);
    size_t n = 0;
    while (n < max && !s_q.empty()) {
        out.push_back(s_q.front());
        s_q.pop_front();
        n++;
    }
    return n;
}

// ----------------------------------------------------- own address, adverts

bool bleReadBdAddr(uint8_t out[6]) {
    if (!openAll()) return false;
    uint8_t pkt[8], ret[6];
    size_t got = 0;
    const int st = command(pkt, buildCmd(pkt, OP_READ_BD_ADDR, nullptr, 0), ret, sizeof ret, &got);
    if (st != 0 || got != 6) { failOnce("Read BD_ADDR", st ? st : -EPROTO); return false; }
    memcpy(out, ret, 6);   // LSB first, as on the wire = NimBLE's val order
    return true;
}

int bleAdvStart(const uint8_t* ad, uint8_t len, uint16_t minUnits, uint16_t maxUnits) {
    if (!openAll()) return 0;   // no controller: the stub radio (logged once)
    uint8_t pkt[48];
    // Parameters may not change while advertising (§7.8.5): off first.
    // "Already off" is not an error worth reporting.
    (void)command(pkt, buildAdvEnable(pkt, false));
    s_advOn = false;
    int st = command(pkt, buildAdvParams(pkt, minUnits, maxUnits));
    if (st != 0) { advFail("LE Set Advertising Parameters", st); return -1; }
    st = command(pkt, buildAdvData(pkt, ad, len));
    if (st != 0) { advFail("LE Set Advertising Data", st); return -1; }
    st = command(pkt, buildAdvEnable(pkt, true));
    if (st != 0) { advFail("LE Set Advertising Enable", st); return -1; }
    s_advOn = true;
    return 1;
}

void bleAdvStop() {
    if (!s.threadStarted || !s_advOn) return;
    uint8_t pkt[8];
    (void)command(pkt, buildAdvEnable(pkt, false));
    s_advOn = false;
}

// ------------------------------------------------------------ test hooks

int bleScanTestParse(const uint8_t* pkt, size_t len) { return parseEvent(pkt, len); }

size_t bleScanTestBuildParams(uint8_t* out, bool active, uint16_t intervalMs, uint16_t windowMs) {
    return buildParams(out, active, intervalMs, windowMs);
}

size_t bleScanTestBuildEnable(uint8_t* out, bool enable, bool filterDup) {
    return buildEnable(out, enable, filterDup);
}

size_t bleScanTestBuildAdvParams(uint8_t* out, uint16_t minUnits, uint16_t maxUnits) {
    return buildAdvParams(out, minUnits, maxUnits);
}
size_t bleScanTestBuildAdvData(uint8_t* out, const uint8_t* ad, uint8_t len) {
    return buildAdvData(out, ad, len);
}
size_t bleScanTestBuildAdvEnable(uint8_t* out, bool enable) { return buildAdvEnable(out, enable); }
size_t bleScanTestBuildReadBdAddr(uint8_t* out) { return buildCmd(out, OP_READ_BD_ADDR, nullptr, 0); }

size_t bleScanTestFilter(uint8_t* out, bool reports) {
    const hci_filter_t f = makeFilter(reports);
    memcpy(out, &f, 14);
    return 14;
}

}   // namespace cm0
