#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Battery log in RTC memory (see batlog.c): one sample every 5 minutes
void batlog_init(void);
// Called from the main loop every pass
void batlog_tick(uint32_t now_ms, bool screen_on, bool wifi_joined, bool ap_on);
// The log as CSV, oldest first; returns the length written
size_t batlog_csv(char *buf, size_t size);
