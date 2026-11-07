#pragma once
#include <stdbool.h>
#include <stdint.h>

// Battery-backed real-time clock (boards with BOARD_HAS_RTC). Times are UTC seconds since 1970.

// Reads the clock, waiting for its next second tick (up to about 1 s) so the result is exact to a
// few ms. false if the chip does not answer or has lost the time (its oscillator stopped).
bool rtc_read(int64_t *epoch);
// Stores the time given as sec + ms. The write happens at the next full second, from a timer, so the
// caller never waits.
void rtc_write(int64_t sec, int ms);
