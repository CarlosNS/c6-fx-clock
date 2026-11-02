#pragma once
#include <stdbool.h>
#include <stddef.h>

// User settings, described once in a table (settings.c). The same short key is used for the NVS
// entry, the "<key> <value>" command and the field in the status JSON, so every transport (web,
// and later Bluetooth) gets the full set for free.

void settings_load(void);  // from NVS into g_app (missing / out-of-range values keep their defaults)
void settings_save(void);  // writes the persistent ones
// Applies "<key> <value>"; returns false if the key is unknown. Marks g_app.dirty when needed.
bool settings_set(const char *key, int value);
// Appends "key":value pairs (comma separated, no braces) to buf; returns the new length
size_t settings_json(char *buf, size_t size, size_t len);
