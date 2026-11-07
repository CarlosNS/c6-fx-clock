#pragma once

// Waveshare ESP32-C6-LCD-1.47: everything that depends on this board (selected by board.h). The
// rest of the board-specific code lives in lcd.c and in the input handling.

#define BOARD_NAME "Waveshare ESP32-C6-LCD-1.47"
#define BOARD_CHIP "ESP32-C6"

// ---- Capabilities ----
// Wi-Fi and Bluetooth can't run at the same time here: two 110 KB framebuffers leave room for one
// radio stack only, so each runs in its own boot mode (bootmode.c). Boards with PSRAM can set 0.
#define BOARD_RADIO_EXCLUSIVE 1
// Framebuffers in normal use (the UPDATE mode always uses 1 to leave heap for HTTPS)
#define BOARD_FB_COUNT 2

// ---- Display: ST7789 172x320 over SPI ----
#define BOARD_LCD_W 172
#define BOARD_LCD_H 320
#define BOARD_LCD_CORNER_R 0     // not measured: the faces were laid out by eye on this panel
#define BOARD_LCD_EDGE 0
#define BOARD_LCD_MOSI 6
#define BOARD_LCD_SCLK 7
#define BOARD_LCD_CS 14
#define BOARD_LCD_DC 15
#define BOARD_LCD_RST 21
#define BOARD_LCD_BL 22
#define BOARD_SD_CS 4        // the SD card shares the bus: keep it deselected
#define BOARD_LCD_X_GAP 34   // the 172 px panel sits in the middle of the 240 px controller RAM

// No real-time clock: the time is lost at every power cut
#define BOARD_HAS_RTC 0
// No touch screen: the interactive faces use the pad in the apps
#define BOARD_HAS_TOUCH 0

// ---- Inputs / outputs ----
// BOOT button, active low. It is also a strapping pin: held at power-on it enters the ROM download
// mode, so the firmware only reads it once running.
#define BOARD_PIN_BUTTON 9
#define BOARD_PIN_LED 8      // WS2812 RGB LED
