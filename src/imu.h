#pragma once
#include <stdbool.h>

// Motion sensor (boards with BOARD_HAS_IMU): only the accelerometer, for "raise to wake".

// false if the sensor does not answer
bool imu_init(void);
// Acceleration in milli-g on the board's axes, as seen with the screen upright (portrait) facing
// you: x to the right, y up, z out of the screen. Lying flat with the screen up, z is about +1000.
bool imu_read(int *x, int *y, int *z);
