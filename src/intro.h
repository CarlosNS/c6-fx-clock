// Power-on animation (intro.c)
#pragma once
#include <stdbool.h>

// True when the watch starts from off (power-on reset), or the console asked for it ("intro")
bool intro_wanted(void);
// Plays it (about 4 s) and lights the panel at this brightness
void intro_play(int brightness);
// Console "intro": play it on the next boot (the console then restarts the watch)
void intro_request(void);
// Draws and sends one frame at t ms (console "introshot <ms>", to take screenshots of it)
#include <stdint.h>
void intro_frame(uint32_t t);
