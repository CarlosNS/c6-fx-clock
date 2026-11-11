#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Step counter from the accelerometer, with a 30-day history kept in NVS.

#define STEPS_DAYS 30

void steps_init(void);
// One accelerometer sample (milli-g); call 20-40 times a second, screen on or off
void steps_feed(int x, int y, int z, uint32_t now_ms);
// Today's steps
int steps_today(void);
// Console "stepsset <n>": today's count, to see faces and apps at any point of the goal (tests)
void steps_debug_set(int n);
// Calibration log (raw accelerometer samples to the microSD): start, samples so far (-1 = not
// logging), stop (writes the file and gives its name; false if it could not)
void steps_log_start(void);
int steps_log_samples(void);
bool steps_log_stop(char *name, size_t n);
// Steps of `days_ago` days back (0 = today, up to STEPS_DAYS - 1); -1 if unknown
int steps_day(int days_ago);
// Called once a minute by the main loop: rolls the day over at midnight and saves now and then
void steps_minute(void);
