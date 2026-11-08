#pragma once
#include <stdbool.h>

// QMI8658 motion sensor: the accelerometer, for "raise to wake" and the step counter.

// false if the sensor does not answer
bool imu_init(void);
// Acceleration in milli-g on the board's axes, as seen with the screen upright (portrait) facing
// you: x to the right, y up, z out of the screen. Lying flat with the screen up, z is about +1000.
bool imu_read(int *x, int *y, int *z);
