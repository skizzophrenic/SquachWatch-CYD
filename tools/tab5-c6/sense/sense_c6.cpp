// Tab5 Monster C5 CSI Human Sensing -- the sensing radio on the Tab5's OWN radio chip, the ESP32-C6.
//
// The same job as sense_c5.cpp (one motion model per transmitter, fused,
// ten lines a second), moved onto the Tab5's onboard ESP32-C6, so the
// sensing screens work with no Monster C5 on the bus. The C6 runs
// Espressif's ESP-Hosted slave (the P4 reaches WiFi and Bluetooth through
// it, over SDIO), and this file rides on that slave the way SquachWatch's
// squach_sniff.c does: it registers one custom-message handler and does
// nothing at all until the tablet sends a command. Booting is identical to
// the stock image, on purpose: a crash at boot would brick the radio (the
// C6 has no rollback).
//
// Lines and commands are the ones in include/sense_proto.h, byte for byte:
// each line goes to the tablet as one custom-message event (SENSE_EVT_LINE,
// without its newline), each command arrives as one (SENSE_CMD_TEXT). The
// tablet feeds the lines into the same parser it uses for the Monster's
// UART, so the screens cannot tell the two radios apart.
//
// What is different from the C5, and why:
//   - 2.4 GHz only (the C6 has no 5 GHz radio), so `park` takes 1-14.
//   - No `join`: the C6's station is the tablet's, driven from the P4 over
//     ESP-Hosted. Sensing is PARKED (promiscuous on one channel, the
//     busiest transmitters modelled) or, once the tablet has joined a
//     network and pings its router, STATION (`station <bssid> <chan>`):
//     the router is held as the first target whatever else is busy, the
//     channel is the network's (never changed here: that would drop the
//     tablet's connection), and the three other slots go to the busiest
//     other transmitters, as before.
//   - The C6 cannot force the L-LTF shape for every modulation (that is a
//     C5-generation radio feature), so a transmitter's samples can come in
//     more than one tone layout. Each target locks onto one layout (the
//     one it sends most) and the others are counted, not modelled.
//   - No FPU on the C6 (the C5 has one): the maths is the same, in
//     software floats, at 160 MHz instead of 240. `rates` says what it costs.
//   - Nothing is saved on the C6: the tablet re-sends its settings on every
//     link, and a radio that forgets is one that cannot boot into a bad state.
//
// The model is ESP32Marauder's CsiMotionModel (MIT License, Copyright (c)
// 2020 Just Call Me Koko, https://github.com/justcallmekoko/ESP32Marauder),
// see csi_motion_model.h.
#if defined(SENSE_C6)
extern "C" {
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_timer.h"
#include "esp_mac.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "esp_hosted_peer_data.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
}
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <math.h>
#include <new>
#include "sense_proto.h"
#include "csi_motion_model.h"
#include "csi_vitals.h"

#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "dev"
#endif

static const char* TAG = "sense";

// ---- Tunables (the C5's, trimmed for 512 KB of RAM) --------------------------
static const uint8_t  kQueueSize   = 32;     // CSI samples waiting for the task (the C5 keeps 64)
static const uint16_t kMaxCsiBytes = 256;    // 64 tones x 2 x 16 bit
static const uint8_t  kMaxTargets  = 4;
static const uint8_t  kSourceSlots = 16;
static const uint32_t kRankMs      = 2000;
static const uint32_t kEvictMs     = 10000;
static const uint16_t kMinRatePerS = 2;
static const uint32_t kInfoMs      = 60000;
static const uint32_t kBenchMs     = 10000;  // the '# c6' health line
static const uint8_t  kLayouts     = 4;      // tone layouts counted per target
// At most one reading per target every 20 ms (50 a second): the model needs
// no more, and two SquachWatch boards pinging the same router put ~300 a
// second on the channel, more than this radio's soft-float maths can take.
static const uint32_t kMinGapMs    = 20;

// ---- CSI sample queue (filled in the WiFi task, drained in our task) ---------
struct Sample {
  uint16_t length;
  int8_t   rssi;
  bool     first_word_invalid;
  uint8_t  mac[6];
  uint8_t  channel;
  uint32_t at;
  int8_t   iq[kMaxCsiBytes];
};
static Sample            s_queue[kQueueSize];
static volatile uint8_t  s_wr = 0, s_rd = 0;
static volatile uint32_t s_accepted = 0, s_dropped = 0, s_seen = 0;

struct Source { uint8_t mac[6]; volatile uint16_t frames; uint32_t lastAt; };
static Source s_sources[kSourceSlots];

// ---- Targets -----------------------------------------------------------------
struct Target {
  bool     used;
  uint8_t  mac[6];
  uint32_t accepted, dropped, lastAt;
  uint32_t queuedAt;   // the callback's last queued reading (kMinGapMs)
  // The tone layouts this transmitter's samples arrive in (by CSI length),
  // and the one the model is fed. The C6 cannot force one layout.
  uint16_t layoutLen[kLayouts];
  uint32_t layoutN[kLayouts];
  uint16_t lockedLen;
  uint32_t otherLayout;
  CsiMotionModel model;
};
static Target s_targets[kMaxTargets];
// STATION: the router the tablet joined, held in a target slot for good.
static bool    s_havePin = false;
static uint8_t s_pin[6];
static bool isPinned(const uint8_t* mac) { return s_havePin && memcmp(mac, s_pin, 6) == 0; }

// ---- State -------------------------------------------------------------------
static uint8_t  s_mode = SENSE_MODE_OFF;
static uint16_t s_chan = 6;
static uint32_t s_windowMs = 1000, s_calMs = 30000, s_holdMs = 15000;
static uint32_t s_lastMotionMs = 0;
static bool     s_presence = false;
static uint8_t  s_raw = 0;
static bool     s_csiOn = false, s_promisc = false;
static uint32_t s_startedMs = 0, s_lastLineMs = 0, s_lastRankMs = 0, s_lastInfoMs = 0, s_lastFusionMs = 0, s_lastBenchMs = 0;

