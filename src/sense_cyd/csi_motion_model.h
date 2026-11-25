// SquachWatch-CYD -- WiFi channel-state motion model, for HUMAN SENSING (SENSE_CYD_APP).
//
// Ported from ESP32Marauder v1.20.0's CsiMotionModel (MIT License, Copyright
// (c) 2020 Just Call Me Koko, https://github.com/justcallmekoko/ESP32Marauder),
// with the window length made a parameter and a per-sample "live" value added
// for the 10 Hz graph. The maths is theirs and is kept recognisable on purpose,
// so a fix upstream can be carried across.
//
// What it does, in words: every CSI sample is one transmitter's view of the
// channel, as 64 complex tones. Per window (1 s by default) four things are
// measured -- how much each tone's amplitude wobbled, how far consecutive
// samples decorrelated, how much the phase slope's residual moved, and how
// much the amplitude shape changed sample to sample. A calibration period
// with the room empty gives each feature a median and a spread (MAD); from
// then on each window's features become z-scores, the median of the four is
// the motion evidence, and that is mapped onto 0-100 so that the calibration
// 85th percentile lands at the MOTION threshold. A furniture-still room scores
// near 0; a person walking through the path scores high.
#pragma once

#include <stdint.h>
#include <stddef.h>

class CsiMotionModel {
 public:
  enum class SampleEncoding : uint8_t {
    SIGNED_12_LE,          // ESP32-C5/C6 forced L-LTF: 16-bit little-endian words, 12 significant bits
    SIGNED_8_IMAG_REAL,    // original ESP32: int8 imaginary, int8 real
  };

  static constexpr size_t kMaxCarriers = 64;
  static constexpr size_t kHistorySize = 160;
  static constexpr uint8_t kPossibleMotionThreshold = 12;
  static constexpr uint8_t kMotionThreshold = 28;

  enum class State : uint8_t {
    CALIBRATING,
    INSUFFICIENT_SAMPLES,
    STILL,
    POSSIBLE_MOTION,
    MOTION,
  };

  void reset(uint32_t now_ms, uint32_t calibration_ms = 30000, uint32_t window_ms = 1000);
  void addSample(const int8_t* bytes, size_t length, int8_t rssi,
                 uint32_t now_ms, bool first_word_invalid = false,
                 SampleEncoding encoding = SampleEncoding::SIGNED_12_LE);
  void tick(uint32_t now_ms, uint32_t accepted, uint32_t dropped);

  bool calibrated() const { return calibrated_; }
  uint8_t calibrationPercent(uint32_t now_ms) const;
  uint8_t motionScore() const { return motion_score_; }
  uint8_t amplitudeScore() const { return amplitude_score_; }
  uint8_t phaseScore() const { return phase_score_; }
  uint8_t correlationScore() const { return correlation_score_; }
  uint8_t reliability() const { return reliability_; }
  uint16_t samplesPerSecond() const { return samples_per_second_; }
  int8_t rssi() const { return last_rssi_; }
  State state() const { return state_; }
  const uint8_t* history() const { return history_; }
  size_t historyHead() const { return history_head_; }
  uint32_t calibrationDurationMs() const { return calibration_ms_; }
  uint32_t windowMs() const { return window_ms_; }
  // Tones in the last sample (carrier_count_ is cleared at every window end).
  size_t carriers() const { return previous_carrier_count_; }
  // Per-sample decorrelation against the previous sample, as a z-score
  // against the calibration (0 before calibration), smoothed. Moves within
  // a few samples of a person moving; the windowed score follows a second
  // later. For the live graph, not for the PRESENCE decision.
  float live() const { return live_; }

 private:
  static constexpr size_t kFeatureCount = 4;
  static constexpr size_t kMaxCalibrationWindows = 32;

  void clearWindow(uint32_t now_ms);
  void finishWindow(uint32_t now_ms);
  void finishCalibration();
  static int16_t decode12(const int8_t* bytes);
  static float robustMedian(float* values, size_t count);
  static float featureStd(float sum, float sum_sq, uint16_t count);

  float previous_real_[kMaxCarriers] = {};
  float previous_imag_[kMaxCarriers] = {};
  float previous_norm_[kMaxCarriers] = {};
  float magnitude_sum_[kMaxCarriers] = {};
  float magnitude_sq_sum_[kMaxCarriers] = {};
  float feature_sum_[3] = {};
  float feature_sq_sum_[3] = {};
  float calibration_features_[kMaxCalibrationWindows][kFeatureCount] = {};
  float feature_median_[kFeatureCount] = {};
  float feature_scale_[kFeatureCount] = {};
  uint8_t history_[kHistorySize] = {};
  size_t carrier_count_ = 0;
  size_t previous_carrier_count_ = 0;
  size_t history_head_ = 0;
  size_t calibration_window_count_ = 0;
  uint32_t started_ms_ = 0;
  uint32_t calibration_ms_ = 30000;
  uint32_t window_ms_ = 1000;
  uint32_t window_started_ms_ = 0;
  uint32_t last_tick_ms_ = 0;
  uint32_t last_history_ms_ = 0;
  uint32_t previous_accepted_ = 0;
  uint32_t previous_dropped_ = 0;
  float rssi_mean_ = -100.0f;
  float rssi_deviation_ = 0.0f;
  float smoothed_score_ = 0.0f;
  float motion_threshold_z_ = 1.0f;
  float live_ = 0.0f;
  float live_median_ = 0.0f;   // calibration median of per-sample decorrelation
  float live_scale_ = 1.0f;
  float live_sum_ = 0.0f;      // per-sample decorrelation, summed over the calibration
  uint32_t live_count_ = 0;
  uint16_t window_samples_ = 0;
  uint16_t pair_samples_ = 0;
  uint16_t samples_per_second_ = 0;
  int8_t last_rssi_ = -127;
  uint8_t motion_score_ = 0;
  uint8_t amplitude_score_ = 0;
  uint8_t phase_score_ = 0;
  uint8_t correlation_score_ = 0;
  uint8_t reliability_ = 0;
  uint8_t motion_votes_ = 0;
  uint8_t still_votes_ = 0;
  bool calibrated_ = false;
  State state_ = State::CALIBRATING;
};
