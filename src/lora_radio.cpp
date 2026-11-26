// SquachWatch-CYD — the SX1262 behind RadioLib. See include/lora_radio.h.
#if SQUACH_LORA
#include "lora_radio.h"
#include <Arduino.h>
#include <SPI.h>
#if defined(CROWPANEL7)
#include "crowpanel7_board.h"
// The slot has the bus to itself; the panel is RGB, not SPI.
#define LORA_SPI SPI
#elif defined(TWATCH_S3)
// The T-Watch S3's own SX1262 (and the S3 Plus's), from LilyGoLib's pin
// table; ALDO4 powers it (main.cpp's PMU setup). The panel is on the global
// SPI object -- TFT_eSPI drives it -- so the radio gets the other engine,
// HSPI, and neither ever re-routes the other's pins.
#define LORA_PIN_SCK   3
#define LORA_PIN_MISO  4
#define LORA_PIN_MOSI  1
#define LORA_PIN_NSS   5
#define LORA_PIN_DIO1  9
#define LORA_PIN_RST   8
#define LORA_PIN_BUSY  7
static SPIClass s_loraSpi(HSPI);
#define LORA_SPI s_loraSpi
#elif defined(TDECK_PLUS)
// The T-Deck Plus's SX1262 sits on the DISPLAY's bus (SCK 40, MISO 38, MOSI
// 41), beside the SD card. The radio shares the screen's SETUP, not just its
// wires: its bytes go out through the same register path the screen's own
// commands use (SharedHal and sharedXfer below), under FramePush::busLock()
// so never while a frame is on the wire, and the only thing changed for its
// turn is the clock (the SX126x takes 16 MHz at most; the panel runs at 40)
// -- put back before the lock is released. Nothing is registered with the
// IDF driver as a second device: two attempts that way (2026-10-12/13) had
// the driver reprogram the whole host at every swap, and the screen's next
// window command went out under the radio's settings: tearing, then a frozen
// panel with the firmware running at 28 fps. Pins from LilyGo's utilities.h.
#include "frame_push.h"
#include <TFT_eSPI.h>
#include <soc/spi_reg.h>
#include <driver/spi_master.h>
extern spi_device_handle_t dmaHAL;   // TFT_eSPI's device for the panel's DMA pushes
#define LORA_PIN_NSS   9
#define LORA_PIN_DIO1  45
#define LORA_PIN_RST   17
#define LORA_PIN_BUSY  13
#define LORA_SPI SPI                 // not used on this board; the device below is
#define LORA_SHARED_BUS 1
#else
#error "SQUACH_LORA on a board with no LoRa pins"
#endif
#ifndef LORA_SHARED_BUS
#define LORA_SHARED_BUS 0
#endif
#include <RadioLib.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <string.h>
#include <math.h>