// VITALS: park-and-follow only on this radio (no network to ping).
static bool      s_vitals = false;
static bool      s_vitFramesOnly = false;   // `vitals frames`: send the frames, the tablet fits
static CsiVitals* s_vit = nullptr;
static int       s_vitTarget = -1;
static uint8_t   s_vitMac[6];
static uint32_t  s_vitFrames = 0;
static uint32_t  s_fitUsLast = 0, s_fitUsMax = 0;

// Fused output.
static uint8_t  s_score = 0, s_amp = 0, s_phase = 0, s_corr = 0, s_rel = 0, s_state = SENSE_STATE_CAL;
static uint16_t s_rate = 0, s_live = 0;
static int8_t   s_rssi = -127;
static uint8_t  s_motionVotes = 0, s_stillVotes = 0;

// Sends that the link refused (a refusal is a lost line), for the health line.
static uint32_t s_txFail = 0; static int s_txErr = 0;
// Bench: how busy this task is (work time over wall time, per health line).
static uint64_t s_busyUs = 0;
static uint32_t s_wallUs0 = 0;

// Commands from the tablet, handed from the RPC task to ours.
struct Cmd { char text[SENSE_CMD_MAX]; };
static QueueHandle_t s_cmdQ;

static uint32_t now_ms() { return (uint32_t)(esp_timer_get_time() / 1000); }

// Shared with squach_sniff.c: each side turns promiscuous off only if the
// other is not using it.
extern "C" bool squach_sniff_on(void);
extern "C" bool squach_sense_on(void) { return s_promisc; }

// ---- Lines out: one custom-message event per line --------------------------
static void outLine(const char* s) {
  size_t n = strlen(s);
  while (n && (s[n - 1] == '\n' || s[n - 1] == '\r')) n--;
  if (!n) return;
  const esp_err_t e = esp_hosted_send_custom_data(SENSE_EVT_LINE, (const uint8_t*)s, n);
  if (e != ESP_OK) { s_txFail++; s_txErr = (int)e; }
}
static void outf(const char* fmt, ...) {
  char b[SENSE_LINE_MAX + 64];
  va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
  outLine(b);
}
static void note(const char* fmt, ...) {
  char b[200];
  va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
  outf("# %s", b);
  ESP_LOGI(TAG, "%s", b);
}

// ---- The CSI callback: decide fast, copy, go ---------------------------------
static void IRAM_ATTR csiCb(void* ctx, wifi_csi_info_t* info) {
  (void)ctx;
  if (!info || !info->buf || info->len < 16) return;
  s_seen++;
  const uint32_t now = now_ms();
  int freeSlot = -1, oldest = 0;
  bool counted = false;
  for (int i = 0; i < kSourceSlots; i++) {
    if (s_sources[i].lastAt && memcmp(s_sources[i].mac, info->mac, 6) == 0) {
      if (s_sources[i].frames < 0xFFFF) s_sources[i].frames++;
      s_sources[i].lastAt = now; counted = true; break;
    }
    if (!s_sources[i].lastAt) { if (freeSlot < 0) freeSlot = i; }
    else if (s_sources[i].lastAt < s_sources[oldest].lastAt) oldest = i;
  }
  if (!counted) {
    const int slot = freeSlot >= 0 ? freeSlot : oldest;
    memcpy(s_sources[slot].mac, info->mac, 6);
    s_sources[slot].frames = 1; s_sources[slot].lastAt = now;
  }
  int want = -1;
  for (int i = 0; i < kMaxTargets; i++)
    if (s_targets[i].used && memcmp(s_targets[i].mac, info->mac, 6) == 0) { want = i; break; }
  if (want < 0) return;
  // The router the tablet pings is exempt: its replies are the stream we
  // asked for, ~50 a second, and with its beacons in between the cap threw
  // a third of them away (31/s modelled of 50 pings, bench 2026-11-17).
  if (!isPinned(s_targets[want].mac) && now - s_targets[want].queuedAt < kMinGapMs) return;   // counted above, not modelled
  s_targets[want].queuedAt = now;
  const uint8_t next = (uint8_t)((s_wr + 1) % kQueueSize);
  if (next == s_rd) { s_dropped++; return; }
  Sample& s = s_queue[s_wr];
  s.length = info->len > kMaxCsiBytes ? kMaxCsiBytes : info->len;
  s.rssi = info->rx_ctrl.rssi;
  s.first_word_invalid = info->first_word_invalid;
  s.channel = info->rx_ctrl.channel;
  s.at = now;
  memcpy(s.mac, info->mac, 6);
  memcpy(s.iq, info->buf, s.length);
  s_wr = next;
  s_accepted++;
}

// ---- Radio -------------------------------------------------------------------
static bool csiStart() {
  // The C6's radio is 20 MHz wide and cannot force the L-LTF shape, and
  // esp_wifi_set_csi_config refuses a set it cannot do (ESP_FAIL on the
  // C5's full set, bench 2026-11-17). Widest set first, narrower until one
  // takes; the health line says which.
  struct Try { const char* name; uint8_t ht20, ht40, su, mu, dcm, bf; } tries[] = {
    { "legacy+ht20+ht40+he",      1, 1, 1, 1, 1, 1 },
    { "legacy+ht20+he",           1, 0, 1, 1, 1, 1 },
    { "legacy+ht20+he-su",        1, 0, 1, 0, 0, 0 },
    { "legacy+ht20",              1, 0, 0, 0, 0, 0 },
    { "legacy",                   0, 0, 0, 0, 0, 0 },
  };
  esp_err_t e;
  if ((e = esp_wifi_set_csi_rx_cb(csiCb, nullptr)) != ESP_OK) { note("csi cb: %s", esp_err_to_name(e)); return false; }
  const char* took = nullptr;
  for (const Try& t : tries) {
    wifi_csi_config_t config = {};
    config.enable = 1;
    config.acquire_csi_legacy = 1;
    config.acquire_csi_ht20 = t.ht20;
    config.acquire_csi_ht40 = t.ht40;
    config.acquire_csi_su = t.su;
    config.acquire_csi_mu = t.mu;
    config.acquire_csi_dcm = t.dcm;
    config.acquire_csi_beamformed = t.bf;
    config.acquire_csi_he_stbc = ESP_CSI_ACQUIRE_STBC_HELTF1;
    config.val_scale_cfg = 0;
    config.dump_ack_en = 0;
    e = esp_wifi_set_csi_config(&config);
    if (e == ESP_OK) { took = t.name; break; }
    note("csi config %s: %s", t.name, esp_err_to_name(e));
  }
  if (!took) { note("csi config: nothing the C6 accepts"); return false; }
  if ((e = esp_wifi_set_csi(true)) != ESP_OK) { note("csi on: %s", esp_err_to_name(e)); return false; }
  s_csiOn = true;
  note("csi on: %s", took);
  return true;
}

