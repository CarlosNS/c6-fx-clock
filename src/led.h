#pragma once
#include <stdint.h>

// On-board WS2812 RGB LED (C6: GPIO8). Does nothing on boards without one (BOARD_PIN_LED undefined)
void led_init(void);
void led_set(uint8_t r, uint8_t g, uint8_t b);
