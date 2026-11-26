// SquachWatch-CYD -- HUMAN SENSING: the chest signal, for the VITALS screen (SENSE_RICH).
//
// From the Tab5 Monster C5 CSI Human Sensing project (same author, same maths),
// given an input for the ESP32/S3's 8-bit L-LTF readings (addSample8).
//
// Breathing moves a chest a few millimetres, which changes the WiFi channel's
// amplitude a little on every tone at once, at the breathing rate. This turns
// one transmitter's CSI samples into two numbers twenty times a second:
//
//   1. Each sample's tone amplitudes, normalised by their total (so the radio's
//      gain steps cancel), are averaged into 50 ms frames.
//   2. Each tone's slow drift (an EMA over ~5 s) is subtracted.
//   3. Every 2 s, over the last 20 s of frames, the two directions across the
//      tones that move the most together (principal components) are found.
//   4. Each frame is projected onto them: c1 (the strongest shared rhythm, the
//      chest when someone sits still) and c2 (the next).
//
// Everything else (filtering, breath peaks, rates, the heart band) happens on
// the screen (src/sense_cyd/sense_vitals.cpp). The window (~100 KB) and the
// covariance (16 KB) come from PSRAM, only while VITALS is up.
#pragma once
#include "sense_cyd.h"
#if SENSE_RICH || defined(SENSE_MODEL_HOST)
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

class CsiVitals {
 public:
  static const int kMaxTones = 64;
  static const int kWindow   = 400;      // frames in a fit: 20 s at 20 a second
  static const uint32_t kFrameMs = 50;   // 20 frames a second
  static const int kRefitFrames = 40;    // refit every 2 s

  ~CsiVitals();
  bool begin();                          // allocate the window; false if out of memory
  void reset();
  // One CSI sample (12-bit I/Q, little-endian words, as the C5 delivers them).
  void addSample(const int8_t* bytes, size_t length, bool firstWordInvalid);
  // One ESP32/S3 L-LTF sample: 64 tones of int8 imaginary, real; the 50 data
  // tones (+-2..26) are used, the nulls and pilots-only edges left out.
  void addSample8(const int8_t* iq, size_t length);
  // True when a frame closed: c1/c2 are its projections, n how many samples
  // went into it (0 = a gap, the last frame repeated), q how much of the
  // movement the first component explains, 0-100.
  bool tick(uint32_t nowMs, float& c1, float& c2, uint8_t& n, uint8_t& q);
  // A frame closed elsewhere (the Tab5's C6 averages its readings into one
  // every 50 ms and sends it): x is T tones' normalised amplitudes. The rest
  // is tick()'s: drift out, the window, a refit every 2 s, the projections.
  bool push(const float* x, int T, float& c1, float& c2, uint8_t& q);
  int tones() const { return tones_; }

 private:
  void fit();
  int tones_ = 0;
  float acc_[kMaxTones] = {}; int accN_ = 0;
  uint32_t frameStart_ = 0; bool started_ = false;
  float last_[kMaxTones] = {}; bool haveLast_ = false;
  float mean_[kMaxTones] = {}; int meanN_ = 0;
  float* win_ = nullptr;                 // kWindow x kMaxTones, oldest overwritten
  float* cov_ = nullptr;                 // kMaxTones x kMaxTones
  int head_ = 0, count_ = 0, sinceFit_ = 0;
  float v1_[kMaxTones] = {}, v2_[kMaxTones] = {};
  bool fitted_ = false;
  uint8_t q_ = 0;
};
#endif