namespace {

// The handful of protected calls a sniffer needs and a node never does: the
// version register, the chip's own error word, and the packet status word
// with the despread signal RSSI in it.
class Sx1262Open : public SX1262 {
public:
    using SX1262::SX1262;
    int16_t  readReg(uint16_t a, uint8_t* d, uint8_t n) { return readRegister(a, d, n); }
    uint16_t devErrors()  { return getDeviceErrors(); }
    int16_t  clearErrors() { return clearDeviceErrors(); }
};

#if LORA_SHARED_BUS
// One exchange, full duplex, through the host's own word registers: up to 64
// bytes a go, the chip select held by the caller across the lot (an SX126x
// command is one NSS-low frame, and the clock may pause inside it). The host
// is idle between frames, set up for the panel by the pusher's endWrite();
// the clock, the duplex bits and the length are the three things touched,
// and all three go back before this returns. SPI_UPDATE commits a changed
// setup on the S3 before SPI_USR starts a transfer (the library does the
// same in TFT_WRITE_BITS).
static uint32_t s_radioClk = 0;
static inline void hostLatch() {
    WRITE_PERI_REG(SPI_CMD_REG(SPI_PORT), SPI_UPDATE);
    while (READ_PERI_REG(SPI_CMD_REG(SPI_PORT)) & SPI_UPDATE) {}
}
static bool sharedXfer(const uint8_t* out, uint8_t* in, size_t len) {
    if (len == 0) return false;
    if (!s_radioClk) s_radioClk = spiFrequencyToClockDiv(8000000);
    // The IDF driver owns this host once the panel's DMA is up, and its
    // interrupt handler answers the host's transfer-done interrupt -- which a
    // register-fed transfer raises too. Called with nothing of its own
    // queued and no device holding the bus, the handler walked an empty
    // entry: LoadProhibited in spi_bus_lock_bg_exit, every 15 s to 6 min,
    // on whichever task was running (the black box, 2026-10-13). So the
    // panel's device holds the bus for the radio's turn, the way it does
    // around the library's own register-fed writes, and the interrupt is
    // masked while the bytes go out and its flag cleared before it is
    // unmasked. Before the DMA is up there is no handler to misfire.
    const bool held = dmaHAL && !FramePush::pushHoldsBus() && spi_device_acquire_bus(dmaHAL, portMAX_DELAY) == ESP_OK;
    const uint32_t ena0  = READ_PERI_REG(SPI_DMA_INT_ENA_REG(SPI_PORT));
    WRITE_PERI_REG(SPI_DMA_INT_ENA_REG(SPI_PORT), 0);
    const uint32_t user0 = READ_PERI_REG(SPI_USER_REG(SPI_PORT));
    const uint32_t clk0  = READ_PERI_REG(SPI_CLOCK_REG(SPI_PORT));
    const uint32_t dlen0 = READ_PERI_REG(SPI_MS_DLEN_REG(SPI_PORT));
    WRITE_PERI_REG(SPI_CLOCK_REG(SPI_PORT), s_radioClk);
    WRITE_PERI_REG(SPI_USER_REG(SPI_PORT), (user0 & SPI_CK_OUT_EDGE) | SPI_USR_MOSI | SPI_USR_MISO | SPI_DOUTDIN);
    size_t at = 0;
    while (at < len) {
        const size_t n = len - at > 64 ? 64 : len - at;
        uint32_t w[16] = { 0 };
        if (out) memcpy(w, out + at, n);
        const size_t words = (n + 3) / 4;
        for (size_t i = 0; i < words; i++) WRITE_PERI_REG(SPI_W0_REG(SPI_PORT) + (i << 2), w[i]);
        WRITE_PERI_REG(SPI_MS_DLEN_REG(SPI_PORT), (uint32_t)(n * 8 - 1));
        hostLatch();
        SET_PERI_REG_MASK(SPI_CMD_REG(SPI_PORT), SPI_USR);
        while (READ_PERI_REG(SPI_CMD_REG(SPI_PORT)) & SPI_USR) {}
        if (in) {
            for (size_t i = 0; i < words; i++) w[i] = READ_PERI_REG(SPI_W0_REG(SPI_PORT) + (i << 2));
            memcpy(in + at, w, n);
        }
        at += n;
    }
    WRITE_PERI_REG(SPI_MS_DLEN_REG(SPI_PORT), dlen0);
    WRITE_PERI_REG(SPI_USER_REG(SPI_PORT), user0);
    WRITE_PERI_REG(SPI_CLOCK_REG(SPI_PORT), clk0);
    hostLatch();
    WRITE_PERI_REG(SPI_DMA_INT_CLR_REG(SPI_PORT), SPI_TRANS_DONE_INT_CLR);
    WRITE_PERI_REG(SPI_DMA_INT_ENA_REG(SPI_PORT), ena0);
    if (held) spi_device_release_bus(dmaHAL);
    return true;
}
// RadioLib's Arduino HAL for the pins and timing, with the SPI side replaced:
// the display bus's lock around each exchange, asked for with busWant() so a
// push in flight stops after its current 8-row chunk (about a millisecond)
// rather than making the radio wait out the frame -- with the lock alone the
// survey's hops slowed to the point of missing MeshCore's 65 ms preamble (0
// of 4 decoded, 2026-10-13; 8 of 8 with the cut-in). The exchange itself
// goes through sharedXfer. RadioLib brackets every register
// exchange in spiBeginTransaction() .. spiEndTransaction(), from whichever
// task asks, so this is the one place the sharing has to be right.
class SharedHal : public ArduinoHal {
public:
    using ArduinoHal::ArduinoHal;
    void spiBegin() override {}
    void spiEnd() override {}
    void spiBeginTransaction() override { FramePush::busWant(); FramePush::busLock(); }
    void spiTransfer(uint8_t* out, size_t len, uint8_t* in) override { sharedXfer(out, in, len); }
    void spiEndTransaction() override   { FramePush::busUnlock(); }
};
#endif

Sx1262Open*        s_radio  = nullptr;
SemaphoreHandle_t  s_irqSem = nullptr;
LoraRadio::BringUp s_up;
uint32_t           s_calHz  = 0;       // where the image was last calibrated
bool               s_implicit = false;
uint16_t           s_preamble = 8;     // the profile's, for the time-on-air estimate
uint8_t            s_implicitLen = 0;
uint8_t            s_sf = 11;
uint16_t           s_bw10 = 2500;
uint8_t            s_sync = 0x2B;
uint8_t            s_pflags = 0;
uint32_t           s_freqHz = 0;

void IRAM_ATTR onDio1() {
    BaseType_t woken = pdFALSE;
    if (s_irqSem) xSemaphoreGiveFromISR(s_irqSem, &woken);
    if (woken) portYIELD_FROM_ISR();
}

// A reset pulse, then wait for BUSY to fall. An SX126x holds BUSY high while
// it boots and drops it within a millisecond or two; a floating pin (nothing
// in the slot, or K1 on the card) stays wherever the pull-up leaves it.
bool resetAndWaitBusy() {
    pinMode(LORA_PIN_BUSY, INPUT_PULLUP);
    pinMode(LORA_PIN_RST, OUTPUT);
    digitalWrite(LORA_PIN_RST, LOW);
    delay(2);
    digitalWrite(LORA_PIN_RST, HIGH);
    const uint32_t t0 = millis();
    while (millis() - t0 < 50) {
        if (digitalRead(LORA_PIN_BUSY) == LOW) return true;
        delay(1);
    }
    return false;
}

// ReadRegister 0x0320, sixteen bytes: the chip's version string, before
// RadioLib has been told anything. Opcode, 16-bit address, one NOP for the
// status byte, then the data.
void rawVersion(char out[17]) {
#if LORA_SHARED_BUS
    uint8_t tx[20] = { 0x1D, 0x03, 0x20, 0x00 }, rx[20] = { 0 };
    FramePush::busLock();
    digitalWrite(LORA_PIN_NSS, LOW);
    sharedXfer(tx, rx, sizeof tx);
    digitalWrite(LORA_PIN_NSS, HIGH);
    FramePush::busUnlock();
    for (int i = 0; i < 16; i++) {
        const uint8_t c = rx[4 + i];
        out[i] = (c >= 0x20 && c < 0x7F) ? (char)c : (c ? '.' : '\0');
    }
#else
    LORA_SPI.beginTransaction(SPISettings(2000000, MSBFIRST, SPI_MODE0));
    digitalWrite(LORA_PIN_NSS, LOW);
    LORA_SPI.transfer(0x1D);
    LORA_SPI.transfer(0x03);
    LORA_SPI.transfer(0x20);
    LORA_SPI.transfer(0x00);
    for (int i = 0; i < 16; i++) {
        const uint8_t c = LORA_SPI.transfer(0x00);
        out[i] = (c >= 0x20 && c < 0x7F) ? (char)c : (c ? '.' : '\0');
    }
    digitalWrite(LORA_PIN_NSS, HIGH);
    LORA_SPI.endTransaction();
#endif
    out[16] = '\0';
}

}