static void csiStop() {
  if (!s_csiOn) return;
  esp_wifi_set_csi(false);
  esp_wifi_set_csi_rx_cb(nullptr, nullptr);
  s_csiOn = false;
}

static void promiscOff() {
  s_promisc = false;
  if (!squach_sniff_on()) esp_wifi_set_promiscuous(false);
}

static void clearTargets() {
  for (int i = 0; i < kMaxTargets; i++) s_targets[i].used = false;
  memset(s_sources, 0, sizeof s_sources);
  s_wr = s_rd = 0; s_accepted = s_dropped = s_seen = 0;
  s_score = s_amp = s_phase = s_corr = s_rel = 0; s_rate = s_live = 0; s_rssi = -127;
  s_motionVotes = s_stillVotes = 0; s_state = SENSE_STATE_CAL;
  s_startedMs = now_ms(); s_lastFusionMs = s_startedMs;
}

static void stopAll() {
  csiStop();
  promiscOff();
  clearTargets();
  s_havePin = false;
  s_mode = SENSE_MODE_OFF;
  s_presence = false;
}

static bool startPark(uint16_t chan) {
  if (chan < 1 || chan > 14) { note("park: channel 1-14 on this radio (2.4 GHz only)"); return false; }
  csiStop();
  clearTargets();
  s_havePin = false;
  // The WiFi driver is the tablet's: it must have started it over
  // ESP-Hosted (esp_wifi_init + start on the P4) before this can work.
  esp_err_t e = esp_wifi_set_channel((uint8_t)chan, WIFI_SECOND_CHAN_NONE);
  if (e != ESP_OK) {
    note("channel %u refused: %s%s", chan, esp_err_to_name(e),
         (e == ESP_ERR_WIFI_NOT_INIT || e == ESP_ERR_WIFI_NOT_STARTED) ? " (the tablet has not started the WiFi driver)" : "");
    return false;
  }
  s_chan = chan;
  wifi_promiscuous_filter_t f = { .filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_DATA };
  esp_wifi_set_promiscuous_filter(&f);
  if ((e = esp_wifi_set_promiscuous(true)) != ESP_OK) { note("promiscuous: %s", esp_err_to_name(e)); return false; }
  s_promisc = true;
  if (!csiStart()) return false;
  s_mode = SENSE_MODE_PARK;
  note("park: channel %u (2.4 GHz), modelling the %u busiest transmitters", chan, kMaxTargets);
  return true;
}

// The tablet has joined a network (over ESP-Hosted) and pings its router:
// the router's replies are the steady stream sensing wants. The channel is
// the network's already; promiscuous adds the other transmitters, as parked.
static bool startStation(const uint8_t* bssid, uint16_t chan) {
  csiStop();
  clearTargets();
  s_chan = chan;
  memcpy(s_pin, bssid, 6); s_havePin = true;
  wifi_promiscuous_filter_t f = { .filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_DATA };
  esp_wifi_set_promiscuous_filter(&f);
  esp_err_t e = esp_wifi_set_promiscuous(true);
  if (e != ESP_OK) note("station: promiscuous %s (the router alone, then)", esp_err_to_name(e));
  else s_promisc = true;
  if (!csiStart()) return false;
  Target& t = s_targets[0];
  t.used = true; memcpy(t.mac, bssid, 6); t.accepted = t.dropped = 0; t.lastAt = now_ms();
  memset(t.layoutLen, 0, sizeof t.layoutLen); memset(t.layoutN, 0, sizeof t.layoutN);
  t.lockedLen = 0; t.otherLayout = 0;
  t.model.reset(now_ms(), s_calMs, s_windowMs);
  s_mode = SENSE_MODE_STA;
  note("station: the router %02X:%02X:%02X:%02X:%02X:%02X on channel %u, and the 3 busiest others", bssid[0], bssid[1], bssid[2],
       bssid[3], bssid[4], bssid[5], chan);
  return true;
}

// ---- Targets: pick the busiest transmitters ----------------------------------
static int findTarget(const uint8_t* mac) {
  for (int i = 0; i < kMaxTargets; i++)
    if (s_targets[i].used && memcmp(s_targets[i].mac, mac, 6) == 0) return i;
  return -1;
}

