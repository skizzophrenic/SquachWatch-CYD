// SquachWatch-CM0 -- the ESP32 I2S v2 driver API, the subset the
// firmware's sound.cpp uses. The implementation (src/cm0/i2s_alsa.cpp)
// streams the samples to the Cardputer Zero's ES8389 over ALSA: the
// pin table and the DMA parameters are accepted and ignored, the rate,
// the width and the stereo layout are honoured (16 kHz, 16-bit, left
// right). The sound task is the only caller of start/write/stop;
// install runs before it is created -- no locking inside the shim.
#pragma once
#include <cstdint>
#include <cstddef>

typedef int      esp_err_t;
#define ESP_OK   0
#define ESP_FAIL -1

typedef int i2s_port_t;
#define I2S_NUM_0 0

typedef uint32_t i2s_mode_t;
#define I2S_MODE_MASTER (1u << 0)
#define I2S_MODE_TX     (1u << 1)

#define I2S_BITS_PER_SAMPLE_16BIT   16
#define I2S_BITS_PER_CHAN_16BIT     16
#define I2S_CHANNEL_FMT_RIGHT_LEFT  0
#define I2S_COMM_FORMAT_STAND_I2S   0
#define ESP_INTR_FLAG_LEVEL1        0
#define I2S_MCLK_MULTIPLE_256       256
#define I2S_PIN_NO_CHANGE           (-1)

struct i2s_config_t {
    i2s_mode_t  mode;
    int         sample_rate;
    int         bits_per_sample;
    int         channel_format;
    int         communication_format;
    int         intr_alloc_flags;
    int         dma_buf_count;
    int         dma_buf_len;
    bool        use_apll;
    bool        tx_desc_auto_clear;
    int         fixed_mclk;
    int         mclk_multiple;
    int         bits_per_chan;
};

struct i2s_pin_config_t
{
    int mck_io_num;
    int bck_io_num;
    int ws_io_num;
    int data_out_num;
    int data_in_num;
};

esp_err_t i2s_driver_install(i2s_port_t port, const i2s_config_t* cfg,
                             int queue_size, void* i2s_queue);
esp_err_t i2s_set_pin(i2s_port_t port, const i2s_pin_config_t* pin_cfg);
esp_err_t i2s_start(i2s_port_t port);
esp_err_t i2s_stop(i2s_port_t port);
// size is in BYTES (stereo 16-bit: 4 per frame); *bytes_written receives
// the bytes taken. ticks_to_wait is portMAX_DELAY everywhere sound.cpp
// calls this: the write blocks until the frames are gone.
esp_err_t i2s_write(i2s_port_t port, const void* src, size_t size,
                    size_t* bytes_written, uint32_t ticks_to_wait);
esp_err_t i2s_zero_dma_buffer(i2s_port_t port);
