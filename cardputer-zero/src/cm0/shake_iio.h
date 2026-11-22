// SquachWatch-CM0 -- the CM0 half of Shake (shake.h): the BMI270 over IIO.
//
// The kernel's bmi270 driver owns the chip (iio:deviceN, name "bmi270"), so
// instead of the firmware's I2C FIFO reads, a small thread samples
// in_accel_{x,y,z}_raw at the chip's own output rate (the FIFO and the rate
// are root-only) and queues them; Shake::poll() on the loop runs the
// firmware's own detector over them (shake_iio.cpp).
#pragma once
#include <cstdint>

namespace cm0 {

// Shake::begin() finds the device itself; tests point it at a directory
// holding in_accel_{x,y,z}_raw, in_accel_scale and
// in_accel_sampling_frequency. Empty = search /sys/bus/iio/devices.
void shakeSetDir(const char* dir);

// Stops the reader thread (exit). Shake::begin() may start it again.
void shakeClose();

// Test hook: one sample, at firmware time tMs (millis()), as |a| in mg.
// Queued exactly as the reader thread queues a real one.
void shakeInject(uint32_t tMs, int32_t magMg);

// Samples taken by the reader thread since begin() (for the bench).
uint32_t shakeSamples();

}   // namespace cm0