static void rankSources(uint32_t now) {
  for (int i = 0; i < kMaxTargets; i++)
    if (s_targets[i].used && !isPinned(s_targets[i].mac) && now - s_targets[i].lastAt > kEvictMs) {
      note("target %02X:%02X:%02X:%02X:%02X:%02X went quiet, dropped", s_targets[i].mac[0], s_targets[i].mac[1],
           s_targets[i].mac[2], s_targets[i].mac[3], s_targets[i].mac[4], s_targets[i].mac[5]);
      s_targets[i].used = false;
    }
  struct Cand { uint8_t mac[6]; uint16_t frames; } cand[kSourceSlots];
  int n = 0;
  for (int i = 0; i < kSourceSlots; i++) {
    const uint16_t f = s_sources[i].frames;
    s_sources[i].frames = 0;
    if (!s_sources[i].lastAt || f == 0) continue;
    if (now - s_sources[i].lastAt > kEvictMs) { s_sources[i].lastAt = 0; continue; }
    memcpy(cand[n].mac, s_sources[i].mac, 6); cand[n].frames = f; n++;
  }
  for (int i = 1; i < n; i++) { Cand c = cand[i]; int j = i; while (j && cand[j - 1].frames < c.frames) { cand[j] = cand[j - 1]; j--; } cand[j] = c; }
  const uint16_t minFrames = (uint16_t)(kMinRatePerS * kRankMs / 1000);
  for (int i = 0; i < n; i++) {
    if (cand[i].frames < minFrames) break;
    if (findTarget(cand[i].mac) >= 0) continue;
    int slot = -1;
    for (int k = 0; k < kMaxTargets; k++) if (!s_targets[k].used) { slot = k; break; }
    if (slot < 0) break;
    Target& t = s_targets[slot];
    t.used = true; memcpy(t.mac, cand[i].mac, 6); t.accepted = t.dropped = 0; t.lastAt = now;
    memset(t.layoutLen, 0, sizeof t.layoutLen); memset(t.layoutN, 0, sizeof t.layoutN);
    t.lockedLen = 0; t.otherLayout = 0;
    t.model.reset(now, s_calMs, s_windowMs);
    note("target %02X:%02X:%02X:%02X:%02X:%02X: %u frames in %lu ms, calibrating %lu s", t.mac[0], t.mac[1], t.mac[2],
         t.mac[3], t.mac[4], t.mac[5], cand[i].frames, (unsigned long)kRankMs, (unsigned long)(s_calMs / 1000));
  }
  // The layout lock. Counts decay by a quarter each ranking period (a
  // half-life of about five seconds). A target locks onto the first layout
  // it sends a few of; it switches only when another layout outnumbers the
  // locked one three to one (a change restarts its calibration): on the
  // bench one transmitter sent 128- and 256-byte samples in alternating
  // two-second bursts and flipped every period (2026-11-17).
  for (int i = 0; i < kMaxTargets; i++) {
    Target& t = s_targets[i];
    if (!t.used) continue;
    int best = -1, locked = -1;
    for (int k = 0; k < kLayouts; k++) {
      if (!t.layoutLen[k]) continue;
      if (t.layoutLen[k] == t.lockedLen) locked = k;
      if (best < 0 || t.layoutN[k] > t.layoutN[best]) best = k;
    }
    const uint32_t lockedN = locked >= 0 ? t.layoutN[locked] : 0;
    if (best >= 0 && t.layoutLen[best] != t.lockedLen && t.layoutN[best] >= 8 && t.layoutN[best] >= 3 * lockedN) {
      if (t.lockedLen) note("target %02X%02X: tone layout %u -> %u bytes (%lu vs %lu), calibrating again", t.mac[4], t.mac[5],
                            t.lockedLen, t.layoutLen[best], (unsigned long)t.layoutN[best], (unsigned long)lockedN);
      t.lockedLen = t.layoutLen[best];
      t.model.reset(now, s_calMs, s_windowMs);
      t.accepted = t.dropped = 0;
    }
    for (int k = 0; k < kLayouts; k++) t.layoutN[k] -= t.layoutN[k] / 4;
  }
}

// Which layout bucket a sample's length falls in (adds one if room).
static int layoutBucket(Target& t, uint16_t len) {
  for (int k = 0; k < kLayouts; k++) {
    if (t.layoutLen[k] == len) return k;
    if (!t.layoutLen[k]) { t.layoutLen[k] = len; return k; }
  }
  return -1;
}

// ---- Fusion: ESP32Marauder's, over the calibrated, trusted targets ----------
static void fuse(uint32_t now) {
  struct Cand { uint8_t score, amp, phase, corr, rel; int8_t rssi; float live; };
  Cand best[3] = {};
  uint32_t relSum = 0, rateSum = 0;
  uint8_t active = 0, calibrating = 0, calPct = 0;
  float liveMax = 0.0f;
  for (int i = 0; i < kMaxTargets; i++) {
    if (!s_targets[i].used) continue;
    CsiMotionModel& m = s_targets[i].model;
    m.tick(now, s_targets[i].accepted, s_targets[i].dropped);
    rateSum += m.samplesPerSecond();
    if (!m.calibrated()) { calibrating++; if (m.calibrationPercent(now) > calPct) calPct = m.calibrationPercent(now); continue; }
    if (m.reliability() < 25) continue;
    active++;
    relSum += m.reliability();
    if (m.live() > liveMax) liveMax = m.live();
    Cand c = { m.motionScore(), m.amplitudeScore(), m.phaseScore(), m.correlationScore(), m.reliability(), m.rssi(), m.live() };
    for (int s = 0; s < 3; s++) {
      if (c.score > best[s].score || (c.score == best[s].score && best[s].rel == 0)) {
        for (int mv = 2; mv > s; mv--) best[mv] = best[mv - 1];
        best[s] = c; break;
      }
    }
  }
  s_rate = (uint16_t)(rateSum > 65535 ? 65535 : rateSum);
  s_rel = active ? (uint8_t)(relSum / active) : 0;
  uint32_t w = 0, ms = 0, as = 0, ps = 0, cs = 0;
  for (int i = 0; i < 3 && best[i].rel; i++) {
    w += best[i].rel; ms += best[i].score * best[i].rel; as += best[i].amp * best[i].rel;
    ps += best[i].phase * best[i].rel; cs += best[i].corr * best[i].rel;
  }
  s_score = w ? (uint8_t)(ms / w) : 0;
  s_amp = w ? (uint8_t)(as / w) : 0;
  s_phase = w ? (uint8_t)(ps / w) : 0;
  s_corr = w ? (uint8_t)(cs / w) : 0;
  s_rssi = best[0].rel ? best[0].rssi : -127;
  if (s_rssi == -127) {
    for (int i = 0; i < kMaxTargets; i++)
      if (s_targets[i].used && s_targets[i].model.rssi() > s_rssi) s_rssi = s_targets[i].model.rssi();
  }
  float lv = liveMax * 100.0f; if (lv > 1000.0f) lv = 1000.0f;
  s_live = (uint16_t)lv;
  if (!active && calibrating) {
    s_state = SENSE_STATE_CAL; s_rel = calPct / 4;
  } else if (!active) {
    s_state = SENSE_STATE_LOWDATA;
  } else if (s_score >= CsiMotionModel::kMotionThreshold) {
    s_motionVotes = s_motionVotes < 5 ? s_motionVotes + 1 : 5; s_stillVotes = 0;
    s_state = s_motionVotes >= 2 ? SENSE_STATE_MOTION : SENSE_STATE_POSSIBLE;
  } else if (s_score >= CsiMotionModel::kPossibleMotionThreshold) {
    s_motionVotes = s_stillVotes = 0; s_state = SENSE_STATE_POSSIBLE;
  } else {
    s_motionVotes = 0; s_stillVotes = s_stillVotes < 3 ? s_stillVotes + 1 : 3;
    if (s_stillVotes >= 2) s_state = SENSE_STATE_STILL;
  }
}

