// Tab5 Monster C5 CSI Human Sensing -- the sensing radio's chest signal, for the VITALS screen.
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
// the screen, where there is time and memory to spare. Plain C++: the host
// test (test/host_vitals) builds it with SENSE_MODEL_HOST.
#pragma once
#if defined(SENSE_C5) || defined(SENSE_C6) || defined(SENSE_MODEL_HOST)
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

class CsiVitals {
 public:
  static const int kMaxTones = 64;
  static const int kWindow   = 400;      // frames in a fit at most: 20 s at 20 a second
  // Fewer when memory is short: in SquachWatch the C6 also runs the Bluetooth
  // controller and had ~106 KB free, under the 118 KB a full window takes
  // (bench 2026-11-17). 300 (15 s) or 200 (10 s) frames, leaving kKeepFree.
  static const size_t kKeepFree = 32 * 1024;
  int window() const { return window_; }
  static const uint32_t kFrameMs = 50;   // 20 frames a second
  static const int kRefitFrames = 40;    // refit every 2 s

  ~CsiVitals() { free(win_); free(cov_); }   // the window goes back (the C6 lends it)
  bool begin();                          // allocate the window; false if out of memory
  void reset();
  // One CSI sample: 12-bit I/Q in little-endian words, as the C5 delivers
  // them, or (int8) one signed byte each, imaginary then real, as the C6
  // does (bench dump 2026-11-17: the C6's bytes are NOT 12-bit words; read
  // as words they were scrambled). More than kMaxTones tones (the 256-byte
  // layout) are averaged in neighbouring pairs down to kMaxTones.
  void addSample(const int8_t* bytes, size_t length, bool firstWordInvalid, bool int8 = false);
  // True when a frame closed: c1/c2 are its projections, n how many samples
  // went into it (0 = a gap, the last frame repeated), q how much of the
  // movement the first component explains, 0-100.
  bool tick(uint32_t nowMs, float& c1, float& c2, uint8_t& n, uint8_t& q);
  // FRAMES mode (`vitals frames`): only the frame, every 50 ms -- each
  // tone's amplitude, normalised and averaged -- for the tablet to fit
  // (SquachWatch's Tab5 runs the 20 s fit on its P4). Needs no begin(): no
  // window here. x has room for kMaxTones.
  bool frame(uint32_t nowMs, float* x, int& T, uint8_t& n);
  int tones() const { return tones_; }

 private:
  void fit();
  int tones_ = 0;
  float acc_[kMaxTones] = {}; int accN_ = 0;
  uint32_t frameStart_ = 0; bool started_ = false;
  float last_[kMaxTones] = {}; bool haveLast_ = false;
  float mean_[kMaxTones] = {}; int meanN_ = 0;
  float* win_ = nullptr;                 // window_ x kMaxTones, oldest overwritten
  int window_ = 0;
  float* cov_ = nullptr;                 // kMaxTones x kMaxTones
  int head_ = 0, count_ = 0, sinceFit_ = 0;
  float v1_[kMaxTones] = {}, v2_[kMaxTones] = {};
  bool fitted_ = false;
  uint8_t q_ = 0;
};
#endif
