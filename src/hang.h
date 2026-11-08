#pragma once
#include <stdint.h>
#include "board.h"

// Hang tracking: the main loop and the display sender write here which
// step they are in. The variables live in RTC memory that a reset does not clear, so when the task
// watchdog reboots a stuck clock, board_init() can tell where each one was.
extern volatile uint32_t g_stage_main, g_stage_lcd;
#define STAGE_MAIN(n) (g_stage_main = (n))
#define STAGE_LCD(n) (g_stage_lcd = (n))
