#pragma once
#include <stdint.h>

// On-board WS2812 RGB LED (GPIO8)
void led_init(void);
void led_set(uint8_t r, uint8_t g, uint8_t b);