namespace LoraRadio {

bool begin(BringUp& r) {
    memset(&s_up, 0, sizeof s_up);
    if (!s_irqSem) s_irqSem = xSemaphoreCreateBinary();

#if !LORA_SHARED_BUS
    LORA_SPI.begin(LORA_PIN_SCK, LORA_PIN_MISO, LORA_PIN_MOSI, LORA_PIN_NSS);
#endif
    pinMode(LORA_PIN_NSS, OUTPUT);
    digitalWrite(LORA_PIN_NSS, HIGH);

    s_up.busyLow = resetAndWaitBusy();
    rawVersion(s_up.version);
    s_up.chipFound = strncmp(s_up.version, "SX126", 5) == 0;
    Serial.printf("[lora] slot: BUSY %s after reset; version register \"%s\" -> %s\n",
                  s_up.busyLow ? "fell" : "stayed high", s_up.version,
                  s_up.chipFound ? "an SX126x" : "no module (K1 on the card or speaker?)");
    if (!s_up.chipFound) { r = s_up; return false; }

#if LORA_SHARED_BUS
    // The global SPI object is handed over for the type only: SharedHal never
    // begins it or sends through it.
    if (!s_radio) s_radio = new Sx1262Open(new Module(new SharedHal(SPI), LORA_PIN_NSS, LORA_PIN_DIO1, LORA_PIN_RST, LORA_PIN_BUSY));
#else
    if (!s_radio) s_radio = new Sx1262Open(new Module(LORA_PIN_NSS, LORA_PIN_DIO1, LORA_PIN_RST, LORA_PIN_BUSY, LORA_SPI));
#endif

    // The TCXO's voltage is not documented for this module (docs/LORA.md
    // section 1): Elecrow's S3 code says 3.3 V, their P4 code 1.6 V, and
    // the datasheet wants the rail 200 mV above it. Try the datasheet-legal
    // ones first and take the first that starts; RadioLib itself falls back
    // to a crystal when the oscillator does not start at all.
    static const uint8_t tries[] = { 18, 33, 16, 0 };
    for (uint8_t i = 0; i < sizeof tries; i++) {
        const float v = tries[i] / 10.0f;
        const int16_t st = s_radio->begin(869.525f, 250.0f, 11, 5, 0x2B, 10, 16, v, false);
        const uint16_t de = (st == RADIOLIB_ERR_NONE) ? s_radio->devErrors() : 0;
        Serial.printf("[lora] begin with TCXO %.1f V: err %d, device errors 0x%03x\n", v, (int)st, (unsigned)de);
        s_up.err = st;
        s_up.devErrors = de;
        if (st == RADIOLIB_ERR_NONE && !(de & RADIOLIB_SX126X_XOSC_START_ERR)) {
            s_up.tcxoDeci = tries[i];
            s_up.ok = true;
            break;
        }
        if (st == RADIOLIB_ERR_NONE) s_radio->clearErrors();
    }
    if (!s_up.ok) { r = s_up; return false; }

    s_calHz = 869525000;
    s_radio->setRxBoostedGainMode(true);
    s_radio->setDio1Action(onDio1);
    r = s_up;
    return true;
}

const BringUp& status() { return s_up; }

#if LORA_SHARED_BUS
void busHold()    { FramePush::busWant(); FramePush::busLock(); }
void busRelease() { FramePush::busUnlock(); }
#else
void busHold()    {}
void busRelease() {}
#endif

bool apply(const Lora::Profile& p) {
    if (!s_radio || !s_up.ok) return false;
    s_radio->standby();
    // Image calibration is per band (the chip's own table: 902-928 is one
    // calibration, 863-870 another), so a hop inside one needs none; run it
    // for a real move, 869 to 433 say. The 3 MHz this was sized for made
    // every hop between LongFast (906.875) and MeshCore US (910.525) a fresh
    // calibration, twice a round.
    const uint32_t d = p.freqHz > s_calHz ? p.freqHz - s_calHz : s_calHz - p.freqHz;
    const bool cal = d > 30000000u;
    int16_t st = s_radio->setFrequency(p.freqHz / 1000000.0f, !cal);
    if (st != RADIOLIB_ERR_NONE) { s_up.err = st; return false; }
    if (cal) s_calHz = p.freqHz;
    st = s_radio->setBandwidth(p.bwKhz10 / 10.0f);              if (st) { s_up.err = st; return false; }
    st = s_radio->setSpreadingFactor(p.sf);                     if (st) { s_up.err = st; return false; }
    st = s_radio->setCodingRate(p.cr);                          if (st) { s_up.err = st; return false; }
    st = s_radio->setSyncWord(p.sync);                          if (st) { s_up.err = st; return false; }
    st = s_radio->setPreambleLength(p.preamble);                if (st) { s_up.err = st; return false; }
    st = s_radio->setCRC((p.flags & Lora::PF_CRC) ? 2 : 0);     if (st) { s_up.err = st; return false; }
    st = s_radio->invertIQ((p.flags & Lora::PF_INVERT) != 0);   if (st) { s_up.err = st; return false; }
    if (p.flags & Lora::PF_IMPLICIT) st = s_radio->implicitHeader(p.implicitLen);
    else                             st = s_radio->explicitHeader();
    if (st) { s_up.err = st; return false; }
    if (p.flags & Lora::PF_LDRO) st = s_radio->forceLDRO(true);
    else                         st = s_radio->autoLDRO();
    if (st) { s_up.err = st; return false; }
    s_implicit    = (p.flags & Lora::PF_IMPLICIT) != 0;
    s_implicitLen = p.implicitLen;
    s_preamble    = p.preamble;
    s_sf = p.sf; s_bw10 = p.bwKhz10; s_sync = p.sync; s_pflags = p.flags; s_freqHz = p.freqHz;
    return true;
}

bool tuneHz(uint32_t hz) {
    if (!s_radio || !s_up.ok) return false;
    const uint32_t d = hz > s_calHz ? hz - s_calHz : s_calHz - hz;
    const bool cal = d > 30000000u;
    if (s_radio->setFrequency(hz / 1000000.0f, !cal) != RADIOLIB_ERR_NONE) return false;
    if (cal) s_calHz = hz;
    s_freqHz = hz;
    return true;
}

// Every event a sniffer counts, all routed to DIO1.
static const RadioLibIrqFlags_t RX_IRQS =
    (1UL << RADIOLIB_IRQ_RX_DONE) | (1UL << RADIOLIB_IRQ_TIMEOUT) | (1UL << RADIOLIB_IRQ_CRC_ERR) |
    (1UL << RADIOLIB_IRQ_HEADER_VALID) | (1UL << RADIOLIB_IRQ_HEADER_ERR) | (1UL << RADIOLIB_IRQ_PREAMBLE_DETECTED);
static const RadioLibIrqFlags_t CAD_IRQS =
    RX_IRQS | (1UL << RADIOLIB_IRQ_CAD_DONE) | (1UL << RADIOLIB_IRQ_CAD_DETECTED);

bool startReceive() {
    if (!s_radio || !s_up.ok) return false;
    const int16_t st = s_radio->startReceive(RADIOLIB_SX126X_RX_TIMEOUT_INF, RX_IRQS, RX_IRQS, 0);
    if (st) s_up.err = st;
    return st == RADIOLIB_ERR_NONE;
}

bool startReceiveFor(uint32_t ms) {
    if (!s_radio || !s_up.ok) return false;
    // The chip's timeout counts in 15.625 us steps.
    uint32_t raw = ms * 64u;
    if (raw > 0xFFFFFEu) raw = 0xFFFFFEu;
    const int16_t st = s_radio->startReceive(raw, RX_IRQS, RX_IRQS, 0);
    if (st) s_up.err = st;
    return st == RADIOLIB_ERR_NONE;
}

bool startCad(uint8_t symbols, bool gotoRx, uint32_t rxMs, uint8_t detPeak) {
    if (!s_radio || !s_up.ok) return false;
    ChannelScanConfig_t cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.cad.symNum   = symbols <= 2 ? RADIOLIB_SX126X_CAD_ON_2_SYMB : symbols >= 8 ? RADIOLIB_SX126X_CAD_ON_8_SYMB : RADIOLIB_SX126X_CAD_ON_4_SYMB;
    cfg.cad.detPeak  = detPeak ? detPeak : RADIOLIB_SX126X_CAD_PARAM_DEFAULT;
    cfg.cad.detMin   = RADIOLIB_SX126X_CAD_PARAM_DEFAULT;
    cfg.cad.exitMode = gotoRx ? RADIOLIB_SX126X_CAD_GOTO_RX : RADIOLIB_SX126X_CAD_GOTO_STDBY;
    // setCad takes this one in microseconds and divides by 15.625 itself;
    // the chip's field holds about 262 s at most.
    if (rxMs > 262000u) rxMs = 262000u;
    cfg.cad.timeout  = rxMs * 1000u;
    cfg.cad.irqFlags = CAD_IRQS;
    cfg.cad.irqMask  = CAD_IRQS;
    const int16_t st = s_radio->startChannelScan(cfg);
    if (st) s_up.err = st;
    return st == RADIOLIB_ERR_NONE;
}

void standby() { if (s_radio && s_up.ok) s_radio->standby(); }

uint16_t irq() { return s_radio ? (uint16_t)s_radio->getIrqFlags() : 0; }
void clearIrq(uint16_t bits) { if (s_radio) s_radio->clearIrqFlags(bits); }

bool waitIrq(uint32_t ms) {
    if (!s_irqSem) return false;
    return xSemaphoreTake(s_irqSem, pdMS_TO_TICKS(ms)) == pdTRUE;
}

bool readPacket(Lora::Packet& pk) {
    if (!s_radio || !s_up.ok) return false;
    const uint16_t flags = (uint16_t)s_radio->getIrqFlags();
    size_t len = s_radio->getPacketLength(true);
    if (len > sizeof pk.data) len = sizeof pk.data;
    const int16_t st = s_radio->readData(pk.data, sizeof pk.data);
    // readData copies the bytes before it judges the CRC, so a mismatch is
    // still a frame worth keeping -- flagged, never decoded.
    if (st != RADIOLIB_ERR_NONE && st != RADIOLIB_ERR_CRC_MISMATCH) { s_up.err = st; return false; }
    pk.len   = (uint8_t)len;
    pk.rssi  = (int16_t)lroundf(s_radio->getRSSI());
    pk.snr4  = (int8_t)lroundf(s_radio->getSNR() * 4.0f);
    pk.ferrHz = (int32_t)lroundf(s_radio->getFrequencyError());
    pk.freqHz = s_freqHz; pk.bwKhz10 = s_bw10; pk.sf = s_sf; pk.sync = s_sync; pk.pflags = s_pflags;
    pk.flags = 0;
    pk.cr = 0;
    if (s_implicit) {
        pk.flags |= Lora::PK_IMPLICIT;
    } else {
        uint8_t crRaw = 0; bool hasCrc = false;
        if (s_radio->getLoRaRxHeaderInfo(&crRaw, &hasCrc) == RADIOLIB_ERR_NONE) {
            pk.cr = (uint8_t)(crRaw ? 4 + crRaw : 0);
            if (hasCrc) pk.flags |= Lora::PK_CRC_PRESENT;
        }
    }
    if (flags & RADIOLIB_SX126X_IRQ_CRC_ERR) pk.flags |= Lora::PK_CRC_ERR;
    else if (pk.flags & Lora::PK_CRC_PRESENT) pk.flags |= Lora::PK_CRC_OK;
    // The preamble is the one field of the air interface a received frame does
    // not carry: the chip detects the preamble and reports nothing about how
    // long it was. So this stays an estimate -- but the profile we are tuned to
    // holds what the network's senders use, and that is a far better estimate
    // than a flat 8 symbols. MeshCore Narrow sends 32 (src/lora_profiles.cpp),
    // and 8 undercounts a 60-byte frame by 98 ms of 706, 13.9 %; Meshtastic's
    // 16 undercounts a 66-byte LongFast frame by 66 ms of 723, 9.1 %.
    // docs/LORA.md's reference table (0.71 s for MeshCore Narrow at 60 bytes,
    // 1.1 s for MeshCom at 80) only comes out of this formula with the
    // profile's own preamble, so the table was always the check on this line.
    // Airtime feeds the duty figure, and a duty figure a tenth low is the one
    // error here with a legal meaning. The estimate is no longer guaranteed low
    // in one direction: a sender on firmware older than the preset's preamble
    // change -- MeshCore before 1.16, which sent 8 where the table now says 32
    // -- is overcounted by that difference. That is a bounded error on a
    // shrinking set of nodes, against a systematic 10-14 % undercount on all of
    // them; if a frame ever carries its preamble length, this line should use
    // that instead.
    pk.toaUs = Lora::timeOnAirUs(pk.sf, pk.bwKhz10, pk.cr ? pk.cr : 5, s_preamble,
                                 pk.len, (pk.flags & Lora::PK_CRC_PRESENT) != 0, s_implicit,
                                 (s_pflags & Lora::PF_LDRO) != 0);
    return true;
}

bool transmit(const uint8_t* d, uint8_t n, int8_t dbm) {
    if (!s_radio || !s_up.ok) return false;
    if (dbm > 22) dbm = 22;
    if (dbm < -9) dbm = -9;
    s_radio->setOutputPower(dbm);
    const int16_t st = s_radio->transmit(const_cast<uint8_t*>(d), n);
    Serial.printf("[lora] tx %u bytes at %d dBm: err %d\n", (unsigned)n, (int)dbm, (int)st);
    return st == RADIOLIB_ERR_NONE;
}
#if LORA_BENCH_TX
bool benchTransmit(const uint8_t* d, uint8_t n, int8_t dbm) { return transmit(d, n, dbm); }
#endif

int16_t rssiNow() {
    if (!s_radio || !s_up.ok) return -200;
    return (int16_t)lroundf(s_radio->getRSSI(false));
}

}
#endif
