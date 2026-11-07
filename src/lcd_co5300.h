#pragma once
#include <stdint.h>

// CO5300 AMOLED over QSPI (Waveshare ESP32-S3-Touch-AMOLED-2.06). Pixels are RGB565 byte-swapped
// (high byte first), like the C6 framebuffer, so rgb565() from lcd.h works unchanged.

// Resets and initializes the panel; it stays dark (brightness 0) until co5300_set_brightness
void co5300_init(void);
// 0..100 % mapped to register 0x51 (0..255)
void co5300_set_brightness(int percent);
// Draws a w x h block at (x, y) and waits until it has been sent. The CO5300 needs even start
// coordinates and even sizes. At most CO5300_MAX_BLOCK_BYTES per call; the buffer must be DMA
// capable (internal RAM).
#define CO5300_MAX_BLOCK_BYTES (32 * 1024)
void co5300_draw(int x, int y, int w, int h, const uint16_t *pixels);
