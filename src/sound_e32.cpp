// SquachWatch E32R28T speaker backend (ESP32-WROOM-32E, ILI9341).
// Built-in DAC2 at GPIO26 -> FM8002E/SC8002 amplifier -> speaker.
// GPIO4 LOW enables the amplifier. DAC2 uses the ESP32 cosine generator.
#include "sound.h"
#if SQW_SPEAKER && defined(E32R28T_AUDIO)

#include <Arduino.h>
#include <atomic>
#include <driver/dac.h>
#include <driver/gpio.h>
#include "settings.h"
#include "clock.h"

namespace Sound {
namespace {

static const gpio_num_t AMP_PIN = GPIO_NUM_4;
struct Note {
    uint16_t fromHz;
    uint16_t toHz;
    uint16_t ms;
    uint16_t restMs;
};
static const Note kNew[] = { {660, 660, 52, 30}, {880, 880, 52, 30}, {1100, 1100, 85, 0} };
static const Note kWatch[] = { {240, 510, 470, 90}, {240, 540, 520, 0} };
static const Note kLow[] = { {530, 280, 230, 100}, {290, 130, 360, 0} };
static const Note kMessage[] = { {1568, 1568, 175, 55}, {2093, 2093, 240, 0} };
static const Note kVisitor[] = { {350, 230, 70, 90}, {330, 200, 80, 0} };
static const Note kThrow[] = { {300, 1500, 280, 0} };
static const Note kLand[] = { {220, 130, 90, 0} };
static const Note kClippy[] = { {420, 630, 90, 20}, {630, 290, 260, 0} };
static const Note kToast[] = { {200, 1800, 120, 0} };
static const Note kPlugIn[] = { {220, 440, 120, 0}, {440, 880, 180, 0} };
static const Note kUnplug[] = { {880, 440, 150, 0}, {440, 220, 200, 0} };
static const Note kLandToaster[] = { {1350, 620, 60, 15}, {620, 620, 350, 0} };
static const Note kLandYeti[] = { {150, 130, 110, 15}, {130, 130, 300, 0} };
static const Note kHeadsUp[] = { {240, 520, 520, 0} };
// Short original melody for the SOUND 14 serial test.
static const Note kMelody[] = {
    {523, 523, 140, 22}, {659, 659, 140, 22},
    {784, 784, 190, 50}, {659, 659, 125, 20},
    {880, 880, 220, 35}, {784, 784, 125, 20},
    {659, 659, 140, 20}, {523, 523, 320, 0}
};

struct Sequence { const Note* notes; uint8_t size; };
#define SEQ(s) {s, (uint8_t)(sizeof(s) / sizeof((s)[0]))}
static const Note kMothmanChirp[] = {{880, 1140, 85, 40}, {1320, 1010, 130, 0}};
static const Note kDacTest[] = {{800, 800, 1500, 0}};
static const Sequence kEvents[] = {
    SEQ(kNew), SEQ(kWatch), SEQ(kLow), SEQ(kMessage), SEQ(kVisitor),
    SEQ(kThrow), SEQ(kLand), SEQ(kClippy), SEQ(kToast), SEQ(kPlugIn),
    SEQ(kUnplug), SEQ(kLandToaster), SEQ(kLandYeti), SEQ(kHeadsUp),
    SEQ(kMelody), SEQ(kDacTest), SEQ(kMothmanChirp)
};
#undef SEQ
static_assert(sizeof(kEvents)/sizeof(kEvents[0]) == (int)Id::COUNT,
              "SquachWatch sound IDs have changed; update the E32 driver");

struct Request {
    Id event;
    dac_cw_scale_t scale;
    uint32_t generation;
};

static QueueHandle_t s_queue = nullptr;
static SemaphoreHandle_t s_control = nullptr;
static std::atomic<uint32_t> s_generation{0};
static std::atomic<bool> s_hush{false};
static bool s_ok = false;
static bool s_ampLow = true;

static void amp(bool on) {
    gpio_set_level(AMP_PIN, on != s_ampLow);
}

static bool current(const Request& request) {
    return s_generation.load(std::memory_order_relaxed) == request.generation;
}

static bool waitMs(const Request& request, uint32_t ms) {
    while (ms && current(request)) {
        const uint32_t step = ms > 20 ? 20 : ms;
        vTaskDelay(pdMS_TO_TICKS(step) + 1);
        ms -= step;
    }
    return current(request);
}

static bool playNote(const Request& request, const Note& note) {
    dac_cw_config_t tone = {};
    tone.en_ch = DAC_CHANNEL_2;
    tone.scale = request.scale;
    tone.phase = DAC_CW_PHASE_0;
    tone.freq = note.fromHz;
    if (dac_cw_generator_config(&tone) != ESP_OK ||
        dac_cw_generator_enable() != ESP_OK) return false;

    if (note.fromHz == note.toHz) {
        waitMs(request, note.ms);
    } else {
        const int32_t sweep = static_cast<int32_t>(note.toHz) - note.fromHz;
        for (uint32_t at = 0; at < note.ms && current(request);) {
            const uint32_t step = note.ms - at > 20 ? 20 : note.ms - at;
            tone.freq = note.fromHz + sweep * static_cast<int32_t>(at) / note.ms;
            if (dac_cw_generator_config(&tone) != ESP_OK) break;
            if (!waitMs(request, step)) break;
            at += step;
        }
    }
    dac_cw_generator_disable();
    dac_output_voltage(DAC_CHANNEL_2, 128);
    return waitMs(request, note.restMs);
}

static void playNow(const Request& request) {
    const Sequence& sequence = kEvents[static_cast<uint8_t>(request.event)];
    xSemaphoreTake(s_control, portMAX_DELAY);
    const bool playing = current(request);
    if (playing) amp(true);
    xSemaphoreGive(s_control);
    if (!playing) return;

    if (request.event == Id::DAC_TEST) Serial.println("[sound/e32] direct DAC test tone");
    waitMs(request, 20);
    for (uint8_t i = 0; i < sequence.size && current(request); ++i) {
        if (!playNote(request, sequence.notes[i])) break;
    }

    dac_cw_generator_disable();
    dac_output_voltage(DAC_CHANNEL_2, 128);
    xSemaphoreTake(s_control, portMAX_DELAY);
    amp(false);
    xSemaphoreGive(s_control);
}

static void audioTask(void*) {
    Request request;
    for (;;) {
        if (xQueueReceive(s_queue, &request, portMAX_DELAY) == pdTRUE)
            playNow(request);
    }
}

} // namespace

void begin() {
    if (s_ok) return;
    gpio_set_level(AMP_PIN, 1);
    gpio_config_t ampConfig = {};
    ampConfig.pin_bit_mask = 1ULL << AMP_PIN;
    ampConfig.mode = GPIO_MODE_OUTPUT;
    if (gpio_config(&ampConfig) != ESP_OK) {
        Serial.println("[sound/e32] cannot configure amplifier shutdown");
        return;
    }

    if (dac_output_enable(DAC_CHANNEL_2) != ESP_OK ||
        dac_output_voltage(DAC_CHANNEL_2, 128) != ESP_OK) {
        Serial.println("[sound/e32] cannot configure DAC2 on GPIO26");
        dac_output_disable(DAC_CHANNEL_2);
        return;
    }

    s_queue = xQueueCreate(4, sizeof(Request));
    s_control = xSemaphoreCreateMutex();
    if (!s_queue || !s_control ||
        xTaskCreatePinnedToCore(audioTask, "sqw-e32-audio", 3072, nullptr, 2, nullptr, 1) != pdPASS) {
        if (s_queue) vQueueDelete(s_queue);
        if (s_control) vSemaphoreDelete(s_control);
        s_queue = nullptr;
        s_control = nullptr;
        dac_output_disable(DAC_CHANNEL_2);
        Serial.println("[sound/e32] cannot start playback task");
        return;
    }
    s_ok = true;
    Serial.println("[sound/e32] DAC26 AMP4 (LOW=on), ready=1");
}

void play(Id id, bool sample) {
    if (!s_ok || id >= Id::COUNT) return;
    if (!sample) {
        const uint8_t mode = Settings::soundMode();
        const bool alert = id == Id::NEW_DEVICE || id == Id::WATCH_HIT ||
                           id == Id::LOW_BATTERY || id == Id::HEADS_UP;
        if (mode == 0 || (!alert && mode < 2)) return;
        if (s_hush.load(std::memory_order_relaxed) || (Clock::night() && id != Id::WATCH_HIT)) return;
    }

    const uint8_t volume = Settings::soundVolume();
    if ((volume == 0 && id != Id::DAC_TEST) || xSemaphoreTake(s_control, 0) != pdTRUE) return;
    // The hardware oscillator has four output levels. Use the verified
    // half-scale level for the bench test regardless of the saved volume.
    const dac_cw_scale_t levels[] = {DAC_CW_SCALE_8, DAC_CW_SCALE_4,
                                     DAC_CW_SCALE_2, DAC_CW_SCALE_1};
    const auto scale = id == Id::DAC_TEST ? DAC_CW_SCALE_2 : levels[(volume - 1) / 25];
    const Request request{id, scale, s_generation.load(std::memory_order_relaxed)};
    xQueueSend(s_queue, &request, 0);
    xSemaphoreGive(s_control);
}

void hush(bool on) {
    s_hush.store(on, std::memory_order_relaxed);
}

// Invalidate even a request the worker has already taken off the queue.
static void cancelPlayback() {
    s_generation.store(s_generation.load(std::memory_order_relaxed) + 1,
                       std::memory_order_relaxed);
    amp(false);
    xQueueReset(s_queue);
}

void quiet() {
    if (!s_ok) return;
    xSemaphoreTake(s_control, portMAX_DELAY);
    cancelPlayback();
    xSemaphoreGive(s_control);
}

bool present() {
    return s_ok;
}

void ampLow(bool activeLow) {
    if (!s_ok) return;
    xSemaphoreTake(s_control, portMAX_DELAY);
    s_ampLow = activeLow;
    cancelPlayback();
    xSemaphoreGive(s_control);
}

} // namespace Sound
#endif
