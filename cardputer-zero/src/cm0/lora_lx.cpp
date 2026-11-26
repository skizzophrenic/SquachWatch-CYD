// SquachWatch-CM0 -- Linux SX1262 backend for the LoraRadio shim.
// See lora_lx.h. The bring-up recipe below matches the pocket-lorascan app
// exactly (the reference driver verified on this exact hardware).
#include "lora_lx.h"
#include "lora_pkt.h"

#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <poll.h>
#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <system_error>
#include <chrono>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
#include <stdexcept>
#include <map>
#include <thread>

namespace {

constexpr uint32_t SPI_IOC_MESSAGE_1            = 0x40206b00u;
constexpr uint32_t SPI_IOC_WR_MODE              = 0x40016b01u;
constexpr uint32_t SPI_IOC_WR_BITS_PER_WORD     = 0x40016b03u;
constexpr uint32_t SPI_IOC_WR_MAX_SPEED_HZ      = 0x40046b04u;

constexpr uint32_t GPIO_V2_GET_LINE_IOCTL  = 0xc250b407u;
constexpr uint32_t GPIO_V2_LINE_SET_VALUES = 0xc010b40fu;
constexpr uint32_t GPIO_V2_LINE_GET_VALUES = 0xc010b40eu;
constexpr int GPIO_FLAG_INPUT = 0x4, GPIO_FLAG_OUTPUT = 0x8;
constexpr uint32_t GPIO_ATTR_OUTPUT_VALUES = 0x02;

constexpr int I2C_SLAVE = 0x0703;

const char* const SPI_DEV  = "/dev/spidev0.1";
const char* const GPIO_CHIP = "/dev/gpiochip0";
const char* const EXT5V    = "/sys/class/leds/ext_5v_out/brightness";
const char* const I2C_BUS  = "/dev/i2c-1";
const uint8_t PI4IO_ADDR   = 0x43;
const int PIN_BUSY = 22, PIN_IRQ = 23, PIN_RESET = 26;
const uint8_t TCXO_VOLTAGE_CODE = 0x06;   // 3.0 V
// SetDIO3AsTcxoCtrl voltage codes (datasheet): 1.6, 1.7, 1.8, 2.2, 2.4,
// 2.7, 3.0, 3.3 V -- not linear, so report in deci-volts via a table
const uint8_t TCXO_DECIVOLTS[] = {16, 17, 18, 22, 24, 27, 30, 33};

// ---- I2C / cap power ----

bool capPowerOn() {
    {
        FILE* f = std::fopen(EXT5V, "r");
        if (f) {
            char buf[32] = {0};
            if (std::fgets(buf, sizeof(buf), f)) {
                if (std::atoi(buf) <= 0) {
                    std::fclose(f);
                    FILE* g = std::fopen(EXT5V, "w");
                    if (g) { std::fprintf(g, "1\n"); std::fclose(g); }
                    std::this_thread::sleep_for(std::chrono::milliseconds(200));
                } else { std::fclose(f); }
            } else { std::fclose(f); }
        }
    }
    int fd = ::open(I2C_BUS, O_RDWR);
    if (fd < 0) { std::fprintf(stderr, "[lora] %s: %s\n", I2C_BUS, std::strerror(errno)); return false; }
    if (::ioctl(fd, I2C_SLAVE, (int)PI4IO_ADDR) < 0) { ::close(fd); return false; }
    auto rd = [&](uint8_t reg, uint8_t& out) -> bool {
        if (::write(fd, &reg, 1) != 1) return false;
        return ::read(fd, &out, 1) == 1;
    };
    uint8_t out = 0, hiz = 0, dr = 0;
    bool ok = false;
    for (int i = 0; i < 5; i++) {
        uint8_t r;
        if (rd(0x05, r) && rd(0x07, hiz) && rd(0x03, dr)) { out = r; ok = true; break; }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    if (!ok) { ::close(fd); return false; }
    auto wr = [&](uint8_t reg, uint8_t val) -> bool {
        // one 2-byte transaction: pointer byte + value. A 1-byte write() on
        // i2c-dev is its own transaction and only moves the register pointer.
        uint8_t b[2] = {reg, val};
        return ::write(fd, b, 2) == 2;
    };
    if (!wr(0x05, (uint8_t)(out | 0x01))) { ::close(fd); return false; }
    if (!wr(0x07, (uint8_t)(hiz & 0xFE))) { ::close(fd); return false; }
    if (!wr(0x03, (uint8_t)(dr | 0x01)))  { ::close(fd); return false; }
    ::close(fd);
    return true;
}

// ---- GPIO (character device, v2 API) ----

struct GpioReq {
    std::vector<uint8_t> buf;
    explicit GpioReq(const std::vector<uint32_t>& offsets, bool output, bool initial, const char* consumer) {
        buf.assign(592, 0);
        uint32_t n = offsets.size();
        for (uint32_t i = 0; i < n; i++) {
            buf[4 * i + 0] = offsets[i] & 0xFF;
            buf[4 * i + 1] = (offsets[i] >> 8) & 0xFF;
            buf[4 * i + 2] = (offsets[i] >> 16) & 0xFF;
            buf[4 * i + 3] = (offsets[i] >> 24) & 0xFF;
        }
        std::string pad(consumer);
        while (pad.size() < 32) pad += '\0';
        for (size_t i = 0; i < 32; i++) buf[256 + i] = pad[i];
        uint64_t flags = output ? (uint64_t)GPIO_FLAG_OUTPUT : (uint64_t)GPIO_FLAG_INPUT;
        for (int k = 0; k < 8; k++) buf[288 + k] = (flags >> (8 * k)) & 0xFF;
        if (output) {
            uint32_t numAttrs = 1;
            buf[296] = numAttrs & 0xFF; buf[297] = 0; buf[298] = 0; buf[299] = 0;
            uint64_t mask = (n < 64) ? ((1ULL << n) - 1ULL) : 0xFFFFFFFFFFFFFFFFULL;
            uint64_t values = initial ? mask : 0ULL;
            buf[320] = 2; buf[321] = 0; buf[322] = 0; buf[323] = 0;   // attr id = 2 (output-values)
            buf[324] = 0; buf[325] = 0; buf[326] = 0; buf[327] = 0;   // pad
            for (int k = 0; k < 8; k++) buf[328 + k] = (values >> (8 * k)) & 0xFF;
            for (int k = 0; k < 8; k++) buf[336 + k] = (mask >> (8 * k)) & 0xFF;
        }
        uint32_t num = offsets.size();
        buf[560] = num & 0xFF; buf[561] = 0; buf[562] = 0; buf[563] = 0;
    }
};

class GpioLines {
public:
    GpioLines(const std::string& chip, const std::vector<uint32_t>& offsets, bool output, bool initial, const char* consumer) {
        int fd = ::open(chip.c_str(), O_RDWR);
        if (fd < 0) throw std::runtime_error(std::string("gpiochip: ") + std::strerror(errno));
        GpioReq req(offsets, output, initial, consumer);
        if (::ioctl(fd, GPIO_V2_GET_LINE_IOCTL, req.buf.data()) < 0) { ::close(fd); throw std::runtime_error("gpiochip ioctl"); }
        ::close(fd);
        uint32_t fd32 = req.buf[588] | (req.buf[589] << 8) | (req.buf[590] << 16) | (req.buf[591] << 24);
        _fd = (int)fd32;
    }
    ~GpioLines() { if (_fd >= 0) ::close(_fd); }
    int get(uint32_t i) const {
        // gpio v2: get values ioctl writes into a 16-byte struct: u64 values @0, u64 mask @8
        uint8_t buf[16] = {0};
        buf[8] = (1u << (i & 63)) & 0xFF;   // mask
        ::ioctl(_fd, GPIO_V2_LINE_GET_VALUES, buf);
        uint64_t values = 0, mask = 0;
        for (int k = 0; k < 8; k++) { values |= (uint64_t)buf[k] << (8 * k); mask |= (uint64_t)buf[8 + k] << (8 * k); }
        if (!(mask & (1ULL << (i & 63)))) return -1;
        return (int)((values >> (i & 63)) & 1);
    }
    void set(int i, int v) const {
        uint8_t buf[16] = {0};
        uint64_t values = v ? (1ULL << i) : 0ULL;
        for (int k = 0; k < 8; k++) buf[k] = (values >> (8 * k)) & 0xFF;
        buf[8] = (1u << (i & 63)) & 0xFF;   // mask
        ::ioctl(_fd, GPIO_V2_LINE_SET_VALUES, buf);
    }
    void close() { if (_fd >= 0) { ::close(_fd); _fd = -1; } }
private:
    int _fd = -1;
};

// ---- SPI ----

class Spi {
public:
    explicit Spi(const char* path, int speed = 1000000) {
        _fd = ::open(path, O_RDWR | O_CLOEXEC);
        if (_fd < 0) throw std::runtime_error(std::string("spi: ") + std::strerror(errno));
        uint8_t mode = 0;
        ::ioctl(_fd, SPI_IOC_WR_MODE, &mode);
        uint8_t bits = 8;
        ::ioctl(_fd, SPI_IOC_WR_BITS_PER_WORD, &bits);
        int s = speed;
        ::ioctl(_fd, SPI_IOC_WR_MAX_SPEED_HZ, &s);
        _speed = s;
    }
    ~Spi() { if (_fd >= 0) ::close(_fd); }
    std::vector<uint8_t> xfer(const std::vector<uint8_t>& data) {
        const uint32_t n = data.size();
        std::vector<uint8_t> txBuf = data;   // exact size: no truncation, no overflow
        std::vector<uint8_t> rxBuf(n, 0);
        uint8_t msg[32] = {0};
        auto put_u64 = [&](uint32_t off, uint64_t v) { for (int k = 0; k < 8; k++) msg[off + k] = (v >> (8 * k)) & 0xFF; };
        auto put_u32 = [&](uint32_t off, uint32_t v) { for (int k = 0; k < 4; k++) msg[off + k] = (v >> (8 * k)) & 0xFF; };
        auto put_u16 = [&](uint32_t off, uint16_t v) { msg[off] = v & 0xFF; msg[off + 1] = (v >> 8) & 0xFF; };
        put_u64(0, (uint64_t)(uintptr_t)txBuf.data());
        put_u64(8, (uint64_t)(uintptr_t)rxBuf.data());
        put_u32(16, n);
        put_u32(20, (uint32_t)_speed);
        put_u16(24, 0);
        msg[26] = 8, msg[27] = 0, msg[28] = 0, msg[29] = 0, msg[30] = 0, msg[31] = 0;
        if (::ioctl(_fd, SPI_IOC_MESSAGE_1, msg) < 0) {
            throw std::system_error(errno, std::system_category(), "spi ioctl");
        }
        return rxBuf;
    }
private:
    int _fd = -1;
    int _speed = 1000000;
};

// ---- state ----

struct SxState {
    GpioLines* reset = nullptr;
    GpioLines* busy  = nullptr;
    GpioLines* irq   = nullptr;
    Spi*       spi   = nullptr;
    bool       ready = false;
    bool       busyStuck = false;
    uint32_t   freqHz = 0;
    uint32_t   calHz = 0;
    uint16_t   preamble = 16;
    uint8_t    sf = 11;
    uint16_t   bwKhz10 = 2500;
    uint8_t    sync = 0x2B;
    uint8_t    pflags = 0;
    bool       implicit = false;
    uint8_t    implicitLen = 0;
    uint8_t    ldro = 0;
};

static SxState s;
static LoraRadio::BringUp s_status{};

static void waitBusy(uint32_t ms, bool& stuckOut) {
    stuckOut = false;
    if (!s.busy) return;
    const auto t0 = std::chrono::steady_clock::now();
    while (s.busy->get(0) > 0) {
        if (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count() > (int)ms) {
            stuckOut = true; return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

static std::vector<uint8_t> cmd(uint8_t op, const std::vector<uint8_t>& params = {}) {
    // The chip must be BUSY-low before every command (datasheet 8.3.1);
    // pocket-lorascan gates every command the same way
    bool stuck = false;
    waitBusy(100, stuck);
    std::vector<uint8_t> v{op};
    v.insert(v.end(), params.begin(), params.end());
    auto rx = s.spi->xfer(v);
    // status byte (rx[1]): mode bits [6:4], command status bits [3:1]:
    // 0x03 command timeout, 0x04 processing error, 0x05 execution failure.
    // 0x01/0x02 (data available / packet received) are normal mid-RX states
    // and must not log. Log and keep going: the next command (or the caller's
    // error read) says how far it got.
    if (rx.size() >= 2 && ((rx[1] >> 1) & 0x07) >= 0x03)
        std::fprintf(stderr, "[lora] cmd 0x%02X status 0x%02X\n", op, (int)rx[1]);
    return rx;
}

static std::vector<uint8_t> readReg(uint16_t addr, size_t n) {
    std::vector<uint8_t> v{0x1D, (uint8_t)(addr >> 8), (uint8_t)(addr & 0xFF), 0};
    v.insert(v.end(), n, 0);
    auto rx = s.spi->xfer(v);
    if (rx.size() < 4 + n) return std::vector<uint8_t>(rx.begin(), rx.end());
    return std::vector<uint8_t>(rx.begin() + 4, rx.end());
}

static std::vector<uint8_t> writeReg(uint16_t addr, const std::vector<uint8_t>& data) {
    std::vector<uint8_t> v{0x0D, (uint8_t)(addr >> 8), (uint8_t)(addr & 0xFF)};
    v.insert(v.end(), data.begin(), data.end());
    return s.spi->xfer(v);
}

static void setFrequency(uint32_t hz) {
    const uint32_t r = (uint32_t)std::llround((long double)hz * (float)(1<<25) / 32e6f);
    cmd(0x86, {(uint8_t)(r >> 24), (uint8_t)(r >> 16), (uint8_t)(r >> 8), (uint8_t)r});
    s.freqHz = hz;
}

static void setSyncWord(uint8_t sw) {
    writeReg(0x0740, {(uint8_t)((sw & 0xF0) | 0x04), (uint8_t)(((sw & 0x0F) << 4) | 0x04)});
    s.sync = sw;
}

static void setModulation(uint8_t sf, uint16_t bwKhz10, uint8_t cr, uint8_t ldro) {
    // BW codes keyed in 100 Hz units (bwKhz10): 625=62.5 kHz ... 5000=500 kHz
    static const std::map<uint16_t, uint8_t> BW = {{625,0x03},{1250,0x04},{2500,0x05},{5000,0x06}};
    auto it = BW.find(bwKhz10);
    uint8_t bw = it == BW.end() ? 0x05 : it->second;
    cmd(0x8B, {sf, bw, (uint8_t)(cr - 4), ldro});
}

static void setPreamble(uint16_t preamble, bool crc, bool iqInv, uint8_t payloadLen) {
    // 0x8C: preamble[15:0], header type (0x00 explicit), payload length
    // (ignored in explicit mode; used by implicit), CRC, IQ inversion
    cmd(0x8C, {(uint8_t)(preamble >> 8), (uint8_t)(preamble & 0xFF), 0x00, payloadLen,
               (uint8_t)(crc ? 0x01 : 0x00), (uint8_t)(iqInv ? 0x01 : 0x00)});
    s.preamble = preamble;
}

const uint16_t RX_IRQ_MASK =
    LoraRadio::IRQ_RX_DONE | LoraRadio::IRQ_PREAMBLE | LoraRadio::IRQ_HDR_VALID |
    LoraRadio::IRQ_HDR_ERR | LoraRadio::IRQ_CRC_ERR | LoraRadio::IRQ_TIMEOUT;

static void setDioMasks(uint16_t mask) {
    // A DIO fires only when the bit is set in BOTH the overall IrqMask and
    // the DIO1Mask (datasheet 13.3.1) -- program the same set in both.
    cmd(0x08, {(uint8_t)(mask >> 8), (uint8_t)mask,
               (uint8_t)(mask >> 8), (uint8_t)mask, 0, 0, 0, 0});
}

}  // namespace

namespace LoraRadio {

bool begin(LoraRadio::BringUp& r) {
    resetBus();   // a retry after a failed bring-up starts clean (H.2)
    std::memset(&r, 0, sizeof r);
    if (!capPowerOn()) { r.err = -1; return false; }

    try {
        s.reset = new GpioLines(GPIO_CHIP, {PIN_RESET}, true,  true,  "lora-lx");
        s.busy  = new GpioLines(GPIO_CHIP, {PIN_BUSY}, false, false, "lora-lx");
        s.irq   = new GpioLines(GPIO_CHIP, {PIN_IRQ},  false, false, "lora-lx");
        s.spi   = new Spi(SPI_DEV);
    } catch (const std::exception& e) {
        resetBus();   // release the lines the allocation already claimed (H.2)
        r.err = -2;
        return false;
    }

    // reset pulse
    s.reset->set(0, 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    s.reset->set(0, 1);   // release reset (line 0, drive high)
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    bool busyStuck = false;
    waitBusy(200, busyStuck);
    r.busyLow = !busyStuck;

    // version string: ReadRegister 0x0320, 16 data bytes (reply data at rx[4])
    std::vector<uint8_t> ver{0x1D, 0x03, 0x20, 0x00};
    ver.insert(ver.end(), 16, 0);
    auto rx = s.spi->xfer(ver);
    const size_t vn = rx.size() >= 20 ? 16 : (rx.size() > 4 ? rx.size() - 4 : 0);
    std::memcpy(r.version, rx.data() + 4, vn);
    r.version[16] = '\0';
    r.chipFound = std::strncmp(r.version, "SX126", 5) == 0;

    // modem bring-up
    cmd(0x80, {0x00});                        // SetStandby(RC)
    cmd(0x97, {TCXO_VOLTAGE_CODE, 0x00, 0x01, 0x40});
    cmd(0x89, {0x7F});                        // Calibrate(all)
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    waitBusy(500, busyStuck);
    cmd(0x07, {0x00, 0x00});                  // ClearDeviceErrors
    cmd(0x96, {0x01});                        // SetRegulatorMode(DC-DC)
    cmd(0x9D, {0x01});                        // SetDio2AsRfSwitchCtrl
    cmd(0x80, {0x01});                        // SetStandby(XOSC)
    cmd(0x8A, {0x01});                        // SetPacketType(LoRa)
    setFrequency(906.875e6);                  // MT US LongFast (docs/LORA_CM0.md)
    cmd(0x98, {0xE1, 0xE9});                  // CalibrateImage 902-928
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    waitBusy(500, busyStuck);
    cmd(0x8F, {0x00, 0x00});                  // SetBufferBaseAddress
    writeReg(0x08AC, {0x96});                 // RX boosted gain
    setDioMasks(RX_IRQ_MASK);

    setSyncWord(0x2B);   // POR value is not 0x24B4 -- program before the readback
    auto sw = readReg(0x0740, 2);
    r.ok = (sw.size() >= 2 && sw[0] == 0x24 && sw[1] == 0xB4);
    if (!r.ok) r.err = -3;

    auto errs = s.spi->xfer({0x17, 0, 0, 0});
    uint16_t de = (errs.size() >= 4) ? (uint16_t)(errs[3] | (errs[2] << 8)) : 0;
    r.devErrors = de;
    if (de & 0x20) { r.ok = false; r.err = -4; }

    r.tcxoDeci = TCXO_DECIVOLTS[TCXO_VOLTAGE_CODE];   // deci-volts

    s_status = r;
    s.ready = r.ok;
    s.calHz = 906.875e6;
    return r.ok;
}

const LoraRadio::BringUp& status() { return s_status; }

bool apply(const Lora::Profile& p) {
    if (!s.ready) return false;
    cmd(0x80, {0x00});
    setFrequency(p.freqHz);
    // Image calibration happens once in begin() for 902-928 -- pocket-lorascan
    // does the same and decodes across the whole US band; tuneHz() recalibrates
    // for the sweep. Band-following 0x98 codes for other allocations are a
    // later phase (table in docs/LORA_FIXLIST.md).
    // auto LDRO per the chip's rule (pocket-lorascan does the same): on when
    // the symbol time is >= 16.38 ms, unless the profile forces it
    const bool ldro = (p.flags & Lora::PF_LDRO) != 0 || Lora::autoLdro(p.sf, p.bwKhz10);
    setModulation(p.sf, p.bwKhz10, p.cr, ldro ? 1 : 0);
    const bool implicit = (p.flags & Lora::PF_IMPLICIT) != 0;
    setPreamble(p.preamble, (p.flags & Lora::PF_CRC) != 0, (p.flags & Lora::PF_INVERT) != 0,
                implicit ? p.implicitLen : 0);
    setSyncWord(p.sync);
    // Errata 15.1 (modulation quality for 500 kHz) and 15.4 (IQ polarity),
    // read-modify-write on every apply, as pocket-lorascan does
    auto r889 = readReg(0x0889, 1);
    if (!r889.empty())
        writeReg(0x0889, {p.bwKhz10 == 5000 ? (uint8_t)(r889[0] & ~0x04) : (uint8_t)(r889[0] | 0x04)});
    auto r736 = readReg(0x0736, 1);
    if (!r736.empty())
        writeReg(0x0736, {(p.flags & Lora::PF_INVERT) != 0 ? (uint8_t)(r736[0] & ~0x04) : (uint8_t)(r736[0] | 0x04)});
    cmd(0x80, {0x01});
    s.pflags = p.flags; s.implicit = implicit;
    s.implicitLen = p.implicitLen; s.sf = p.sf; s.bwKhz10 = p.bwKhz10;
    s.preamble = p.preamble; s.ldro = ldro ? 1 : 0;
    return true;
}

bool startReceive() {
    if (!s.ready) return false;
    clearIrq(0xFFFF);   // pocket-lorascan clears before SetRx
    setDioMasks(RX_IRQ_MASK);
    cmd(0x82, {0xFF, 0xFF, 0xFF});   // SetRx(continuous)
    return true;
}

bool startReceiveFor(uint32_t ms) {
    if (!s.ready) return false;
    clearIrq(0xFFFF);   // pocket-lorascan clears before SetRx
    setDioMasks(RX_IRQ_MASK);
    const uint32_t raw = std::min(ms * 64u, 0xFFFFFEu);
    cmd(0x82, {(uint8_t)(raw >> 16), (uint8_t)(raw >> 8), (uint8_t)raw});
    return true;
}

bool startCad(uint8_t symbols, bool gotoRx, uint32_t rxMs, uint8_t detPeak) {
    if (!s.ready) return false;
    const uint8_t sym = (symbols <= 2) ? 0x01 : (symbols <= 4 ? 0x02 : 0x03);
    const uint8_t det = detPeak ? detPeak : (uint8_t)(s.sf + 16);
    const uint8_t detMin = 0x0A;   // chip POR default
    const uint8_t exitMode = gotoRx ? 0x01 : 0x00;
    const uint32_t t = std::min(rxMs * 64u, 0xFFFFFFu);   // 15.625 us ticks, 24-bit
    setDioMasks(LoraRadio::IRQ_CAD_DONE | LoraRadio::IRQ_CAD_HIT);
    cmd(0x88, {sym, det, detMin, exitMode,
               (uint8_t)(t >> 16), (uint8_t)(t >> 8), (uint8_t)t});   // SetCadParams
    cmd(0xC5, {});                                                     // SetCad
    return true;
}

void standby() {
    if (!s.ready) return;
    cmd(0x80, {0x00});
}

uint16_t irq() {
    if (!s.ready) return 0;
    auto rx = s.spi->xfer({0x12, 0, 0, 0});   // GetIrqStatus, 2 data bytes
    if (rx.size() < 4) return 0;
    return (uint16_t)((rx[2] << 8) | rx[3]);
}

void clearIrq(uint16_t bits) {
    if (!s.ready) return;
    cmd(0x02, {(uint8_t)(bits >> 8), (uint8_t)bits});
}

bool waitIrq(uint32_t ms) {
    if (!s.irq) return false;
    const auto t0 = std::chrono::steady_clock::now();
    while (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count() < (int64_t)ms) {
        if (s.irq->get(0) > 0) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

bool readPacket(Lora::Packet& pk) {
    if (!s.ready) return false;
    std::memset(&pk, 0, sizeof pk);
    // GetRxBufferStatus: after the status byte, len (1 byte), then start (1 byte)
    auto st13 = s.spi->xfer({0x13, 0, 0, 0});
    if (st13.size() < 4) return false;
    const uint8_t ln = st13[2];
    const uint8_t start = st13[3];
    // ReadBuffer: opcode + offset + len data bytes; data starts at rx[3]
    std::vector<uint8_t> rd{0x1E, start, 0};
    rd.insert(rd.end(), ln, 0);
    auto raw = s.spi->xfer(rd);
    const size_t avail = raw.size() > 3 ? raw.size() - 3 : 0;
    const uint8_t n = avail < sizeof pk.data ? (uint8_t)avail : sizeof pk.data;
    std::memcpy(pk.data, raw.data() + 3, n);
    pk.len = n;
    // GetPacketStatus: rssiPkt (dBm = -x/2), snrPkt (SNR x 4, signed)
    auto st14 = s.spi->xfer({0x14, 0, 0, 0, 0, 0});
    if (st14.size() >= 4) {
        pk.rssi = -(int16_t)(st14[2] / 2);
        pk.snr4 = (int8_t)st14[3];
    }
    pk.freqHz = s.freqHz; pk.bwKhz10 = s.bwKhz10; pk.sf = s.sf; pk.sync = s.sync;
    pk.pflags = s.pflags; pk.profile = Lora::PROFILE_CUSTOM;
    pk.proto = Lora::Proto::UNKNOWN;
    pk.cr = 0;   // the header's CR is not parsed here; 0 = unknown
    // The received header's CRC flag, explicit mode only: bit 7 of the low
    // byte of the 16-bit register at 0x076B (the frequency-error/RX-CRC
    // block) -- exactly what the RadioLib fork's getLoRaRxHeaderInfo reads
    // (jgromes/RadioLib 7.7.1, SX126x.cpp:732), which the ESP32 backend
    // turns into PK_CRC_PRESENT -> PK_CRC_OK on a clean frame. Without it
    // good frames log "no crc" and lora_opalert's crcOk gate never passes.
    if (!s.implicit) {
        auto hcrc = readReg(0x076B, 2);
        if (hcrc.size() >= 2 && (hcrc[1] & 0xF0))
            pk.flags |= Lora::PK_CRC_PRESENT;
    }
    pk.toaUs = Lora::timeOnAirUs(pk.sf, pk.bwKhz10, 5, s.preamble, pk.len,
                                 (s.pflags & Lora::PF_CRC) != 0, s.implicit, s.ldro != 0);
    // The sniffer leaves clearing the RX flags to the backend
    // (lora_sniffer.cpp). Read the word, derive the flags, then clear it,
    // as pocket-lorascan does. RadioLib readData rule: a frame that failed
    // the CRC is CrcErr, or HeaderErr without HeaderValid.
    auto w = s.spi->xfer({0x12, 0, 0, 0});
    const uint16_t bits = (w.size() >= 4) ? (uint16_t)((w[2] << 8) | w[3]) : 0;
    if ((bits & IRQ_CRC_ERR) || ((bits & IRQ_HDR_ERR) && !(bits & IRQ_HDR_VALID)))
        pk.flags |= Lora::PK_CRC_ERR;
    else if (pk.flags & Lora::PK_CRC_PRESENT)
        pk.flags |= Lora::PK_CRC_OK;    // the ESP32 backend's rule, lora_radio.cpp:412
    if (s.implicit) pk.flags |= Lora::PK_IMPLICIT;
    if (bits) clearIrq(bits);
    return true;
}

int16_t rssiNow() {
    if (!s.ready) return -200;
    auto rx = s.spi->xfer({0x15, 0, 0, 0});
    if (rx.size() < 2) return -200;
    return (int16_t)((uint8_t)rx[1] / 2) * -1;
}

bool tuneHz(uint32_t hz) {
    if (!s.ready) return false;
    const uint32_t d = (hz > s.calHz ? hz - s.calHz : (s.calHz > hz ? s.calHz - hz : 0));
    if (d > 30000000u) {
        cmd(0x98, {0xE1, 0xE9});
        s.calHz = hz;
    }
    setFrequency(hz);
    return true;
}

void busHold()   {}
void busRelease() {}

// SetTxParams power encoding (SX1262 datasheet 13.4.4): -9..+22 dBm,
// 1 step per dBm, 0xF7 at -9 and 0x16 at +22.
static uint8_t txPowerByte(int8_t dbm) {
    return (uint8_t)(0xF7u + (uint8_t)(dbm + 9));
}

bool transmit(const uint8_t* d, uint8_t n, int8_t dbm) {
    if (!s.ready || !d || n == 0) return false;
    if (dbm > 22) dbm = 22;    // the firmware's ceiling, and the chip's
    if (dbm < -9) dbm = -9;
    // Standby, then the PA setup, the packet into the buffer, then SetTx.
    // SetTxParams/SetPaConfig are re-issued every frame: the chip loses
    // its TX context on sleep and the host has no other record that it
    // was set. The IRQ is TxDone (bit 0).
    cmd(0x80, {0x00});
    cmd(0x95, {0x04, 0x07, 0x00, 0x01});   // SetPaConfig, SX1262 high-power row
    cmd(0x8F, {0x00, 0x00});               // SetBufferBaseAddress 0
    cmd(0x8E, {txPowerByte(dbm), 0x02});   // SetTxParams, ramp 40 us
    // TX takes its length from SetPacketParams even with an explicit header
    // (RX leaves it 0), so set it to this frame; RX's is put back after.
    const bool crc = (s.pflags & Lora::PF_CRC) != 0, iqInv = (s.pflags & Lora::PF_INVERT) != 0;
    cmd(0x8C, {(uint8_t)(s.preamble >> 8), (uint8_t)(s.preamble & 0xFF), (uint8_t)(s.implicit ? 0x01 : 0x00), n,
               (uint8_t)(crc ? 0x01 : 0x00), (uint8_t)(iqInv ? 0x01 : 0x00)});
    std::vector<uint8_t> w{0x0E, 0x00};    // WriteBuffer, offset 0
    w.insert(w.end(), d, d + n);
    s.spi->xfer(w);
    setDioMasks(0x0001);                   // DIO1 on TxDone only
    clearIrq(0xFFFF);
    const uint32_t toa = Lora::timeOnAirUs(s.sf, s.bwKhz10, 5, s.preamble, n,
                                           (s.pflags & Lora::PF_CRC) != 0, s.implicit, s.ldro != 0);
    uint32_t ticks = toa > 0 ? (toa / 15u) + 16384u : 16384u;   // 2x airtime + 250 ms margin
    if (ticks > 0xFFFFF) ticks = 0xFFFFF;
    cmd(0x83, {(uint8_t)(ticks >> 16), (uint8_t)(ticks >> 8), (uint8_t)ticks});   // SetTx
    const uint32_t waitMs = (uint32_t)(ticks * 15.625e-3f) + 250;
    bool irq = waitIrq(waitMs);
    (void)irq;
    auto w_rx = s.spi->xfer({0x12, 0, 0, 0});
    const uint16_t bits = (w_rx.size() >= 4) ? (uint16_t)((w_rx[2] << 8) | w_rx[3]) : 0;
    const bool done = (bits & 0x0001) != 0;
    if (bits) clearIrq(bits);
    cmd(0x80, {0x00});   // back to STDBY_RC (the chip returns on its own; keep it explicit)
    cmd(0x8C, {(uint8_t)(s.preamble >> 8), (uint8_t)(s.preamble & 0xFF), (uint8_t)(s.implicit ? 0x01 : 0x00),
               (uint8_t)(s.implicit ? s.implicitLen : 0), (uint8_t)(crc ? 0x01 : 0x00), (uint8_t)(iqInv ? 0x01 : 0x00)});
    setDioMasks(RX_IRQ_MASK);
    std::fprintf(stderr, "[lora] tx %u bytes at %d dBm: %s\n", (unsigned)n, (int)dbm,
                 done ? "txdone" : (irq ? "irq-but-not-txdone" : "timeout"));
    return done;
}

void resetBus() {
    if (s.reset) s.reset->set(0, 1);   // release reset before dropping the fd
    if (s.busy)  s.busy->close();
    if (s.irq)   s.irq->close();
    if (s.reset) s.reset->close();
    if (s.spi) { delete s.spi; s.spi = nullptr; }
    if (s.busy)  { delete s.busy; s.busy = nullptr; }
    if (s.irq)   { delete s.irq; s.irq = nullptr; }
    if (s.reset) { delete s.reset; s.reset = nullptr; }
    s.ready = false;
}

}  // namespace LoraRadio
