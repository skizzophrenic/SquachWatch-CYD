// Tab5 Monster C5 CSI Human Sensing -- the sensing radio's chest signal. See csi_vitals.h.
#if defined(SENSE_C5) || defined(SENSE_C6) || defined(SENSE_MODEL_HOST)
#include "csi_vitals.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#if defined(SENSE_C5) || defined(SENSE_C6)
#include <esp_heap_caps.h>
static void* bigAlloc(size_t n) {
  void* p = heap_caps_calloc(1, n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  return p ? p : calloc(1, n);
}
#else
static void* bigAlloc(size_t n) { return calloc(1, n); }
#endif

static int16_t decode12(const int8_t* b) {
  uint16_t w = (uint8_t)b[0] | ((uint16_t)(uint8_t)b[1] << 8);
  if (w & 0x0800) w |= 0xF000;
  return (int16_t)w;
}

bool CsiVitals::begin() {
  if (!cov_) cov_ = (float*)bigAlloc(sizeof(float) * kMaxTones * kMaxTones);
  if (!win_ && cov_) {
    static const int sizes[3] = { kWindow, 300, 200 };
    for (int s : sizes) {
      const size_t need = sizeof(float) * (size_t)s * kMaxTones;
#if defined(SENSE_C5) || defined(SENSE_C6)
      if (heap_caps_get_free_size(MALLOC_CAP_8BIT) < need + kKeepFree) continue;
#endif
      win_ = (float*)bigAlloc(need);
      if (win_) { window_ = s; break; }
    }
  }
  reset();
  return win_ && cov_;
}

void CsiVitals::reset() {
  tones_ = 0; accN_ = 0; started_ = false; haveLast_ = false; meanN_ = 0;
  head_ = count_ = sinceFit_ = 0; fitted_ = false; q_ = 0;
  memset(acc_, 0, sizeof acc_); memset(mean_, 0, sizeof mean_);
  memset(v1_, 0, sizeof v1_); memset(v2_, 0, sizeof v2_);
}

void CsiVitals::addSample(const int8_t* bytes, size_t length, bool firstWordInvalid, bool int8) {
  if (!bytes || length < 16) return;
  if (int8) {
    const size_t first = firstWordInvalid ? 2 : 0;          // four bytes = two tones
    const int m = (int)(length / 2) - (int)first;
    if (m < 8) return;
    const int r = (m + kMaxTones - 1) / kMaxTones;          // tones per bin
    const int n = m / r;
    if (tones_ && n != tones_) { reset(); }
    tones_ = n;
    float mag[kMaxTones], total = 0;
    for (int i = 0; i < n; i++) {
      float a = 0;
      for (int k = 0; k < r; k++) {
        const int8_t* c = bytes + (first + (size_t)(i * r + k)) * 2;
        const float im = c[0], re = c[1];
        a += sqrtf(re * re + im * im);
      }
      mag[i] = a / r; total += mag[i];
    }
    if (total < 1.0f) return;
    const float scale = (float)n / total;
    for (int i = 0; i < n; i++) acc_[i] += mag[i] * scale;
    accN_++;
    return;
  }
  size_t scalars = (length / 2) & ~(size_t)1;
  const size_t first = firstWordInvalid ? 2 : 0;
  if (scalars <= first + 3) return;
  int n = (int)((scalars - first) / 2);
  if (n > kMaxTones) n = kMaxTones;
  if (tones_ && n != tones_) { reset(); }   // a different layout: start over
  tones_ = n;
  float mag[kMaxTones], total = 0;
  for (int i = 0; i < n; i++) {
    const size_t s = first + (size_t)i * 2;
    const float re = decode12(bytes + s * 2), im = decode12(bytes + (s + 1) * 2);
    mag[i] = sqrtf(re * re + im * im); total += mag[i];
  }
  if (total < 1.0f) return;
  const float scale = (float)n / total;
  for (int i = 0; i < n; i++) acc_[i] += mag[i] * scale;
  accN_++;
}

bool CsiVitals::frame(uint32_t now, float* x, int& T, uint8_t& n) {
  if (!started_) { if (!accN_) return false; started_ = true; frameStart_ = now; return false; }
  if (now - frameStart_ < kFrameMs) return false;
  // a stall longer than a frame closes one frame, not a burst of them
  frameStart_ = now - ((now - frameStart_) % kFrameMs);
  T = tones_;
  if (!T) return false;
  n = (uint8_t)(accN_ > 255 ? 255 : accN_);
  if (accN_) { for (int i = 0; i < T; i++) x[i] = acc_[i] / accN_; memcpy(last_, x, sizeof(float) * T); haveLast_ = true; }
  else if (haveLast_) memcpy(x, last_, sizeof(float) * T);
  else return false;
  memset(acc_, 0, sizeof acc_); accN_ = 0;
  return true;
}

bool CsiVitals::tick(uint32_t now, float& c1, float& c2, uint8_t& n, uint8_t& q) {
  if (!win_ || !cov_) return false;
  float x[kMaxTones]; int T = 0;
  if (!frame(now, x, T, n)) return false;
  // slow drift out: ~5 s EMA per tone (a plain mean until it has 100 frames)
  meanN_ = meanN_ < 100 ? meanN_ + 1 : 100;
  const float a = 1.0f / meanN_;
  float* row = win_ + (size_t)head_ * kMaxTones;
  for (int i = 0; i < T; i++) { mean_[i] += (x[i] - mean_[i]) * a; row[i] = x[i] - mean_[i]; }
  head_ = (head_ + 1) % window_; if (count_ < window_) count_++;
  if (++sinceFit_ >= kRefitFrames && count_ >= 100) { sinceFit_ = 0; fit(); }
  c1 = c2 = 0;
  if (fitted_) for (int i = 0; i < T; i++) { c1 += row[i] * v1_[i]; c2 += row[i] * v2_[i]; }
  q = q_;
  return true;
}

// The two strongest directions of the last 20 s, by power iteration on the
// tone-by-tone covariance. Each starts from the last fit (it changes slowly),
// and keeps the last fit's sign, so the signal does not flip at a refit.
void CsiVitals::fit() {
  const int T = tones_;
  float* C = cov_;
  // Single precision: the C5 has a float unit but does doubles in software,
  // and in double this took ~425 ms, a gap in the chest signal every 2.35 s
  // (bench, 2026-11-08). Row by row, so each frame is read once.
  for (int i = 0; i < T; i++) for (int j = i; j < T; j++) C[i * kMaxTones + j] = 0;
  for (int k = 0; k < count_; k++) {
    const float* r = win_ + (size_t)k * kMaxTones;
    for (int i = 0; i < T; i++) { const float ri = r[i]; float* Ci = C + i * kMaxTones; for (int j = i; j < T; j++) Ci[j] += ri * r[j]; }
  }
  const float inv = 1.0f / count_;
  for (int i = 0; i < T; i++) for (int j = i; j < T; j++) C[j * kMaxTones + i] = (C[i * kMaxTones + j] *= inv);
  float trace = 0; for (int i = 0; i < T; i++) trace += C[i * kMaxTones + i];
  if (trace <= 1e-12f) return;
  auto power = [&](float* v, const float* prev, bool havePrev) {
    float w[kMaxTones];
    for (int i = 0; i < T; i++) v[i] = havePrev ? prev[i] : 1.0f / sqrtf((float)T) * (i % 2 ? -1 : 1) + 0.01f * i;
    float lambda = 0;
    for (int it = 0; it < 30; it++) {
      float nrm = 0;
      for (int i = 0; i < T; i++) { float s = 0; for (int j = 0; j < T; j++) s += C[i * kMaxTones + j] * v[j]; w[i] = s; nrm += s * s; }
      nrm = sqrtf(nrm); if (nrm < 1e-20f) return 0.0f;
      for (int i = 0; i < T; i++) v[i] = w[i] / nrm;
      lambda = nrm;
    }
    if (havePrev) { float d = 0; for (int i = 0; i < T; i++) d += v[i] * prev[i]; if (d < 0) for (int i = 0; i < T; i++) v[i] = -v[i]; }
    return lambda;
  };
  float n1[kMaxTones], n2[kMaxTones];
  const float l1 = power(n1, v1_, fitted_);
  for (int i = 0; i < T; i++) for (int j = 0; j < T; j++) C[i * kMaxTones + j] -= l1 * n1[i] * n1[j];
  power(n2, v2_, fitted_);
  memcpy(v1_, n1, sizeof(float) * T); memcpy(v2_, n2, sizeof(float) * T);
  q_ = (uint8_t)(l1 / trace * 100.0f > 100 ? 100 : l1 / trace * 100.0f);
  fitted_ = true;
}
#endif