static uint8_t calPercent(uint32_t now) {
  uint8_t pct = 100; bool any = false;
  for (int i = 0; i < kMaxTargets; i++) if (s_targets[i].used) {
    any = true;
    const uint8_t p = s_targets[i].model.calibrationPercent(now);
    if (p < pct) pct = p;
  }
  return any ? pct : 0;
}

static uint8_t targetCount() { uint8_t n = 0; for (int i = 0; i < kMaxTargets; i++) if (s_targets[i].used) n++; return n; }

// ---- Lines out ---------------------------------------------------------------
static void sendStatus(uint32_t now) {
  outf("$S,%lu,%u,%u,%u,%u,%u,%d,%u,%u,%u,%u,%u,%u,%u,%u,%lu,%u",
       (unsigned long)now, s_score, s_state, s_rate, s_chan, 2, (int)s_rssi, s_rel, calPercent(now),
       targetCount(), s_mode, s_amp, s_phase, s_corr, s_live, (unsigned long)s_dropped, s_presence ? 1 : 0);
}

static void sendTargets(uint32_t now) {
  for (int i = 0; i < kMaxTargets; i++) {
    if (!s_targets[i].used) continue;
    const CsiMotionModel& m = s_targets[i].model;
    outf("$T,%d,%02X%02X,%d,%u,%u,%u,%u", i, s_targets[i].mac[4], s_targets[i].mac[5], (int)m.rssi(),
         (unsigned)m.samplesPerSecond(), m.calibrated() ? m.motionScore() : 0, m.calibrationPercent(now), m.reliability());
  }
}

static void sendInfo() {
  uint8_t mac[6]; esp_read_mac(mac, ESP_MAC_WIFI_STA);
  outf("$I,c6-%s,%02X:%02X:%02X:%02X:%02X:%02X,%u,%u,-,%lu", FIRMWARE_VERSION, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
       s_mode, s_chan, (unsigned long)esp_get_free_heap_size());
}

// The health line: what this costs the C6. Every kBenchMs while sensing.
static void sendBench(uint32_t now) {
  const uint32_t wallUs = (uint32_t)esp_timer_get_time() - s_wallUs0;
  const unsigned busyPct = wallUs ? (unsigned)(s_busyUs * 100 / wallUs) : 0;
  s_busyUs = 0; s_wallUs0 = (uint32_t)esp_timer_get_time();
  char lay[96] = ""; int ln = 0;
  for (int i = 0; i < kMaxTargets && ln < (int)sizeof lay - 24; i++) {
    const Target& t = s_targets[i];
    if (!t.used) continue;
    ln += snprintf(lay + ln, sizeof lay - ln, " %02X%02X:", t.mac[4], t.mac[5]);
    for (int k = 0; k < kLayouts && t.layoutLen[k] && ln < (int)sizeof lay - 12; k++)
      ln += snprintf(lay + ln, sizeof lay - ln, "%s%u%s", k ? "/" : "", (unsigned)t.layoutLen[k], t.layoutLen[k] == t.lockedLen ? "*" : "");
  }
  note("c6 heap %lu (min %lu) task %u%% stack %u seen %lu queued %lu dropped %lu txfail %lu/%d fit %lu/%lu ms layouts%s",
       (unsigned long)esp_get_free_heap_size(), (unsigned long)esp_get_minimum_free_heap_size(), busyPct,
       (unsigned)uxTaskGetStackHighWaterMark(nullptr), (unsigned long)s_seen, (unsigned long)s_accepted,
       (unsigned long)s_dropped, (unsigned long)s_txFail, s_txErr, (unsigned long)(s_fitUsLast / 1000), (unsigned long)(s_fitUsMax / 1000), lay);
  (void)now;
}

static void sendRates() {
  note("seen %lu, queued %lu, dropped %lu, txfail %lu (last %d), heap %lu, mode %u ch %u csi %u promisc %u", (unsigned long)s_seen,
       (unsigned long)s_accepted, (unsigned long)s_dropped, (unsigned long)s_txFail, s_txErr, (unsigned long)esp_get_free_heap_size(),
       (unsigned)s_mode, (unsigned)s_chan, (unsigned)s_csiOn, (unsigned)s_promisc);
  for (int i = 0; i < kMaxTargets; i++) if (s_targets[i].used) {
    const CsiMotionModel& m = s_targets[i].model;
    note("target %d %02X:%02X:%02X:%02X:%02X:%02X %u/s rssi %d tones %u (len %u, other layouts %lu) cal %u%% rel %u score %u (a%u p%u c%u) live %d",
         i, s_targets[i].mac[0], s_targets[i].mac[1], s_targets[i].mac[2], s_targets[i].mac[3], s_targets[i].mac[4],
         s_targets[i].mac[5], m.samplesPerSecond(), m.rssi(), (unsigned)m.carriers(), (unsigned)s_targets[i].lockedLen,
         (unsigned long)s_targets[i].otherLayout, m.calibrationPercent(now_ms()),
         m.reliability(), m.motionScore(), m.amplitudeScore(), m.phaseScore(), m.correlationScore(), (int)(m.live() * 100));
  }
}

