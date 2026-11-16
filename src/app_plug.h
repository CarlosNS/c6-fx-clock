// The USB cable was plugged in: light the screen and say what it brings (app_plug.c)
#pragma once
#include <stdbool.h>
#include <stdint.h>
void plug_notice(void);
// For the tests: the notice as if on a charger (1 charging, 2 full) or a computer (3 charging, 4 full)
void plug_test(int mode);
// The charger scene's frame t ms in, on the native framebuffer set by ui_begin() (also used at boot,
// when plugging in started a watch that was off: main.c)
void plug_scene(uint32_t t, bool charging);
#define PLUG_SCENE_MS 5000
