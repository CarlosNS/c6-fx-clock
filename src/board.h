#pragma once

// Everything that depends on the hardware. Porting the clock to another board (the final target is
// the Waveshare ESP32-S3-Touch-AMOLED-2.06) starts here, in lcd.c and in the input handling.

#define BOARD_NAME "Waveshare ESP32-C6-LCD-1.47"

// ---- Capabilities ----
// Wi-Fi and Bluetooth can't run at the same time here: two 110 KB framebuffers leave room for one
// radio stack only, so each runs in its own boot mode (bootmode.c). Boards with PSRAM can set 0.
#define BOARD_RADIO_EXCLUSIVE 1
// Framebuffers in normal use (the UPDATE mode always uses 1 to leave heap for HTTPS)
#define BOARD_FB_COUNT 2

// ---- Display: ST7789 172x320 over SPI ----
#define BOARD_LCD_MOSI 6
#define BOARD_LCD_SCLK 7
#define BOARD_LCD_CS 14
#define BOARD_LCD_DC 15
#define BOARD_LCD_RST 21
#define BOARD_LCD_BL 22
#define BOARD_SD_CS 4        // the SD card shares the bus: keep it deselected
#define BOARD_LCD_X_GAP 34   // the 172 px panel sits in the middle of the 240 px controller RAM

// ---- Inputs / outputs ----
// BOOT button, active low. It is also a strapping pin: held at power-on it enters the ROM download
// mode, so the firmware only reads it once running.
#define BOARD_PIN_BUTTON 9
#define BOARD_PIN_LED 8      // WS2812 RGB LED