// ---- Commands ----------------------------------------------------------------
static void recalibrate() {
  const uint32_t now = now_ms();
  for (int i = 0; i < kMaxTargets; i++) if (s_targets[i].used) {
    s_targets[i].accepted = s_targets[i].dropped = 0;
    s_targets[i].model.reset(now, s_calMs, s_windowMs);
  }
  s_wr = s_rd = 0;
  s_motionVotes = s_stillVotes = 0; s_state = SENSE_STATE_CAL; s_score = 0; s_presence = false;
  note("recalibrating: keep the room still for %lu s", (unsigned long)(s_calMs / 1000));
}

static void help() {
  note("commands (C6): park <chan>|auto | station <bssid> <chan> | off | cal | win <ms> | caltime <ms> | hold <ms> | vitals on|frames|off | info | rates | raw 0|1 | help");
}

// The loudest 2.4 GHz access point's channel, by a blocking scan. Sensing
// stops for the few seconds it takes. Names are never printed.
static void scan(bool pick) {
  if (s_mode == SENSE_MODE_STA) { note("scan: not while joined (the tablet's connection would drop)"); return; }
  csiStop();
  promiscOff();
  note("scanning 2.4 GHz...");
  wifi_scan_config_t cfg = {};
  cfg.show_hidden = true;
  cfg.scan_type = WIFI_SCAN_TYPE_ACTIVE;
  cfg.scan_time.active.min = 60;
  cfg.scan_time.active.max = 120;
  esp_err_t e = esp_wifi_scan_start(&cfg, true);
  if (e != ESP_OK) { note("scan: %s", esp_err_to_name(e)); if (pick) startPark(s_chan); return; }
  uint16_t n = 0;
  esp_wifi_scan_get_ap_num(&n);
  if (n > 32) n = 32;
  wifi_ap_record_t* recs = (wifi_ap_record_t*)calloc(n ? n : 1, sizeof(wifi_ap_record_t));
  int bestCh = 0, bestRssi = -127;
  uint16_t got = n;
  if (recs && n && esp_wifi_scan_get_ap_records(&got, recs) == ESP_OK) {
    for (uint16_t i = 0; i < got; i++) {
      if (recs[i].primary < 1 || recs[i].primary > 14) continue;
      if (!pick) note("ap  ch %2u  %4d dBm  %02X:%02X:%02X:%02X:%02X:%02X", recs[i].primary, recs[i].rssi,
                      recs[i].bssid[0], recs[i].bssid[1], recs[i].bssid[2], recs[i].bssid[3], recs[i].bssid[4], recs[i].bssid[5]);
      if (recs[i].rssi > bestRssi) { bestRssi = recs[i].rssi; bestCh = recs[i].primary; }
    }
  } else {
    esp_wifi_clear_ap_list();
  }
  free(recs);
  if (bestCh) note("loudest 2.4 GHz: channel %d at %d dBm", bestCh, bestRssi);
  else note("no access point heard");
  if (pick) {
    if (bestCh) { note("auto: parking on channel %d, the loudest network heard", bestCh); startPark((uint16_t)bestCh); }
    else { note("auto: nothing heard; channel %u for now", s_chan); startPark(s_chan); }
  } else if (s_mode == SENSE_MODE_PARK) {
    startPark(s_chan);
  }
}

// FRAMES: each 50 ms frame goes to the tablet as one binary message on the
// line event -- 0x01, n (readings in it), T (tones), then T x u16 LE of the
// tone's normalised amplitude x 8192 -- and the tablet runs the fit (its P4
// has the memory for a full 20 s window and a float unit; here the window
// had to shrink to 10 s beside the Bluetooth controller, bench 2026-11-17).
static const uint8_t SENSE_BIN_FRAME = 0x01;
static void vitalsFramesOn() {
  if (!s_vit) s_vit = new (std::nothrow) CsiVitals();
  if (!s_vit) { note("vitals frames: no memory"); s_vitals = false; return; }
  s_vit->reset(); s_vitals = true; s_vitFramesOnly = true; s_vitTarget = -1; s_vitFrames = 0;
  note("vitals frames on: %s; the tablet fits", s_havePin ? "from the router" : "from the busiest transmitter");
}
static void sendFrame(const float* x, int T, uint8_t n) {
  uint8_t b[3 + 2 * CsiVitals::kMaxTones];
  b[0] = SENSE_BIN_FRAME; b[1] = n; b[2] = (uint8_t)T;
  for (int i = 0; i < T; i++) {
    float v = x[i] * 8192.0f; if (v < 0) v = 0; if (v > 65535.0f) v = 65535.0f;
    const uint16_t u = (uint16_t)lroundf(v);
    b[3 + 2 * i] = (uint8_t)(u & 0xFF); b[4 + 2 * i] = (uint8_t)(u >> 8);
  }
  const esp_err_t e = esp_hosted_send_custom_data(SENSE_EVT_LINE, b, (size_t)(3 + 2 * T));
  if (e != ESP_OK) { s_txFail++; s_txErr = (int)e; }
}

static void vitalsOn() {
  s_vitFramesOnly = false;
  if (s_vit && !s_vit->window()) { delete s_vit; s_vit = nullptr; }   // a frames-only one has no window
  if (!s_vit) s_vit = new (std::nothrow) CsiVitals();
  if (!s_vit || !s_vit->begin()) {
    note("vitals: no memory for the chest signal on this radio (heap %lu)", (unsigned long)esp_get_free_heap_size());
    s_vitals = false;
    return;
  }
  s_vitals = true; s_vit->reset(); s_vitTarget = -1; s_vitFrames = 0;
  s_fitUsLast = s_fitUsMax = 0;
  note("vitals on: %s, a %d s window (heap %lu left)", s_havePin ? "chest signal from the router" : "following the busiest transmitter",
       s_vit->window() / 20, (unsigned long)esp_get_free_heap_size());
}

