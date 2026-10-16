#include "frame_push.h"
#include <TFT_eSPI.h>
#if defined(NM_CYD_C5)
#include <soc/pcr_reg.h>
#endif
#if defined(NM_CYD_C5) || SQW_PUSH_DMA_ONLY
#include <esp_heap_caps.h>
#include <driver/spi_master.h>
#include <string.h>
#endif
#if SQW_PUSH_QSPI
#include "st77922_qspi.h"
#elif defined(NM_CYD_C5) || SQW_PUSH_DMA_ONLY
// The driver's device handle: a global in the processor port the library
// compiles in, outside any namespace.
extern spi_device_handle_t dmaHAL;
#endif

namespace FramePush {

#if SQW_FRAME_PUSH

// 8-bit colour in, the 16 bits that go on the wire out. 512 bytes, built once
// from rgb332Wire() so the table and the tested arithmetic are the same thing.
static uint16_t s_lut[256];

// The SPI hardware takes 64 bytes at a time: 32 pixels, 16 words. This is
// the library's own burst size, fixed by the FIFO, not a tuning knob.
static const uint32_t PX_PER_BURST = 32;

// ---- what the panel already shows ----
//
// One hash per row of the last frame that went out. A row whose hash has not
// changed is not sent: the panel keeps what it has. On a menu that is every
// row, and the push costs nothing; on the main screen it is the title bar,
// the counters and the buttons -- about a third -- repainted identically
// every frame by code that does not know they did not change.
//
// Rows are remembered by their PANEL row, not their row within the sprite.
// The 3.5" draws its main screen in two bands -- one half-height sprite
// pushed twice, at the top of the screen and then halfway down -- and a
// record indexed within the sprite would have the second band reading the
// first band's rows and skipping whatever happened to match. Indexing by
// (y + row) gives each band its own half of the record and costs nothing.
//
// The record is only as good as its last full push, so it is thrown away
// (invalidate()) whenever anything else could have touched the panel -- a
// rotation, a fall-back to the ordinary push -- and every 64th frame is a
// full one regardless, so nothing that slips past that can stay on screen
// for more than three seconds.
//
// Stretching that to one in 192 was tried and put back. It is worth about a
// third of a millisecond a frame on the 3.5" -- one percent -- and it costs
// every board a stale-row window three times longer: six seconds on a 2.8"
// running at thirty frames a second, where today it is two. That is a bad
// trade to make on five boards for one percent on the sixth.
static const int32_t MAX_ROWS = 480;   // the 3.5" in portrait, the tallest there is
static uint32_t s_rowHash[MAX_ROWS];
// -DSQW_C5_FLIP_DIAG=1: the frame before last's hashes too, for the C5's
// two-buffer push. A row whose hash goes A, B, A is a screen drawing on top
// of the frame before instead of its own, which the two buffers would turn
// into a flicker. Every screen checked clean by eye on 2026-10-02 (the
// counter's 40-120 rows per 300 frames were the aura's own blink), so this
// is off; turn it on when a new screen shows a flicker on the C5.
#ifndef SQW_C5_FLIP_DIAG
#define SQW_C5_FLIP_DIAG 0
#endif
#if defined(NM_CYD_C5) && SQW_C5_FLIP_DIAG
static uint32_t s_rowHash2[MAX_ROWS];
static uint32_t s_flipRows = 0, s_flipFrames = 0;
#endif
static bool     s_valid   = false;
static int32_t  s_validW  = 0, s_validH = 0;
static uint32_t s_frameNo = 0;
static int32_t  s_lastRows = 0;
#if defined(CYD35)
static uint32_t s_hashUs = 0, s_wireUs = 0;
#define SQW_PUSH_CLOCK(v) const uint32_t v = micros()
#define SQW_PUSH_CHARGE(acc, v) acc += micros() - (v)
#else
#define SQW_PUSH_CLOCK(v) ((void)0)
#define SQW_PUSH_CHARGE(acc, v) ((void)0)
#endif

static bool s_ready   = false;
static bool s_enabled = true;

bool begin() {
    if (s_ready) return true;
    for (int i = 0; i < 256; i++) s_lut[i] = rgb332Wire((uint8_t)i);
    s_ready = true;
    Serial.println("[push] overlapped frame push on, unchanged rows skipped");
#if defined(SQW_PUSH_PROBE) && !SQW_PUSH_DMA_ONLY
    // Bring-up probe: is the frame push slow because of the wire or the CPU?
    Serial.printf("[probe] cpu %u MHz, apb %u Hz, SPI_CLOCK_REG=0x%08X SPI_USER_REG=0x%08X\n",
                  (unsigned)ESP.getCpuFreqMHz(), (unsigned)getApbFrequency(),
                  (unsigned)READ_PERI_REG(SPI_CLOCK_REG(SPI_PORT)),
                  (unsigned)READ_PERI_REG(SPI_USER_REG(SPI_PORT)));
#endif
    return true;
}

bool available() { return s_ready; }
void setEnabled(bool on) { s_enabled = on; }
bool enabled() { return s_ready && s_enabled; }
void invalidate() { s_valid = false; }
#if defined(CYD35)
void newFrame() { s_lastRows = 0; s_hashUs = 0; s_wireUs = 0; }
uint32_t hashUs() { return s_hashUs; }
uint32_t wireUs() { return s_wireUs; }
#else
void newFrame() { s_lastRows = 0; }
uint32_t hashUs() { return 0; }
uint32_t wireUs() { return 0; }
#endif
int32_t lastRows() { return s_lastRows; }

// FNV-1a over a row, a word at a time. Rows are a multiple of four bytes on
// every board this is built for, and the sprite buffer is word-aligned.
static inline uint32_t rowHash(const uint8_t* row, int32_t w) {
    const uint32_t* p = (const uint32_t*)row;
    uint32_t h = 2166136261u;
    for (int32_t n = w >> 2; n > 0; n--) { h ^= *p++; h *= 16777619u; }
    return h;
}

#if !SQW_PUSH_DMA_ONLY
// Latching the transfer registers, which is not free on every ESP32.
//
// On the original ESP32 and the S3, writing SPI_USR is enough: the length in
// SPI_MOSI_DLEN_REG and the words in SPI_W0.. are read straight out by the
// transfer that bit starts. The ESP32-C5's GPSPI2 does not work that way. Its
// configuration registers are staged, and SPI_UPDATE has to be raised (and
// seen to clear) to commit them before SPI_USR starts anything -- otherwise the
// transfer runs on whatever the peripheral last latched.
//
// The symptom is not a blank screen, which is what makes it worth this comment.
// The panel initialises, answers its ID registers, reports display-on and 16
// bits per pixel, and the firmware happily reports 70 fps -- while the glass
// shows dashed, torn runs of pixels with most rows missing, because some bursts
// go out with a stale length and some do not go out at all.
//
// RockBase's TFT_eSPI C5 port does exactly this in its own TFT_WRITE_BITS, and
// that is why plain TFT_eSPI drawing works on this board while this file, which
// deliberately bypasses the library to overlap conversion with the wire, did
// not. Cost is one register write and one poll per 32-pixel burst, on the one
// chip that needs it; every other board compiles this away to nothing.
#if defined(CONFIG_IDF_TARGET_ESP32C5)
    #define SQW_SPI_LATCH()                                            \
        do {                                                           \
            WRITE_PERI_REG(SPI_CMD_REG(SPI_PORT), SPI_UPDATE);         \
            while (READ_PERI_REG(SPI_CMD_REG(SPI_PORT)) & SPI_UPDATE) {} \
        } while (0)
#else
    #define SQW_SPI_LATCH() do { } while (0)
#endif

// 32 pixels into 16 words, in the order the FIFO sends them: two pixels per
// word, the first pixel in the low half.
static inline void convertBurst(const uint8_t* p, uint32_t* words) {
    for (uint32_t i = 0; i < PX_PER_BURST / 2; i++) {
        words[i] = (uint32_t)s_lut[p[0]] | ((uint32_t)s_lut[p[1]] << 16);
        p += 2;
    }
}

// `total` pixels from `p`, already inside a window, in bursts -- the next
// burst converts while the previous one is on the wire.
static void pushPixels(const uint8_t* p, uint32_t total) {
    uint32_t words[PX_PER_BURST / 2];
    const uint8_t* end = p + total;
    convertBurst(p, words);
    p += PX_PER_BURST;
    for (;;) {
        while (READ_PERI_REG(SPI_CMD_REG(SPI_PORT)) & SPI_USR) {}
        for (uint32_t i = 0; i < PX_PER_BURST / 2; i++)
            WRITE_PERI_REG(SPI_W0_REG(SPI_PORT) + (i << 2), words[i]);
        SET_PERI_REG_MASK(SPI_CMD_REG(SPI_PORT), SPI_USR);
        if (p >= end) break;
        convertBurst(p, words);
        p += PX_PER_BURST;
    }
    while (READ_PERI_REG(SPI_CMD_REG(SPI_PORT)) & SPI_USR) {}
}
#endif  // !SQW_PUSH_DMA_ONLY

// The ESP32-C5's SPI clock, raised from inside the transaction.
//
// WHAT THE DEFAULT COSTS. Measured on an NM-CYD-C5: a full 320x240 push takes
// 82.2 ms, which is 0.343 ms per row and an effective 14.9 Mbit/s. Splitting
// that budget showed the CPU is barely in it -- the LUT conversion is 0.011 ms
// per row and all sixteen SPI_W register writes together are 0.012 ms (75 ns
// each) -- so 93% of the time is spent waiting for the wire.
//
// WHY IT IS SLOW, from the C5 TRM rather than by experiment. Ch.33.7 gives
// f_SPI = f_clk_spi_mst / ((SPI_CLKCNT_N + 1)(SPI_CLKDIV_PRE + 1)), and
// PCR_SPI2_CLKM_SEL (ch.9) defaults to XTAL_CLK, which is 48 MHz on this chip.
// The bus comes up at N=2: 48/3 = 16 MHz, which is exactly the 14.9 Mbit/s
// measured. Setting SPI_CLK_EQU_SYSCLK instead takes the module clock
// undivided.
//
// WHY IT HAS TO BE HERE, rather than in the board's user setup. SPI_FREQUENCY
// genuinely does nothing on this path: TFT_eSPI's begin_tft_write() calls
// spi.beginTransaction(SPISettings(SPI_FREQUENCY, ..)), which REWRITES
// SPI_CLOCK_REG -- so a divisor set before tft.startWrite() is overwritten
// before a single pixel moves. Built at 20, 40 and 80 MHz the register came
// back 0x00002001 every time. Set inside the transaction, it holds, and the
// next beginTransaction anywhere else (touch, SD) restores its own setting, so
// nothing outside this push is affected.
//
// MEASURED, same board, same frame, divisor set here:
//     N=3  12 MHz  107.9 ms   9.3 fps
//     N=2  16 MHz   82.2 ms  12.2 fps   <- the default
//     N=1  24 MHz   56.6 ms  17.7 fps
//     N=0  48 MHz   30.9 ms  32.4 fps   <- this
// Linear in the divisor, which is the proof that the wire was the ceiling.
//
// If a panel ever turns out not to like 48 MHz -- the symptom is torn or
// speckled rows, not a blank screen -- build with -DSQW_C5_SPI_DIV=1 for
// 24 MHz. Do not raise it further: 0 is already the undivided module clock.
// GOING FURTHER THAN THE DIVIDER: the module clock's SOURCE.
// PCR_SPI2_CLKM_CONF_REG (soc/pcr_reg.h, TRM ch.9) selects it at bits [21:20]
// -- 0 = XTAL_CLK, which is the 48 MHz default and the ceiling everything
// above runs into, 1 = PLL_F160M_CLK -- with an 8-bit divider at [19:12].
// PLL_F160M over two is an 80 MHz module clock, and undivided that is 80 MHz
// on the wire. Measured on the same frame: 20.5 ms, 48.8 fps, 60.0 Mbit/s,
// against 82.2 ms and 12.2 fps as shipped. Two different routes to 80 MHz
// (PLL/2 with N=0, and PLL/1 with N=1) gave identical times, which is the
// cross-check that the number is real.
//
// THE SOURCE IS RESTORED BEFORE THIS FUNCTION RETURNS, and that is not
// tidiness. PCR_SPI2_CLKM_SEL is global to SPI2, and on this board the
// XPT2046 touch controller and the SD card are on that same peripheral.
// Arduino's beginTransaction() works out its divisors believing the source is
// the crystal, so leaving the PLL selected would silently run every later
// transaction at 3.3x its requested frequency -- touch at 8 MHz against a
// rated 2.5 MHz, and an SD card at 13 MHz instead of 4. Nothing would report
// an error; touch would just start missing presses. Raised here, restored
// here, and only ever while this file owns the bus.

// ---- the wire by DMA: step one of overlapping the push with the drawing --
// On this chip the push is 20 ms of a 50 ms frame, and the register push
// above spends it SPINNING: 93% of the time is the wire (quietradio's
// measurement), and the one core this chip has stands and watches it. The
// plan is to hand the wire to the DMA engine and draw the next frame while it
// runs. This is the first step, and it changes the frame rate by nothing: the
// same rows go out, by DMA, with the CPU still waiting on each chunk -- so
// that the only question it answers is "does the driver's DMA path put the
// right picture on this panel". Eight rows at a time through two bounce
// buffers in internal RAM (DMA cannot read the frame, which is in PSRAM):
// one converts while the other is on the wire. Then the push moves to a
// task of its own and the waiting goes to the drawing (step two).
//
// -DSQW_C5_DMA=0 is the register push, unchanged.
//
// The Freenove S3 takes this same route, and only this route: the S3 has no
// register push to fall back on (frame_push.h), so a driver that will not
// start hands the frame back to pushSprite().
#ifndef SQW_C5_DMA
#define SQW_C5_DMA 1
#endif
#if (defined(NM_CYD_C5) && SQW_C5_DMA) || SQW_PUSH_DMA_ONLY
#define SQW_PUSH_DMA 1
#else
#define SQW_PUSH_DMA 0
#endif
#if SQW_PUSH_DMA
// Set by busWant(), cleared when the waiter takes the lock; read by the SPI
// push (pushRowsDma) to step aside mid-frame. Here, not beside that push,
// because the QSPI board has no such push and still defines busWant().
static volatile bool s_busWanted = false;
// Whether the panel's device holds the IDF driver's bus (a frame in flight;
// on the T-Deck and the Freenove S3, always, once the first frame has gone).
// Same reason as above.
static volatile bool s_pushHolds = false;   // the panel's device holds the driver's bus (a frame in flight)
static uint16_t* s_bounce[2] = { nullptr, nullptr };
static int32_t   s_bounceW  = 0;
static int8_t    s_dmaState = 0;             // 0 untried, 1 on, -1 not available
static const int32_t CHUNK_ROWS = 8;

#if SQW_PUSH_QSPI
// Panel rows a chunk: 32 x 320 x 2 bytes, 20 KB a buffer. Wider than the SPI
// boards' eight because in landscape a chunk of panel rows is a run of 32
// logical COLUMNS, and 32 bytes of a logical row is one cache line of the
// frame in PSRAM -- one fetch per logical row per chunk instead of one per
// pixel, which is the difference between a push and a crawl.
static const int32_t QSPI_CHUNK_ROWS = 32;

static bool pushDma(TFT_eSPI&, int32_t) {
    if (s_dmaState) return s_dmaState > 0;
    s_dmaState = -1;
    if (!St77922::ready()) { Serial.println("[push] QSPI: the panel never came up"); return false; }
    for (int k = 0; k < 2; k++)
        s_bounce[k] = (uint16_t*)heap_caps_malloc((size_t)St77922::PANEL_W * QSPI_CHUNK_ROWS * sizeof(uint16_t),
                                                  MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!s_bounce[0] || !s_bounce[1]) { Serial.println("[push] QSPI: no room for the bounce buffers"); return false; }
    s_dmaState = 1;
    Serial.printf("[push] QSPI on: %d-row chunks of %u bytes, heap %lu\n",
                  (int)QSPI_CHUNK_ROWS, (unsigned)(St77922::PANEL_W * QSPI_CHUNK_ROWS * 2), (unsigned long)ESP.getFreeHeap());
    return true;
}

// Logical rows [r0, r1) of a w x h frame in rotation `rot`, to the panel.
// The panel is addressed in its own portrait frame, so a span of logical
// rows is a band of panel rows in portrait and a band of panel COLUMNS in
// landscape; the window is set once and the band streams out in chunks of
// panel rows, each converted from the 8-bit frame as it goes. The mapping is
// touch_fit.h's screenToNative, the same one St77922TFT::toPanel() uses for
// rectangles, so a tap lands where the pixel went in all four rotations.
static void pushSpanQspi(const uint8_t* src, int32_t w, int32_t h, uint8_t rot, int32_t r0, int32_t r1) {
    (void)h;
    const int32_t W = St77922::PANEL_W, H = St77922::PANEL_H;
    int32_t px0, py0, px1, py1;
    switch (rot & 3) {
        case 0:  px0 = 0;      px1 = w - 1;      py0 = r0;     py1 = r1 - 1;     break;
        case 1:  px0 = W - r1; px1 = W - 1 - r0; py0 = 0;      py1 = w - 1;      break;
        case 2:  px0 = 0;      px1 = w - 1;      py0 = H - r1; py1 = H - 1 - r0; break;
        default: px0 = r0;     px1 = r1 - 1;     py0 = 0;      py1 = w - 1;      break;
    }
    St77922::setWindow((int16_t)px0, (int16_t)py0, (int16_t)px1, (int16_t)py1);
    const int32_t wOut = px1 - px0 + 1;
    bool first = true;
    int  k = 0;
    for (int32_t py = py0; py <= py1; py += QSPI_CHUNK_ROWS, k++) {
        const int32_t left = py1 - py + 1;
        const int32_t n = left < QSPI_CHUNK_ROWS ? left : QSPI_CHUNK_ROWS;
        uint16_t* b = s_bounce[k & 1];
        switch (rot & 3) {
            case 0:
                for (int32_t i = 0; i < n; i++) {
                    const uint8_t* s = src + (size_t)(py + i) * (size_t)w;
                    uint16_t* d = b + i * wOut;
                    for (int32_t x = 0; x < wOut; x++) d[x] = s_lut[s[x]];
                }
                break;
            case 2:
                for (int32_t i = 0; i < n; i++) {
                    const uint8_t* s = src + (size_t)(H - 1 - (py + i)) * (size_t)w;
                    uint16_t* d = b + i * wOut;
                    for (int32_t x = 0; x < wOut; x++) d[x] = s_lut[s[w - 1 - x]];
                }
                break;
            case 1:
                // Panel row py+i is logical column py+i; panel column px is
                // logical row W-1-px. One cache line of each logical row.
                for (int32_t ly = r0; ly < r1; ly++) {
                    const uint8_t* s = src + (size_t)ly * (size_t)w + py;
                    uint16_t* d = b + ((W - 1 - ly) - px0);
                    for (int32_t i = 0; i < n; i++) d[i * wOut] = s_lut[s[i]];
                }
                break;
            default:
                // Panel row py+i is logical column w-1-(py+i); panel column
                // px is logical row px.
                for (int32_t ly = r0; ly < r1; ly++) {
                    const uint8_t* s = src + (size_t)ly * (size_t)w + (w - 1 - py);
                    uint16_t* d = b + (ly - px0);
                    for (int32_t i = 0; i < n; i++) d[i * wOut] = s_lut[s[-i]];
                }
                break;
        }
        St77922::queuePixels(b, (size_t)n * (size_t)wOut, first);
        first = false;
    }
    St77922::wait();
}
#else

static bool pushDma(TFT_eSPI& tft, int32_t w) {
    if (s_dmaState) return s_dmaState > 0 && s_bounceW == w;
    s_dmaState = -1;
    for (int k = 0; k < 2; k++)
        s_bounce[k] = (uint16_t*)heap_caps_malloc((size_t)w * CHUNK_ROWS * sizeof(uint16_t),
                                                  MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!s_bounce[0] || !s_bounce[1]) {
        Serial.println("[push] DMA: no room for the bounce buffers; the plain push stays");
        return false;
    }
    // initDMA() says no when it has already been done -- which it has on the
    // T-Deck, whose LoRa radio shares this bus and set it up first.
    if (!tft.initDMA(false) && !tft.DMA_Enabled) {
        Serial.println("[push] DMA: the driver would not start; the plain push stays");
        return false;
    }
    tft.setSwapBytes(false);                 // the table is already in wire order
    s_bounceW  = w;
    s_dmaState = 1;
    Serial.printf("[push] DMA on: %d-row chunks of %u bytes, heap %lu\n",
                  (int)CHUNK_ROWS, (unsigned)(w * CHUNK_ROWS * 2), (unsigned long)ESP.getFreeHeap());
    return true;
}

// `rows` rows of `w` pixels from p, the window already set.
//
// DIAG: the first attempt, through the library's pushPixelsDMA (queued,
// interrupt-driven), hung in its wait on the very first chunk, nothing
// printed. This pass goes through the driver's POLLING transmit instead --
// no interrupt, the driver spins on the hardware itself -- and says where it
// is, to tell "the transfer never starts" from "the interrupt never comes".
// Returns the rows sent. Fewer than asked when something else asked for the
// bus (busWant()): the chunk in flight finishes, the rest waits -- the caller
// steps aside and sets the window again for what is left. A LoRa radio on
// this bus has a preamble to catch, and the ~30 ms of a whole frame was too
// long to wait (the T-Deck heard MeshCore's preambles and missed every
// header, 2026-10-12); a chunk is about a millisecond.
static void busStepAside();   // below, with the bus mutex it hands over
static int32_t pushRowsDma(TFT_eSPI& tft, const uint8_t* p, int32_t w, int32_t rows) {
    (void)tft;
    int32_t pushed = 0;
    // Queued, interrupt-driven, and pipelined: this chunk converts into one
    // bounce buffer while the one before is on the wire from the other, and
    // the wait for each is a block, not a spin -- the task sleeping here is
    // what gives the drawing the core.
    static spi_transaction_t tr[2];
    int k = 0;
    bool inFlight = false;
    while (rows > 0) {
        const int32_t n  = rows < CHUNK_ROWS ? rows : CHUNK_ROWS;
        const uint32_t px = (uint32_t)n * (uint32_t)w;
        uint16_t* b = s_bounce[k & 1];
        // Four pixels per read: the source is PSRAM, where a byte costs the
        // same as a word, and this task shares the one core with the
        // drawing, so every cycle here is a cycle the frame does not get.
        // Rows are a multiple of four pixels and word-aligned (rowHash
        // relies on the same), so px is too.
        {
            const uint32_t* p4 = (const uint32_t*)p;
            uint32_t*       b4 = (uint32_t*)b;
            for (uint32_t i = 0; i < px; i += 4) {
                const uint32_t v = *p4++;
                b4[0] = (uint32_t)s_lut[v & 0xff]         | ((uint32_t)s_lut[(v >> 8) & 0xff] << 16);
                b4[1] = (uint32_t)s_lut[(v >> 16) & 0xff] | ((uint32_t)s_lut[v >> 24] << 16);
                b4 += 2;
            }
        }
        if (inFlight) {
            spi_transaction_t* done = nullptr;
            spi_device_get_trans_result(dmaHAL, &done, portMAX_DELAY);
        }
        spi_transaction_t& t = tr[k & 1];
        memset(&t, 0, sizeof t);
        t.tx_buffer = b;
        t.length    = px * 16;
        const esp_err_t r = spi_device_queue_trans(dmaHAL, &t, portMAX_DELAY);
        if (r != ESP_OK) {
            Serial.printf("[push] DMA queue failed: %d; the register push takes over\n", (int)r);
            s_dmaState = -1;
            inFlight = false;
            break;
        }
        inFlight = true;
        p += px;
        rows -= n;
        pushed += n;
        k++;
        if (s_busWanted && rows > 0) break;   // the radio is waiting: let it in after this chunk
    }
    if (inFlight) { spi_transaction_t* done = nullptr; spi_device_get_trans_result(dmaHAL, &done, portMAX_DELAY); }
#if defined(NM_CYD_C5)
    // Back to CPU mode before the library writes a register again. The
    // driver leaves the engine's DMA enables set, and a register-fed
    // transfer with DMA_TX_ENA on waits for descriptors that never come:
    // that was the hang after the first chunk (2026-10-02).
    CLEAR_PERI_REG_MASK(SPI_DMA_CONF_REG(SPI_PORT), SPI_DMA_TX_ENA | SPI_DMA_RX_ENA);
    SET_PERI_REG_MASK(SPI_DMA_CONF_REG(SPI_PORT), SPI_BUF_AFIFO_RST | SPI_DMA_AFIFO_RST);
    CLEAR_PERI_REG_MASK(SPI_DMA_CONF_REG(SPI_PORT), SPI_BUF_AFIFO_RST | SPI_DMA_AFIFO_RST);
    SQW_SPI_LATCH();
#endif
    // On the S3 the library's own post-transfer callback (dma_end_callback in
    // its S3 processor port) clears SPI_DMA_CONF_REG after every transaction,
    // so its register-fed writes work again without any help from here.
    return pushed;
}
#endif  // !SQW_PUSH_QSPI
#endif  // SQW_PUSH_DMA

#if defined(NM_CYD_C5)

// 0 selects the undivided module clock; 1..63 divide it by N+1.
#ifndef SQW_C5_SPI_DIV
#define SQW_C5_SPI_DIV 0
#endif
// 1 sources the module clock from PLL_F160M/2 (80 MHz) instead of the 48 MHz
// crystal. Set to 0 to stay on the crystal: still 3x stock, and the fallback
// if a panel turns out not to like 80 MHz. The symptom of too fast is torn or
// speckled rows, never a blank screen.
#ifndef SQW_C5_SPI_PLL
#define SQW_C5_SPI_PLL 1
#endif

static inline uint32_t sqwC5SpiClockRaise() {
    const uint32_t pcr = READ_PERI_REG(PCR_SPI2_CLKM_CONF_REG);
#if SQW_C5_SPI_PLL
    WRITE_PERI_REG(PCR_SPI2_CLKM_CONF_REG,
                   (pcr & ~(0x3u << 20) & ~(0xFFu << 12)) | (1u << 20) | (1u << 12));
#endif
#if SQW_C5_SPI_DIV == 0
    WRITE_PERI_REG(SPI_CLOCK_REG(SPI_PORT), 1u << 31);   // SPI_CLK_EQU_SYSCLK
#else
    WRITE_PERI_REG(SPI_CLOCK_REG(SPI_PORT),
                   (((uint32_t)SQW_C5_SPI_DIV & 0x3F) << 12) |
                   ((((uint32_t)SQW_C5_SPI_DIV / 2) & 0x3F) << 6) |
                   ((uint32_t)SQW_C5_SPI_DIV & 0x3F));
#endif
    SQW_SPI_LATCH();
    return pcr;
}

static inline void sqwC5SpiClockRestore(uint32_t pcr) {
#if SQW_C5_SPI_PLL
    WRITE_PERI_REG(PCR_SPI2_CLKM_CONF_REG, pcr);
#else
    (void)pcr;
#endif
}

#endif

static uint32_t gcd32(uint32_t a, uint32_t b) { while (b) { uint32_t t = a % b; a = b; b = t; } return a; }

static bool pushNow(TFT_eSPI& tft, const uint8_t* src, int32_t w, int32_t h, int32_t x, int32_t y) {
    if (!s_ready || !s_enabled) return false;
    // Null when the sprite was never created, or was lost to a failed
    // re-create after a rotate. The caller's fallback handles it -- and
    // handles it by doing nothing, same as pushSprite() would.
    if (!src) return false;
    if (w <= 0 || h <= 0 || y < 0 || y + h > MAX_ROWS || (w & 3)) return false;
    const uint32_t total = (uint32_t)w * (uint32_t)h;
    if (total % PX_PER_BURST != 0) return false;

    // A span has to be whole bursts: 320 wide, any row is; 240 wide, two.
#if SQW_PUSH_QSPI
    // On the ST77922 a span of logical rows is a band of panel COLUMNS in
    // landscape, and the panel keeps its pixels in pairs: a window one
    // column wide, or starting on an odd column, came out as a smear across
    // the top of the screen (the title bar's rows, changing every frame).
    // Four rows at a time keeps every window even on both edges.
    const int32_t align = 4;
#else
    const int32_t align = (int32_t)(PX_PER_BURST / gcd32((uint32_t)w, PX_PER_BURST));
#endif

    // A frame is every band of it, so the periodic full refresh counts pushes
    // rather than frames: 64 pushes is 64 frames on one band and 32 on two.
    const bool full = !s_valid || s_validW != w || s_validH != h || (s_frameNo % 64) == 0;
    s_frameNo++;

    // Hash every row, and mark the ones to send. Full: all of them.
    static bool changed[MAX_ROWS];
    int32_t nChanged = 0;
    SQW_PUSH_CLOCK(tHash);
    for (int32_t r = 0; r < h; r++) {
        const uint32_t hv = rowHash(src + (size_t)r * (size_t)w, w);
        changed[r] = full || hv != s_rowHash[y + r];
#if defined(NM_CYD_C5) && SQW_C5_FLIP_DIAG
        if (s_valid && hv != s_rowHash[y + r] && hv == s_rowHash2[y + r]) s_flipRows++;
        s_rowHash2[y + r] = s_rowHash[y + r];
#endif
        s_rowHash[y + r] = hv;
        if (changed[r]) nChanged++;
    }
#if defined(NM_CYD_C5) && SQW_C5_FLIP_DIAG
    if (++s_flipFrames == 300) {
        Serial.printf("[push] rows that went A,B,A in 300 frames: %lu%s\n", (unsigned long)s_flipRows,
                      s_flipRows ? "  <-- a screen may be drawing on the frame before" : "");
        s_flipRows = 0; s_flipFrames = 0;
    }
#endif
    SQW_PUSH_CHARGE(s_hashUs, tHash);
    if (nChanged) {
        SQW_PUSH_CLOCK(tWire);
        // Exactly what pushSprite() -> pushImage() does around its own loop:
        // one transaction, chip select held low, the data/command line left
        // on data after each window. 512 bits per kick, as the library does.
        static RowSpan spans[96];
        int n = frameSpans(changed, h, align, spans, 96);
        // Belt and braces: a span that is not whole bursts (it cannot be,
        // for the sizes this is built for) becomes a full push.
        for (int i = 0; i < n; i++)
            if (((uint32_t)(spans[i].r1 - spans[i].r0) * (uint32_t)w) % PX_PER_BURST) { spans[0].r0 = 0; spans[0].r1 = h; n = 1; break; }
#if !SQW_PUSH_QSPI
        tft.startWrite();
#endif
#if SQW_PUSH_DMA
        // By DMA when the driver will have it; the register push, with its
        // raised clock, when not. The driver runs its own clock (see
        // SQW_C5_DMA_HZ), so the raise is only for the register path.
        const bool dma = pushDma(tft, w);
#if SQW_PUSH_QSPI
        if (!dma || x != 0 || y != 0) return false;   // whole frames only here
#elif SQW_PUSH_DMA_ONLY
        // No register push on this chip: the caller's pushSprite() instead.
        if (!dma) { tft.endWrite(); return false; }
#endif
#endif
#if defined(NM_CYD_C5)
        // The clock raise even by DMA: startWrite() has just set the SPI
        // clock register to the library's 20 MHz, and the driver programs
        // its own 80 MHz only when a different device last had the bus --
        // with one device it never does again, and its transfers ran at the
        // library's clock (28 Mbit/s measured, 1.45 ms per 2,560 pixels).
        // Inside the transaction, after beginTransaction has had its say.
        const uint32_t c5pcr = sqwC5SpiClockRaise();
#endif
#if SQW_PUSH_DMA && !SQW_PUSH_QSPI
        // The bus for the whole frame: the driver's per-transfer lock is a
        // scheduling point, and on one core that is where the radio tasks
        // get in between chunks.
        // On the T-Deck the hold is taken once and KEPT: its radio shares
        // this host and works in the gaps between chunks and frames, and the
        // driver's interrupt handler wakes "the device holding the bus" on
        // its way out of every transaction -- with that changing hands every
        // frame it dereferenced a stale one, LoadProhibited in
        // spi_bus_lock_bg_exit every 15 s to 6 min (the black box,
        // 2026-10-13). Nothing else on that board is a device of this driver.
        //
        // The Freenove S3 keeps it too, for a reason that has nothing to do
        // with a radio. The S3 has two cores and the pusher is not pinned to
        // either, so it can release the bus on one core while the interrupt
        // for the frame's last chunk is still on its way out on the other.
        // The IDF's bg_exit (4.4.6) reads the lock's acquiring device, checks
        // it, then reads it AGAIN to wake it -- and the release had cleared it
        // in between: LoadProhibited at address 0 in spi_bus_lock_bg_exit,
        // from spi_intr, every 10 to 40 minutes (core dump, 2026-10-15). With
        // the hold never let go, that field never goes back to null. Nothing
        // else on the Freenove is a device of this driver either. The C5 still
        // lets go each frame: one core, so the interrupt cannot be mid-exit
        // while the pusher runs.
        if (dma && !s_pushHolds) { spi_device_acquire_bus(dmaHAL, portMAX_DELAY); s_pushHolds = true; }
#endif
        for (int i = 0; i < n; i++) {
            const int32_t r0 = spans[i].r0, r1 = spans[i].r1;
#if SQW_PUSH_QSPI
            pushSpanQspi(src, w, h, tft.getRotation(), r0, r1);
            s_lastRows += r1 - r0;
            continue;
#else
            tft.setAddrWindow(x, y + r0, w, r1 - r0);
#endif
#if SQW_PUSH_DMA && !SQW_PUSH_QSPI
            if (dma) {
                int32_t at = r0;
                while (at < r1) {
                    const int32_t did = pushRowsDma(tft, src + (size_t)at * (size_t)w, w, r1 - at);
                    s_lastRows += did;
                    at += did;
                    if (did == 0 || at >= r1) break;
                    // Stepping aside: the bus and the panel's select released,
                    // the lock handed over for as long as the other side needs
                    // it, then everything taken back and the window set for
                    // the rows still to go. Only when this task holds the lock
                    // (the pusher); a caller pushing synchronously carries on.
                    // The device's hold on the driver is KEPT across the
                    // hand-over: the T-Deck's radio sends its bytes through
                    // the host's registers under that hold, the way the
                    // library's own window writes do. Giving the hold up and
                    // taking it back here, and the radio taking one of its own,
                    // hundreds of times a second, ended in the driver's
                    // interrupt handler on an empty device entry (2026-10-13).
                    tft.endWrite();
                    busStepAside();
                    tft.startWrite();
                    tft.setAddrWindow(x, y + at, w, r1 - at);
                }
                continue;
            }
#endif
#if !SQW_PUSH_DMA_ONLY
            WRITE_PERI_REG(SPI_MOSI_DLEN_REG(SPI_PORT), 511);
            // Once per span, not once per burst. Per the C5 TRM (ch.33, SPI
            // Controller): SPI_UPDATE "synchronize[s] SPI registers from APB
            // clock domain into SPI module clock domain", i.e. CONFIGURATION.
            // The only configuration changing here is the burst length just
            // written above -- setAddrWindow's own commands left it at 8, 16
            // or 32 bits, so it genuinely must be re-latched. The data buffer
            // SPI_W0..W15 is not configuration: the TRM has the SPI module
            // reading TX data straight out of it during the transfer, so the
            // per-burst refills below need no latch of their own.
            //
            // Measured: moving the latch here from inside the burst loop cost
            // and saved nothing -- 0.378 ms per 320-pixel row either way. It
            // stays here because it is the placement the TRM justifies, not
            // because it is faster. The row time is dominated by the wire:
            // 320 px x 16 bits at the 20 MHz this board's setup asks for is
            // 0.256 ms of pure SPI, so roughly two thirds of it is unavoidable
            // at that clock.
            SQW_SPI_LATCH();
            pushPixels(src + (size_t)r0 * (size_t)w, (uint32_t)(r1 - r0) * (uint32_t)w);
            s_lastRows += r1 - r0;
#endif  // !SQW_PUSH_DMA_ONLY
        }
#if SQW_PUSH_DMA && !SQW_PUSH_QSPI
#if !defined(TDECK_PLUS) && !defined(FREENOVE_S3)
        if (dma) { s_pushHolds = false; spi_device_release_bus(dmaHAL); }
#endif
#endif
#if defined(NM_CYD_C5)
        sqwC5SpiClockRestore(c5pcr);
#endif
#if !SQW_PUSH_QSPI
        tft.endWrite();
#endif
        SQW_PUSH_CHARGE(s_wireUs, tWire);
    }
    s_valid  = true;
    s_validW = w;
    s_validH = h;
    return true;
}

bool push(TFT_eSPI& tft, const uint8_t* src, int32_t w, int32_t h, int32_t x, int32_t y) {
    return pushNow(tft, src, w, h, x, y);
}

#if SQW_PUSH_DMA
// ---- step two: the push in a task of its own ----------------------------
// The loop hands over a finished frame (asyncSubmit) and goes on drawing
// the next into another buffer; this task hashes, converts and pushes it,
// sleeping on the DMA between chunks, which is when the drawing runs. Three
// semaphores: `go` (a frame is waiting), `done` (the panel has it), and the
// bus mutex that everything else on the SPI bus takes first.
static TaskHandle_t      s_task  = nullptr;
static SemaphoreHandle_t s_go    = nullptr;
static SemaphoreHandle_t s_done  = nullptr;
static SemaphoreHandle_t s_bus   = nullptr;
static TFT_eSPI*         s_tft   = nullptr;
static const uint8_t*    s_jobSrc = nullptr;
static int32_t           s_jobW = 0, s_jobH = 0, s_jobX = 0, s_jobY = 0;

static void pusherTask(void*) {
    for (;;) {
        xSemaphoreTake(s_go, portMAX_DELAY);
        xSemaphoreTakeRecursive(s_bus, portMAX_DELAY);
        if (!pushNow(*s_tft, s_jobSrc, s_jobW, s_jobH, s_jobX, s_jobY)) {
#if !SQW_PUSH_QSPI
            // A shape it does not know, or the switch is off: the library's
            // own 8-bit image push, from here, so the frame still arrives.
            s_tft->pushImage(s_jobX, s_jobY, s_jobW, s_jobH, (uint8_t*)s_jobSrc, true, nullptr);
#endif
            s_valid = false;
        }
        xSemaphoreGiveRecursive(s_bus);
        xSemaphoreGive(s_done);
    }
}

bool asyncBegin(TFT_eSPI& tft) {
    if (s_task) return true;
    s_tft  = &tft;
    s_go   = xSemaphoreCreateBinary();
    s_done = xSemaphoreCreateBinary();
    // Recursive: on the QSPI board a direct draw takes it from inside a
    // busLock() the loop already holds (main.cpp's "straight to the panel").
    s_bus  = xSemaphoreCreateRecursiveMutex();
    if (!s_go || !s_done || !s_bus) return false;
    xSemaphoreGive(s_done);                  // nothing in flight yet
    // Above the loop task (1), below the radios: it wakes for a fifth of a
    // millisecond per chunk and sleeps the rest.
    if (xTaskCreate(pusherTask, "framePush", 4096, nullptr, 3, &s_task) != pdPASS) { s_task = nullptr; return false; }
    Serial.println("[push] the push runs in its own task; the loop draws while the wire works");
    return true;
}

bool asyncOn() { return s_task != nullptr; }

bool asyncSubmit(const uint8_t* src, int32_t w, int32_t h, int32_t x, int32_t y) {
    if (!s_task || !s_ready || !s_enabled) return false;
    xSemaphoreTake(s_done, portMAX_DELAY);   // the frame before is on the panel; its buffer is free
    s_jobSrc = src; s_jobW = w; s_jobH = h; s_jobX = x; s_jobY = y;
    xSemaphoreGive(s_go);
    return true;
}

void asyncWait() {
    if (!s_task) return;
    xSemaphoreTake(s_done, portMAX_DELAY);
    xSemaphoreGive(s_done);
}

// The pusher, mid-frame, letting a waiter in: the lock given up, a yield so
// the waiter (the same priority) gets the core, and the lock taken back,
// which blocks until the waiter is done. Only when this task holds it: a
// synchronous push from the loop carries on.
static void busStepAside() {
    if (!s_bus || xSemaphoreGetMutexHolder(s_bus) != xTaskGetCurrentTaskHandle()) return;
    xSemaphoreGiveRecursive(s_bus);
    taskYIELD();
    // The give only makes the waiter READY. The T-Deck's radio task is a
    // lower priority than this one, or on the other core, and either way
    // the take below won the race nearly every time: the radio got the bus
    // only between frames, 14-27 ms of waiting per turn (2026-10-13). So
    // sleep a tick at a time until it has the lock (busLock() clears the
    // flag once it does), a few ticks at most.
    for (int i = 0; i < 4 && s_busWanted; i++) vTaskDelay(1);
    if (xSemaphoreTakeRecursive(s_bus, pdMS_TO_TICKS(500)) != pdTRUE) {
        Serial.printf("[push] step-aside: the lock did not come back in 500 ms (holder %p)\n", (void*)xSemaphoreGetMutexHolder(s_bus));
        xSemaphoreTakeRecursive(s_bus, portMAX_DELAY);
    }
}
void busWant()   { s_busWanted = true; }
bool pushHoldsBus() { return s_pushHolds; }
void busLock()   { if (s_bus) xSemaphoreTakeRecursive(s_bus, portMAX_DELAY); s_busWanted = false; }
void busUnlock() { if (s_bus) xSemaphoreGiveRecursive(s_bus); }
#else
void busWant() {}
bool pushHoldsBus() { return false; }
bool asyncBegin(TFT_eSPI&) { return false; }
bool asyncOn() { return false; }
bool asyncSubmit(const uint8_t*, int32_t, int32_t, int32_t, int32_t) { return false; }
void asyncWait() {}
void busLock() {}
void busUnlock() {}
#endif

#else  // every other board, and the emulator: built out, not switched off

bool begin() { return false; }
bool available() { return false; }
void setEnabled(bool) {}
bool enabled() { return false; }
void invalidate() {}
void newFrame() {}
int32_t lastRows() { return 0; }
uint32_t hashUs() { return 0; }
uint32_t wireUs() { return 0; }
bool push(TFT_eSPI&, const uint8_t*, int32_t, int32_t, int32_t, int32_t) { return false; }
bool asyncBegin(TFT_eSPI&) { return false; }
bool asyncOn() { return false; }
bool asyncSubmit(const uint8_t*, int32_t, int32_t, int32_t, int32_t) { return false; }
void asyncWait() {}
void busWant() {}
void busLock() {}
void busUnlock() {}

#endif

}  // namespace FramePush
