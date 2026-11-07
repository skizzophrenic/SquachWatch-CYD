// SquachWatch-CYD — the AXS15231B over QSPI. See axs15231b_qspi.h.
#include "axs15231b_qspi.h"
#if defined(JC3248W535)
#include <Arduino.h>
#include <string.h>
#include <driver/spi_master.h>
#include <esp_heap_caps.h>

namespace Axs15231b {

#ifndef SQW_QSPI_HZ
// The JC3248W535's QSPI pins are NOT on the FSPI IO_MUX, so the GPIO matrix
// is in the path and 80 MHz is not achievable.  40 MHz is safe; some boards
// run at 20 MHz.  If artefacts appear, lower this with -DSQW_QSPI_HZ=20000000.
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

// AXS15231B initialisation sequence, from the Espressif esp_lcd_axs15231b
// component and working JC3248W535 boards (esp-iot-solution #579).
// Format: register, byte count, milliseconds to wait after, the bytes.
// The sequence brackets vendor registers with 0xBB unlock/lock, sets up
// panel timing and gamma (0xA0–0xE5), then finishes with normal mode (0x13),
// sleep out (0x11, 120 ms), and a dummy RAMWR (0x2C) to prime the write.
static const uint8_t INIT[] = {
    // Vendor unlock: 0xBB with key 5A A5 in bytes 6-7
    0xBB, 8, 0,  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x5A, 0xA5,

    // Panel timing / source / gate configuration
    0xA0, 17, 0, 0xC0, 0x10, 0x00, 0x02, 0x00, 0x00, 0x04, 0x3F,
                 0x20, 0x05, 0x3F, 0x3F, 0x00, 0x00, 0x00, 0x00, 0x00,

    0xA2, 31, 0, 0x30, 0x3C, 0x24, 0x14, 0xD0, 0x20, 0xFF, 0xE0,
                 0x40, 0x19, 0x80, 0x80, 0x80, 0x20, 0xF9, 0x10,
                 0x02, 0xFF, 0xFF, 0xF0, 0x90, 0x01, 0x32, 0xA0,
                 0x91, 0xE0, 0x20, 0x7F, 0xFF, 0x00, 0x5A,

    0xD0, 30, 0, 0xE0, 0x40, 0x51, 0x24, 0x08, 0x05, 0x10, 0x01,
                 0x20, 0x15, 0x42, 0xC2, 0x22, 0x22, 0xAA, 0x03,
                 0x10, 0x12, 0x60, 0x14, 0x1E, 0x51, 0x15, 0x00,
                 0x8A, 0x20, 0x00, 0x03, 0x3A, 0x12,

    0xA3, 22, 0, 0xA0, 0x06, 0xAA, 0x00, 0x08, 0x02, 0x0A, 0x04,
                 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
                 0x04, 0x04, 0x04, 0x00, 0x55, 0x55,

    0xC1, 30, 0, 0x31, 0x04, 0x02, 0x02, 0x71, 0x05, 0x24, 0x55,
                 0x02, 0x00, 0x41, 0x00, 0x53, 0xFF, 0xFF, 0xFF,
                 0x4F, 0x52, 0x00, 0x4F, 0x52, 0x00, 0x45, 0x3B,
                 0x0B, 0x02, 0x0D, 0x00, 0xFF, 0x40,

    0xC3, 11, 0, 0x00, 0x00, 0x00, 0x50, 0x03, 0x00, 0x00, 0x00,
                 0x01, 0x80, 0x01,

    0xC4, 29, 0, 0x00, 0x24, 0x33, 0x80, 0x00, 0xEA, 0x64, 0x32,
                 0xC8, 0x64, 0xC8, 0x32, 0x90, 0x90, 0x11, 0x06,
                 0xDC, 0xFA, 0x00, 0x00, 0x80, 0xFE, 0x10, 0x10,
                 0x00, 0x0A, 0x0A, 0x44, 0x50,

    0xC5, 23, 0, 0x18, 0x00, 0x00, 0x03, 0xFE, 0x3A, 0x4A, 0x20,
                 0x30, 0x10, 0x88, 0xDE, 0x0D, 0x08, 0x0F, 0x0F,
                 0x01, 0x3A, 0x4A, 0x20, 0x10, 0x10, 0x00,

    0xC6, 20, 0, 0x05, 0x0A, 0x05, 0x0A, 0x00, 0xE0, 0x2E, 0x0B,
                 0x12, 0x22, 0x12, 0x22, 0x01, 0x03, 0x00, 0x3F,
                 0x6A, 0x18, 0xC8, 0x22,

    0xC7, 20, 0, 0x50, 0x32, 0x28, 0x00, 0xA2, 0x80, 0x8F, 0x00,
                 0x80, 0xFF, 0x07, 0x11, 0x9C, 0x67, 0xFF, 0x24,
                 0x0C, 0x0D, 0x0E, 0x0F,

    0xC9,  4, 0, 0x33, 0x44, 0x44, 0x01,

    0xCF, 27, 0, 0x2C, 0x1E, 0x88, 0x58, 0x13, 0x18, 0x56, 0x18,
                 0x1E, 0x68, 0x88, 0x00, 0x65, 0x09, 0x22, 0xC4,
                 0x0C, 0x77, 0x22, 0x44, 0xAA, 0x55, 0x08, 0x08,
                 0x12, 0xA0, 0x08,

    0xD5, 30, 0, 0x40, 0x8E, 0x8D, 0x01, 0x35, 0x04, 0x92, 0x74,
                 0x04, 0x92, 0x74, 0x04, 0x08, 0x6A, 0x04, 0x46,
                 0x03, 0x03, 0x03, 0x03, 0x82, 0x01, 0x03, 0x00,
                 0xE0, 0x51, 0xA1, 0x00, 0x00, 0x00,

    0xD6, 30, 0, 0x10, 0x32, 0x54, 0x76, 0x98, 0xBA, 0xDC, 0xFE,
                 0x93, 0x00, 0x01, 0x83, 0x07, 0x07, 0x00, 0x07,
                 0x07, 0x00, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
                 0x00, 0x84, 0x00, 0x20, 0x01, 0x00,

    0xD7, 19, 0, 0x03, 0x01, 0x0B, 0x09, 0x0F, 0x0D, 0x1E, 0x1F,
                 0x18, 0x1D, 0x1F, 0x19, 0x40, 0x8E, 0x04, 0x00,
                 0x20, 0xA0, 0x1F,

    0xD8, 12, 0, 0x02, 0x00, 0x0A, 0x08, 0x0E, 0x0C, 0x1E, 0x1F,
                 0x18, 0x1D, 0x1F, 0x19,

    0xD9, 12, 0, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F,
                 0x1F, 0x1F, 0x1F, 0x1F,

    0xDD, 12, 0, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F,
                 0x1F, 0x1F, 0x1F, 0x1F,

    0xDF,  8, 0, 0x44, 0x73, 0x4B, 0x69, 0x00, 0x0A, 0x02, 0x90,

    // Gamma curves (positive / negative, R / G / B)
    0xE0, 17, 0, 0x3B, 0x28, 0x10, 0x16, 0x0C, 0x06, 0x11, 0x28,
                 0x5C, 0x21, 0x0D, 0x35, 0x13, 0x2C, 0x33, 0x28, 0x0D,
    0xE1, 17, 0, 0x37, 0x28, 0x10, 0x16, 0x0B, 0x06, 0x11, 0x28,
                 0x5C, 0x21, 0x0D, 0x35, 0x14, 0x2C, 0x33, 0x28, 0x0F,
    0xE2, 17, 0, 0x3B, 0x07, 0x12, 0x18, 0x0E, 0x0D, 0x17, 0x35,
                 0x44, 0x32, 0x0C, 0x14, 0x14, 0x36, 0x3A, 0x2F, 0x0D,
    0xE3, 17, 0, 0x37, 0x07, 0x12, 0x18, 0x0E, 0x0D, 0x17, 0x35,
                 0x44, 0x32, 0x0C, 0x14, 0x14, 0x36, 0x32, 0x2F, 0x0F,
    0xE4, 17, 0, 0x3B, 0x07, 0x12, 0x18, 0x0E, 0x0D, 0x17, 0x39,
                 0x44, 0x2E, 0x0C, 0x14, 0x14, 0x36, 0x3A, 0x2F, 0x0D,
    0xE5, 17, 0, 0x37, 0x07, 0x12, 0x18, 0x0E, 0x0D, 0x17, 0x39,
                 0x44, 0x2E, 0x0C, 0x14, 0x14, 0x36, 0x3A, 0x2F, 0x0F,

    // Power / VCOM
    0xA4, 16, 0, 0x85, 0x85, 0x95, 0x82, 0xAF, 0xAA, 0xAA, 0x80,
                 0x10, 0x30, 0x40, 0x40, 0x20, 0xFF, 0x60, 0x30,
    0xA4,  4, 0, 0x85, 0x85, 0x95, 0x85,

    // Vendor lock (close vendor command space)
    0xBB, 8, 0,  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

    // Normal display mode, sleep out (120 ms), memory write prime
    0x13, 0,   0,
    0x11, 0, 120,
    // Pixel format: RGB565, after sleep out so it sticks
    0x3A, 1,   0,  0x55,
    // MADCTL: portrait, RGB order
    0x36, 1,   0,  0x00,
    // Display ON
    0x29, 0,  20,
};

void writeReg(uint8_t reg, const uint8_t* data, size_t len);
bool ready() { return s_ready; }

void reinit() {
    if (!s_ready) return;
    for (size_t i = 0; i < sizeof INIT;) {
        const uint8_t reg = INIT[i], n = INIT[i + 1], ms = INIT[i + 2];
        writeReg(reg, INIT + i + 3, n);
        if (ms) delay(ms);
        i += 3 + n;
    }
}

// A one-lane command: `reg` in the address, `len` bytes after. Synchronous.
void writeReg(uint8_t reg, const uint8_t* data, size_t len) {
    if (!s_ready) return;
    wait();
    spi_transaction_ext_t t;
    memset(&t, 0, sizeof t);
    t.base.flags     = SPI_TRANS_VARIABLE_CMD | SPI_TRANS_VARIABLE_ADDR;
    t.base.cmd       = CMD_1W;
    t.base.addr      = (uint32_t)reg << 8;
    t.command_bits   = 8;
    t.address_bits   = 24;
    if (len) {
        // The AXS15231B vendor init has registers up to 31 bytes long.
        // The driver copies anything under four words into its own DMA-safe
        // space; INIT lives in flash, which DMA cannot read. For longer
        // payloads we use a stack copy.
        uint8_t tmp[32];
        if (len > sizeof tmp) len = sizeof tmp;
        memcpy(tmp, data, len);
        t.base.tx_buffer = tmp;
        t.base.length    = 8 * len;
        spi_device_polling_transmit(s_dev, &t.base);
    } else {
        spi_device_polling_transmit(s_dev, &t.base);
    }
}

bool begin() {
    if (s_ready) return true;
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
    // No IOMUX_PINS flag: the JC3248W535's QSPI pins are not on the FSPI
    // IO_MUX, so the GPIO matrix is needed.
    // SPI3_HOST, not SPI2_HOST: the Arduino SPI library's default `SPI`
    // object lives on SPI2_HOST, and sd_log.cpp's SPI.begin() for the SD
    // card would reconfigure that bus with the card's own pins, corrupting
    // the display's QSPI configuration.  SPI3_HOST is free.
    bus.flags           = SPICOMMON_BUSFLAG_MASTER | SPICOMMON_BUSFLAG_QUAD;
    esp_err_t r = spi_bus_initialize(SPI3_HOST, &bus, SPI_DMA_CH_AUTO);
    if (r != ESP_OK) { Serial.printf("[axs15231b] bus would not start: %d\n", (int)r); return false; }
    spi_device_interface_config_t dev;
    memset(&dev, 0, sizeof dev);
    dev.command_bits   = 0;      // set per transaction
    dev.address_bits   = 0;
    dev.mode           = 0;
    dev.clock_speed_hz = (int)QSPI_HZ;
    dev.spics_io_num   = IO_CS;
    dev.flags          = SPI_DEVICE_HALFDUPLEX;
    dev.queue_size     = 4;
    r = spi_bus_add_device(SPI3_HOST, &dev, &s_dev);
    if (r != ESP_OK) { Serial.printf("[axs15231b] device would not add: %d\n", (int)r); return false; }
    s_fill = (uint16_t*)heap_caps_malloc(FILL_PX * sizeof(uint16_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    s_ready = true;
    reinit();
    Serial.printf("[axs15231b] up: 320x480 over QSPI at %lu MHz, fill run %s\n",
                  (unsigned long)(QSPI_HZ / 1000000), s_fill ? "ready" : "NOT allocated");
#if defined(SQW_QSPI_TEST)
    // Bring-up: four quadrants, three seconds.
    pinMode(IO_BL, OUTPUT); digitalWrite(IO_BL, HIGH);
    fillRect(0,   0,   160, 240, 0xF800);
    fillRect(160, 0,   160, 240, 0x07E0);
    fillRect(0,   240, 160, 240, 0x001F);
    fillRect(160, 240, 160, 240, 0xFFFF);
    Serial.println("[axs15231b] TEST: red | green over blue | white, portrait, for 3 s");
    delay(3000);
#endif
    return true;
}

void setWindow(int16_t x0, int16_t y0, int16_t x1, int16_t y1) {
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
    if (first) writeReg(RAM_WRITE, nullptr, 0);
    wait();
    memset(&s_tx, 0, sizeof s_tx);
    s_tx.base.flags     = SPI_TRANS_MODE_QIO | SPI_TRANS_VARIABLE_CMD | SPI_TRANS_VARIABLE_ADDR;
    s_tx.base.cmd       = CMD_4W;
    s_tx.base.addr      = (uint32_t)RAM_CONTINUE << 8;
    s_tx.command_bits   = 8;
    s_tx.address_bits   = 24;
    s_tx.base.tx_buffer = dma;
    s_tx.base.length    = count * 16;
    const esp_err_t r = spi_device_queue_trans(s_dev, &s_tx.base, portMAX_DELAY);
    if (r != ESP_OK) { Serial.printf("[axs15231b] queue failed: %d\n", (int)r); return false; }
    s_inFlight = true;
    return true;
}

void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t rgb565) {
    if (!s_ready || !s_fill || w <= 0 || h <= 0) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > PANEL_W) w = PANEL_W - x;
    if (y + h > PANEL_H) h = PANEL_H - y;
    if (w <= 0 || h <= 0) return;
    const uint16_t c = wire(rgb565);
    for (size_t i = 0; i < FILL_PX; i++) s_fill[i] = c;
    setWindow(x, y, x + w - 1, y + h - 1);
    size_t left = (size_t)w * (size_t)h;
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

}  // namespace Axs15231b
#endif  // JC3248W535