static void command(char* line) {
  while (*line == ' ') line++;
  if (*line == '[' || !*line) return;
  char* sp = strchr(line, ' ');
  char* arg = nullptr;
  if (sp) { *sp = 0; arg = sp + 1; while (*arg == ' ') arg++; }
  if (!strcmp(line, "station")) {
    unsigned b[6]; unsigned ch = 0;
    if (!arg || sscanf(arg, "%x:%x:%x:%x:%x:%x %u", &b[0], &b[1], &b[2], &b[3], &b[4], &b[5], &ch) != 7 || ch < 1 || ch > 14) {
      note("station <aa:bb:cc:dd:ee:ff> <channel 1-14>"); return;
    }
    uint8_t mac[6]; for (int i = 0; i < 6; i++) mac[i] = (uint8_t)b[i];
    startStation(mac, (uint16_t)ch);
    return;
  }
  if (!strcmp(line, "park")) {
    if (arg && !strcmp(arg, "auto")) { scan(true); return; }
    const int ch = arg ? atoi(arg) : s_chan;
    if (ch < 1) { note("park: which channel?"); return; }
    startPark((uint16_t)ch);
  } else if (!strcmp(line, "off")) {
    stopAll();
    note("sensing off");
  } else if (!strcmp(line, "join") || !strcmp(line, "forget") || !strcmp(line, "vitalsnet")) {
    note("%s: not on this radio; it listens (park) only", line);
  } else if (!strcmp(line, "vitals")) {
    if (arg && !strncmp(arg, "frames", 6)) vitalsFramesOn();
    else if (arg && !strncmp(arg, "on", 2)) vitalsOn();
    else {
      if (s_vitals) note("vitals off");
      s_vitals = false;
      // The window and covariance (~120 KB of the C6's heap) go back.
      delete s_vit; s_vit = nullptr; s_vitTarget = -1; s_vitFramesOnly = false;
    }
  } else if (!strcmp(line, "cal")) {
    recalibrate();
  } else if (!strcmp(line, "win")) {
    const long v = arg ? atol(arg) : 0;
    if (v < 200 || v > 5000) { note("win 200..5000 ms (now %lu)", (unsigned long)s_windowMs); return; }
    s_windowMs = (uint32_t)v; recalibrate();
  } else if (!strcmp(line, "hold")) {
    const long v = arg ? atol(arg) : -1;
    if (v < 0 || v > 120000) { note("hold 0..120000 ms (now %lu)", (unsigned long)s_holdMs); return; }
    s_holdMs = (uint32_t)v; note("presence hold %lu ms", (unsigned long)s_holdMs);
  } else if (!strcmp(line, "caltime")) {
    const long v = arg ? atol(arg) : 0;
    if (v < 5000 || v > 120000) { note("caltime 5000..120000 ms (now %lu)", (unsigned long)s_calMs); return; }
    s_calMs = (uint32_t)v; recalibrate();
  } else if (!strcmp(line, "info")) {
    sendInfo();
  } else if (!strcmp(line, "rates")) {
    sendRates();
  } else if (!strcmp(line, "scan")) {
    scan(false);
  } else if (!strcmp(line, "raw")) {
    s_raw = arg ? (uint8_t)atoi(arg) : 0;
    note("raw %u", s_raw);
  } else if (!strcmp(line, "help") || !strcmp(line, "?")) {
    help();
  } else {
    note("unknown: %s", line); help();
  }
}

// In the RPC task: copy and hand over, never act here (a scan blocks).
static void onCmd(uint32_t id, const uint8_t* d, size_t n, void* ctx) {
  (void)id; (void)ctx;
  Cmd c;
  if (n >= sizeof c.text) n = sizeof c.text - 1;
  memcpy(c.text, d, n); c.text[n] = 0;
  xQueueSend(s_cmdQ, &c, 0);
}

