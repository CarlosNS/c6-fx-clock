#pragma once
#include <stdbool.h>

// FT3168 touch screen. Coordinates are panel pixels
// (BOARD_PANEL_W x BOARD_PANEL_H), upright like the portrait screen.

// false if the controller does not answer
bool touch_init(void);
// true while a finger is down, with its position
bool touch_read(int *x, int *y);
