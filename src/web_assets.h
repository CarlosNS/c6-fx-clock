// The control page's pictures and fonts (src/web/), packed into web.pack by tools/web_pack.py,
// embedded in the firmware and served at /a/<name> (web.c).
#pragma once
#include <stddef.h>
#include <stdint.h>

// Finds a file in the pack by name ("fx3.webp", len bytes); NULL if it is not there
const uint8_t *web_asset_find(const char *name, size_t len, size_t *size);
