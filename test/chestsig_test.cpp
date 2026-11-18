// Host test for src/sense_cyd/csi_vitals.* through addSample8, the ESP32/S3's
// 8-bit L-LTF readings (64 tones of int8 imaginary, real): synthetic readings
// with a breathing wobble on some tones, a heartbeat far smaller, noise, the
// quantisation an int8 reading has, gain steps and uneven arrival times. c1
// must follow the breathing; the gain steps must not show.
#include "../src/sense_cyd/csi_vitals.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static double urand() { return rand() / (RAND_MAX + 1.0); }
static double gauss() { return sqrt(-2 * log(urand() + 1e-12)) * cos(6.283185307 * urand()); }
static bool dataTone(int k) { return (k >= 2 && k <= 26) || (k >= 38 && k <= 62); }

int main() {
    srand(42);
    CsiVitals v;
    if (!v.begin()) { printf("FAIL alloc\n"); return 1; }
    double base[64], wBreath[64], wHeart[64], phase[64];
    for (int k = 0; k < 64; k++) {
        base[k] = 18 + 30 * urand();                 // int8 readings sit well inside +-64
        wBreath[k] = (urand() - 0.5) * 0.12;
        wHeart[k] = (urand() - 0.5) * 0.008;
        phase[k] = urand() * 6.28;
    }
    const double fb = 14.0 / 60, fh = 66.0 / 60;
    double t = 0; int frames = 0, nc = 0, lastQ = 0;
    double sxy = 0, sxx = 0, syy = 0;
    while (t < 90) {
        t += 0.012 + 0.025 * urand();                // ~50 a second, uneven
        const uint32_t ms = (uint32_t)(t * 1000);
        const double gain = (int)(t / 7) % 2 ? 1.3 : 1.0;   // an AGC step every 7 s
        const double breath = sin(6.283185307 * fb * t), heart = sin(6.283185307 * fh * t);
        int8_t iq[128];
        memset(iq, 0, sizeof iq);
        for (int k = 0; k < 64; k++) {
            if (!dataTone(k)) continue;
            const double amp = gain * base[k] * (1 + wBreath[k] * breath + wHeart[k] * heart + 0.01 * gauss());
            const long re = lround(amp * cos(phase[k])), im = lround(amp * sin(phase[k]));
            iq[k * 2] = (int8_t)(im > 127 ? 127 : im < -128 ? -128 : im);
            iq[k * 2 + 1] = (int8_t)(re > 127 ? 127 : re < -128 ? -128 : re);
        }
        v.addSample8(iq, sizeof iq);
        float c1, c2; uint8_t n, q;
        if (v.tick(ms, c1, c2, n, q)) {
            frames++; lastQ = q;
            if (t > 30) { const double b = sin(6.283185307 * fb * (ms / 1000.0)); sxy += c1 * b; sxx += (double)c1 * c1; syy += b * b; nc++; }
        }
    }
    const double corr = sxy / sqrt(sxx * syy + 1e-30);
    printf("chest signal (8-bit L-LTF): tones %d  frames %d (%.1f/s)  q %d  |corr(c1, breathing)| %.3f over %d frames\n",
           v.tones(), frames, frames / 90.0, lastQ, fabs(corr), nc);
    const bool ok = v.tones() == 50 && frames > 90 * 18 && fabs(corr) > 0.85;
    printf("  c1 follows the breathing, gain steps and all            %s\n", ok ? "ok" : "FAIL");

    // The Tab5's split (sense_tab5.cpp): the C6 closes the frames (int8
    // readings, 64 tones) and sends each tone's normalised amplitude as a
    // u16 of x 8192; push() does the rest. The rounding must not lose the
    // breathing, which moves the amplitudes by about a percent.
    CsiVitals w;
    if (!w.begin()) { printf("FAIL alloc 2\n"); return 1; }
    double wb[64], wbr[64];
    for (int k = 0; k < 64; k++) { wb[k] = 0.6 + 0.8 * urand(); wbr[k] = (urand() - 0.5) * 0.03; }
    double sxy2 = 0, sxx2 = 0, syy2 = 0; int nc2 = 0, fr2 = 0;
    for (int f = 0; f < 20 * 90; f++) {
        const double tt = f / 20.0, br = sin(6.283185307 * fb * tt);
        float x[64]; double tot = 0;
        for (int k = 0; k < 64; k++) { x[k] = (float)(wb[k] * (1 + wbr[k] * br + 0.004 * gauss())); tot += x[k]; }
        for (int k = 0; k < 64; k++) {   // normalised as the C6 does, then the wire's rounding
            const double v = x[k] * 64 / tot * 8192.0;
            x[k] = (float)(lround(v < 0 ? 0 : v > 65535 ? 65535 : v) / 8192.0);
        }
        float c1, c2; uint8_t q;
        if (w.push(x, 64, c1, c2, q)) {
            fr2++;
            if (tt > 30) { sxy2 += c1 * br; sxx2 += (double)c1 * c1; syy2 += br * br; nc2++; }
        }
    }
    const double corr2 = sxy2 / sqrt(sxx2 * syy2 + 1e-30);
    printf("chest signal (Tab5: C6 frames, u16 wire, fit on the P4): frames %d  |corr| %.3f over %d\n", fr2, fabs(corr2), nc2);
    const bool ok2 = fr2 == 20 * 90 && fabs(corr2) > 0.85;
    printf("  c1 follows the breathing through the wire's rounding    %s\n", ok2 ? "ok" : "FAIL");
    return ok && ok2 ? 0 : 1;
}
