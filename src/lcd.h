#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "board.h"

// CO5300 AMOLED, 410x502 (lcd_co5300.c).
// Faces are either portrait (LCD_W x LCD_H) or landscape (LAND_W x LAND_H); the framebuffer holds
// the same number of pixels in both.
#define LCD_W BOARD_LCD_W
#define LCD_H BOARD_LCD_H
#define LAND_W LCD_H
#define LAND_H LCD_W

// Current orientation's size, used by the gfx primitives
extern int g_lcd_w, g_lcd_h;

// RGB565 already byte-swapped for the SPI bus, so pixels can be written directly to the framebuffer
static inline uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    uint16_t c = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
    return (uint16_t)((c >> 8) | (c << 8));
}

// buffers: 2 = double buffering (normal), 1 = single (the UPDATE boot)
void lcd_init(int buffers);
// Framebuffer to draw the next frame into (changes after every lcd_flush when double buffered)
uint16_t *lcd_fb(void);
// Sends the current framebuffer to the panel and switches to the other buffer
void lcd_flush(void);
void lcd_set_brightness(int percent);
// Puts the panel to sleep while the screen is off (saves power on the AMOLED) and wakes it before the
// next frame
void lcd_sleep(bool sleep);
// Frame most recently sent to the panel (for screenshots; only stable while rendering is paused)
const uint16_t *lcd_last_frame(void);
// Switches to the landscape canvas (shown level, enlarged x1.25). Waits for
// any frame in flight first
void lcd_set_landscape(bool landscape);
