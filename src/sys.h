#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "input.h"

// The watch's shell: which screen is showing (the face, the quick settings, the launcher or an app),
// the gestures that move between them and what BOOT does on each.
//
// On the face: swipe down from the top edge = quick settings, up from the bottom edge = launcher,
// left / right = previous / next face, long press = face picker. Elsewhere: BOOT or a swipe right
// from the left edge = back.

typedef enum { VIEW_FACE, VIEW_QUICK, VIEW_LAUNCHER, VIEW_APP } view_t;

void sys_init(void);
view_t sys_view(void);
// A gesture; waking: this touch turned the screen on (it does nothing else)
void sys_event(const ui_event_t *e);
// BOOT button: short press
void sys_button_short(void);
// Draws the current native view if something changed; returns true when there is a new frame
bool sys_draw(uint32_t now);
// The current view needs the screen to stay on (flashlight, alarm ringing...)
bool sys_keep_awake(void);
// Something shown on the system screens changed (time, battery, link): redraw
void sys_dirty(void);

void sys_home(void);
void sys_open_quick(void);
void sys_open_launcher(void);
void sys_open_app(int index);
int sys_app_count(void);

// Requests from other tasks (console, web, Bluetooth), run by the main loop:
// "home", "quick", "launcher", "app N", "tap X Y", "swipe left|right|up|down [edge]", "long X Y"
void sys_request(const char *cmd);
void sys_poll_requests(void);