// ---- The task ----------------------------------------------------------------
static void senseOnce() {
  const uint32_t now = now_ms();
  const uint32_t t0 = (uint32_t)esp_timer_get_time();
  Cmd c;
  while (xQueueReceive(s_cmdQ, &c, 0) == pdTRUE) command(c.text);

  // Drain the queue into the models: at most one queue's worth a pass. Left
  // open-ended, ~300 readings a second (two SquachWatch boards pinging the
  // same router) kept this loop busy for 20-30 s at a time and no status
  // line went out (bench 2026-11-17); the callback drops the excess instead.
  uint8_t budget = kQueueSize;
  while (s_rd != s_wr && budget--) {
    Sample& s = s_queue[s_rd];
    const int t = findTarget(s.mac);
    if (t >= 0) {
      Target& tg = s_targets[t];
      tg.lastAt = s.at;
      const int b = layoutBucket(tg, s.length);
      if (b >= 0) tg.layoutN[b]++;
      if (!tg.lockedLen) tg.lockedLen = s.length;
      if (s.length == tg.lockedLen) {
        tg.accepted++;
        if (s_vitals && t == s_vitTarget && s_vit) s_vit->addSample(s.iq, s.length, s.first_word_invalid, true);
        tg.model.addSample(s.iq, s.length, s.rssi, s.at, s.first_word_invalid, CsiMotionModel::SampleEncoding::SIGNED_8_IMAG_REAL);
      } else {
        tg.otherLayout++;
      }
      // raw 2: whole samples, hex, from VITALS' transmitter (one in ten),
      // to see the bytes' format (bench).
      static uint8_t rawEvery = 0;
      if (s_raw == 2 && t == s_vitTarget && ++rawEvery >= 10) {
        rawEvery = 0;
        for (int part = 0; part * 64 < s.length; part++) {
          char h[SENSE_LINE_MAX];
          int n = snprintf(h, sizeof h, "# hex %u %u fwi%u ", s.length, part, s.first_word_invalid);
          for (int i = part * 64; i < s.length && i < part * 64 + 64; i++) n += snprintf(h + n, sizeof h - n, "%02x", (uint8_t)s.iq[i]);
          outLine(h);
        }
      }
      if (s_raw == 1) {
        char b2[160];
        int n = snprintf(b2, sizeof b2, "# csi %02X:%02X:%02X:%02X:%02X:%02X ch%u rssi %d len %u fwi %u", s.mac[0], s.mac[1], s.mac[2], s.mac[3],
                         s.mac[4], s.mac[5], s.channel, s.rssi, s.length, s.first_word_invalid);
        for (int i = 0; i < 8 && n < (int)sizeof b2 - 8; i++) n += snprintf(b2 + n, sizeof b2 - n, " %d", (int)s.iq[i]);
        outLine(b2);
      }
    }
    s_rd = (uint8_t)((s_rd + 1) % kQueueSize);
  }

  if (s_mode == SENSE_MODE_PARK || s_mode == SENSE_MODE_STA) {
    if (now - s_lastRankMs >= kRankMs) { s_lastRankMs = now; rankSources(now); }
    if (now - s_lastFusionMs >= 1000) {
      s_lastFusionMs = now; fuse(now); sendTargets(now);
      if (s_vitals) {
        static uint8_t wins = 0;
        int best = -1; uint16_t bestRate = 0;
        const bool curOk = s_vitTarget >= 0 && s_targets[s_vitTarget].used && memcmp(s_vitMac, s_targets[s_vitTarget].mac, 6) == 0;
        for (int i = 0; i < kMaxTargets; i++) if (s_targets[i].used && s_targets[i].model.samplesPerSecond() > bestRate) { bestRate = s_targets[i].model.samplesPerSecond(); best = i; }
        if (curOk && best != s_vitTarget) {
          const uint16_t cur = s_targets[s_vitTarget].model.samplesPerSecond();
          wins = bestRate * 2 > cur * 3 ? (uint8_t)(wins + 1) : 0;
          if (wins < 5) best = s_vitTarget;
        } else wins = 0;
        // Joined: the router, always (its stream is the pinged, steady one).
        if (s_havePin) for (int i = 0; i < kMaxTargets; i++) if (s_targets[i].used && isPinned(s_targets[i].mac)) { best = i; }
        if (best >= 0 && (best != s_vitTarget || memcmp(s_vitMac, s_targets[best].mac, 6) != 0)) {
          wins = 0;
          s_vitTarget = best; memcpy(s_vitMac, s_targets[best].mac, 6); if (s_vit) s_vit->reset();
          note("vitals: chest signal from %02X:%02X:%02X:%02X:%02X:%02X", s_vitMac[0], s_vitMac[1], s_vitMac[2], s_vitMac[3], s_vitMac[4], s_vitMac[5]);
        }
      }
      if (s_state == SENSE_STATE_MOTION) { s_presence = true; s_lastMotionMs = now; }
      else if (s_presence && (now - s_lastMotionMs > s_holdMs || s_state == SENSE_STATE_CAL || s_state == SENSE_STATE_LOWDATA)) s_presence = false;
    }
    if (now - s_lastLineMs >= 1000 / SENSE_LINE_HZ) {
      s_lastLineMs = now;
      float lv = 0.0f;
      for (int i = 0; i < kMaxTargets; i++) if (s_targets[i].used && s_targets[i].model.calibrated() && s_targets[i].model.live() > lv) lv = s_targets[i].model.live();
      lv *= 100.0f; if (lv > 1000.0f) lv = 1000.0f; s_live = (uint16_t)lv;
      sendStatus(now);
    }
    if (s_vitals && s_vit && s_vitFramesOnly) {
      float x[CsiVitals::kMaxTones]; int T = 0; uint8_t n = 0;
      if (s_vit->frame(now, x, T, n)) { s_vitFrames++; sendFrame(x, T, n); }
    } else if (s_vitals && s_vit) {
      float c1, c2; uint8_t n, q;
      const uint32_t f0 = (uint32_t)esp_timer_get_time();
      const bool closed = s_vit->tick(now, c1, c2, n, q);
      const uint32_t fus = (uint32_t)esp_timer_get_time() - f0;
      if (fus > 5000) { s_fitUsLast = fus; if (fus > s_fitUsMax) s_fitUsMax = fus; }   // only a refit takes this long
      if (closed) {
        s_vitFrames++;
        outf("$V,%lu,%ld,%ld,%u,%u", (unsigned long)now, lroundf(c1 * 10000.0f), lroundf(c2 * 10000.0f), (unsigned)n, (unsigned)q);
      }
    }
    if (now - s_lastInfoMs >= kInfoMs) { s_lastInfoMs = now; sendInfo(); }
    if (now - s_lastBenchMs >= kBenchMs) { s_lastBenchMs = now; sendBench(now); }
  }
  s_busyUs += (uint32_t)esp_timer_get_time() - t0;
}

static void senseTask(void*) {
  s_wallUs0 = (uint32_t)esp_timer_get_time();
  for (;;) {
    senseOnce();
    // Idle: wait for a command. Sensing: a short nap (1 kHz tick), as the C5's loop does.
    vTaskDelay((s_mode == SENSE_MODE_PARK || s_mode == SENSE_MODE_STA) ? pdMS_TO_TICKS(s_vitals ? 1 : 2) : pdMS_TO_TICKS(50));
  }
}

extern "C" void squach_sense_init(void) {
  s_cmdQ = xQueueCreate(8, sizeof(Cmd));
  xTaskCreate(senseTask, "sense", 8192, nullptr, 4, nullptr);
  esp_hosted_register_custom_callback(SENSE_CMD_TEXT, onCmd, nullptr);
  ESP_LOGI(TAG, "sensing extension ready (idle until asked)");
}
#endif  // SENSE_C6
