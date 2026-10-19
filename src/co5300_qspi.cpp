// SquachWatch-CYD — the CO5300 AMOLED over QSPI. See co5300_qspi.h.
#include "co5300_qspi.h"
#if defined(WAVESHARE_206)
#include <Arduino.h>
#include <string.h>
#include <driver/spi_master.h>
#include <esp_heap_caps.h>

namespace Co5300 {

// 40 MHz is what the board's BSP runs the panel at. The pins are not the
// IO_MUX ones, and the GPIO matrix is only promised to 40 MHz; four lanes of
// it is still 2.6 Mbit of watch frame in 16 ms.
#ifndef SQW_QSPI_HZ
#define SQW_QSPI_HZ 40000000
#endif
static const uint32_t QSPI_HZ = SQW_QSPI_HZ;
static const uint8_t  CMD_1W = 0x02, CMD_4W = 0x32;
static const uint8_t  RAM_WRITE = 0x2C, RAM_CONTINUE = 0x3C;

static spi_device_handle_t   s_dev = nullptr;
static bool                  s_ready = false;
static bool                  s_inFlight = false;
static spi_transaction_ext_t s_tx;              // the one pixel transfer in flight
static uint16_t*             s_fill = nullptr;  // a DMA-capable run of one colour, for fillRect
static const size_t          FILL_PX = 2048;

// The board's BSP sequence (lcd_init_cmds in esp32_s3_touch_amoled_2_06.c),
// with the two writes esp_lcd_sh8601 makes before it -- pixel format 16 bits
// (0x3A 0x55) and MADCTL 0 (portrait, RGB) -- and the brightness left at 0
// until the glass has been cleared. Register, byte count, milliseconds to
// wait after, the bytes.
static const uint8_t INIT[] = {
    0x11, 0, 120,                          // sleep out
    0x3A, 1, 0,   0x55,                    // 16 bits a pixel
    0x36, 1, 0,   0x00,                    // MADCTL: portrait, RGB
    0xC4, 1, 0,   0x80,
    0x44, 2, 0,   0x01, 0xD1,              // tearing scan line
    0x35, 1, 0,   0x00,                    // tearing effect on
    0x53, 1, 10,  0x20,                    // brightness control on
    0x63, 1, 10,  0xFF,
    0x51, 1, 10,  0x00,                    // brightness 0 while RAM is noise
    0x2A, 4, 0,   0x00, 0x16, 0x01, 0xAF,  // columns 22..431
    0x2B, 4, 0,   0x00, 0x00, 0x01, 0xF5,  // rows 0..501
    0x29, 0, 10,                           // display on
};

bool ready() { return s_ready; }

void writeReg(uint8_t reg, const uint8_t* data, size_t len) {
    if (!s_ready) return;
    wait();
    spi_transaction_ext_t t;
    memset(&t, 0, sizeof t);
    t.base.flags   = SPI_TRANS_VARIABLE_CMD | SPI_TRANS_VARIABLE_ADDR;
    t.base.cmd     = CMD_1W;
    t.base.addr    = (uint32_t)reg << 8;
    t.command_bits = 8;
    t.address_bits = 24;
    uint8_t tmp[16];
    if (len) {
        // INIT lives in flash, which DMA cannot read: copy it out first.
        if (len > sizeof tmp) len = sizeof tmp;
        memcpy(tmp, data, len);
        t.base.tx_buffer = tmp;
        t.base.length    = 8 * len;
    }
    spi_device_polling_transmit(s_dev, &t.base);
}

bool begin() {
    if (s_ready) return true;
    pinMode(IO_RST, OUTPUT);
    digitalWrite(IO_RST, HIGH); delay(5);
    digitalWrite(IO_RST, LOW);  delay(20);
    digitalWrite(IO_RST, HIGH); delay(150);

    spi_bus_config_t bus;
    memset(&bus, 0, sizeof bus);
    bus.mosi_io_num     = IO_D0;
    bus.miso_io_num     = IO_D1;
    bus.sclk_io_num     = IO_SCLK;
    bus.quadwp_io_num   = IO_D2;
    bus.quadhd_io_num   = IO_D3;
    bus.data4_io_num    = -1;
    bus.data5_io_num    = -1;
    bus.data6_io_num    = -1;
    bus.data7_io_num    = -1;
    bus.max_transfer_sz = (int)(MAX_PIXELS * 2 + 8);
    bus.flags           = SPICOMMON_BUSFLAG_MASTER | SPICOMMON_BUSFLAG_QUAD;
    esp_err_t r = spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO);
    if (r != ESP_OK) { Serial.printf("[co5300] bus would not start: %d\n", (int)r); return false; }
    spi_device_interface_config_t dev;
    memset(&dev, 0, sizeof dev);
    dev.command_bits   = 0;      // set per transaction
    dev.address_bits   = 0;
    dev.mode           = 0;
    dev.clock_speed_hz = (int)QSPI_HZ;
    dev.spics_io_num   = IO_CS;
    dev.flags          = SPI_DEVICE_HALFDUPLEX;
    dev.queue_size     = 4;
    r = spi_bus_add_device(SPI2_HOST, &dev, &s_dev);
    if (r != ESP_OK) { Serial.printf("[co5300] device would not add: %d\n", (int)r); return false; }
    s_fill = (uint16_t*)heap_caps_malloc(FILL_PX * sizeof(uint16_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    s_ready = true;
    for (size_t i = 0; i < sizeof INIT;) {
        const uint8_t reg = INIT[i], n = INIT[i + 1], ms = INIT[i + 2];
        writeReg(reg, INIT + i + 3, n);
        if (ms) delay(ms);
        i += 3 + n;
    }
    // Black everywhere, once: the bands above and below the watch frame are
    // never written again, and on an AMOLED black is simply off.
    fillRect(0, 0, PANEL_W, PANEL_H, 0x0000);
    Serial.printf("[co5300] up: %dx%d over QSPI at %lu MHz, watch frame %dx%d at %d,%d, fill run %s\n",
                  (int)PANEL_W, (int)PANEL_H, (unsigned long)(QSPI_HZ / 1000000), (int)OUT, (int)OUT,
                  (int)OFF_X, (int)OFF_Y, s_fill ? "ready" : "NOT allocated");
#if defined(SQW_QSPI_TEST)
    // Bring-up only (-DSQW_QSPI_TEST=1): four quadrants of the watch square
    // at full brightness for three seconds. Top-left red, top-right green,
    // bottom-left blue, bottom-right white.
    brightness(255);
    fillRect(OFF_X,           OFF_Y,           OUT / 2, OUT / 2, 0xF800);
    fillRect(OFF_X + OUT / 2, OFF_Y,           OUT / 2, OUT / 2, 0x07E0);
    fillRect(OFF_X,           OFF_Y + OUT / 2, OUT / 2, OUT / 2, 0x001F);
    fillRect(OFF_X + OUT / 2, OFF_Y + OUT / 2, OUT / 2, OUT / 2, 0xFFFF);
    Serial.println("[co5300] TEST: red | green over blue | white, for 3 s");
    delay(3000);
    brightness(0);
#endif
    return true;
}

void setWindow(int16_t x0, int16_t y0, int16_t x1, int16_t y1) {
    x0 += COL_GAP; x1 += COL_GAP;
    const uint8_t xs[4] = { (uint8_t)(x0 >> 8), (uint8_t)x0, (uint8_t)(x1 >> 8), (uint8_t)x1 };
    const uint8_t ys[4] = { (uint8_t)(y0 >> 8), (uint8_t)y0, (uint8_t)(y1 >> 8), (uint8_t)y1 };
    writeReg(0x2A, xs, 4);
    writeReg(0x2B, ys, 4);
}

void wait() {
    if (!s_inFlight) return;
    spi_transaction_t* done = nullptr;
    spi_device_get_trans_result(s_dev, &done, portMAX_DELAY);
    s_inFlight = false;
}

bool queuePixels(const uint16_t* dma, size_t count, bool first) {
    if (!s_ready || !count) return false;
    if (count > MAX_PIXELS) count = MAX_PIXELS;
    wait();
    memset(&s_tx, 0, sizeof s_tx);
    s_tx.base.flags     = SPI_TRANS_MODE_QIO | SPI_TRANS_VARIABLE_CMD | SPI_TRANS_VARIABLE_ADDR;
    s_tx.base.cmd       = CMD_4W;
    s_tx.base.addr      = (uint32_t)(first ? RAM_WRITE : RAM_CONTINUE) << 8;
    s_tx.command_bits   = 8;
    s_tx.address_bits   = 24;
    s_tx.base.tx_buffer = dma;
    s_tx.base.length    = count * 16;
    const esp_err_t r = spi_device_queue_trans(s_dev, &s_tx.base, portMAX_DELAY);
    if (r != ESP_OK) { Serial.printf("[co5300] queue failed: %d\n", (int)r); return false; }
    s_inFlight = true;
    return true;
}

void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t rgb565) {
    if (!s_ready || !s_fill || w <= 0 || h <= 0) return;
    int16_t x1 = x + w, y1 = y + h;   // exclusive
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x1 > PANEL_W) x1 = PANEL_W;
    if (y1 > PANEL_H) y1 = PANEL_H;
    // Even on both edges, as the controller wants. PANEL_W and PANEL_H are
    // both even, so widening never runs off the glass.
    x &= ~1; y &= ~1;
    x1 = (int16_t)((x1 + 1) & ~1); y1 = (int16_t)((y1 + 1) & ~1);
    if (x1 <= x || y1 <= y) return;
    const uint16_t c = wire(rgb565);
    for (size_t i = 0; i < FILL_PX; i++) s_fill[i] = c;
    setWindow(x, y, x1 - 1, y1 - 1);
    size_t left = (size_t)(x1 - x) * (size_t)(y1 - y);
    bool first = true;
    while (left) {
        const size_t n = left < FILL_PX ? left : FILL_PX;
        queuePixels(s_fill, n, first);
        first = false;
        left -= n;
    }
    wait();
}

void invert(bool on) { writeReg(on ? 0x21 : 0x20, nullptr, 0); }

void brightness(uint8_t level) { writeReg(0x51, &level, 1); }

}  // namespace Co5300
#endif  // WAVESHARE_206
