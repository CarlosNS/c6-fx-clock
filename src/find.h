// Finding each other (find.c).
//
// Find the watch: the phone sends "find 1"; the watch rings at full volume (even in do not disturb)
// and flashes its screen at full brightness until "find 0", a touch, BOOT or PWR, or 5 minutes. When
// it stops by itself or on the watch it tells the phone: {"t":"find","st":"off"}.
//
// Find the phone: the quick settings tile sends {"t":"findphone","st":"on"} (or "off"); the phone
// rings until stopped there or on the watch, and then sends "findphone 0".
#pragma once
#include <stdbool.h>

// "find 0|1" and "findphone 0" from the phone; false if the line is neither
bool find_command(const char *cmd);
bool find_active(void);
// tell: let the phone know (it was stopped on the watch)
void find_stop(bool tell);

// The quick settings tile
bool find_phone_active(void);
void find_phone_toggle(void);
