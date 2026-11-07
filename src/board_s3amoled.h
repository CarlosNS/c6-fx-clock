#pragma once

// Waveshare ESP32-S3-Touch-AMOLED-2.06: everything that depends on this board (selected by board.h).
// Pins from Waveshare's BSP (examples/factory/components/waveshare__esp32_s3_touch_amoled_2_06).

#define BOARD_NAME "Waveshare ESP32-S3-Touch-AMOLED-2.06"
#define BOARD_CHIP "ESP32-S3"

// ---- Capabilities ----
// 8 MB of PSRAM: framebuffers live there, so both radio stacks fit at once (phase 6 checks it)
#define BOARD_RADIO_EXCLUSIVE 0
#define BOARD_FB_COUNT 2

// ---- Display: CO5300 AMOLED 410x502 over QSPI (sh8601-compatible command set) ----
// Compatibility mode (phase 3 of docs/PLAN-s3.md): the faces still draw on the C6's 172x320 canvas
// (LCD_W x LCD_H) and lcd_co5300.c enlarges it while sending: portrait x1.5 (258x480), landscape
// x1.25 (400x215), centred on the panel.
#define BOARD_LCD_W 172
#define BOARD_LCD_H 320
#define BOARD_PANEL_W 410
#define BOARD_PANEL_H 502
#define BOARD_LCD_CS 12
#define BOARD_LCD_SCLK 11
#define BOARD_LCD_D0 4
#define BOARD_LCD_D1 5
#define BOARD_LCD_D2 6
#define BOARD_LCD_D3 7
#define BOARD_LCD_RST 8
#define BOARD_LCD_X_GAP 22   // the 410 px panel starts 22 columns into the controller RAM
// No backlight pin: brightness is the panel's register 0x51
// Visible area: the corners are rounded with a radius of about 115-120 px (measured with arcs of
// known radius on 2026-11-07), and the outermost 2 px of each edge may be hidden. A pixel (x, y)
// in a corner is visible if it lies inside the circle of radius BOARD_LCD_CORNER_R centred
// BOARD_LCD_CORNER_R px in from both edges. Keep anything that matters inside.
#define BOARD_LCD_CORNER_R 120
#define BOARD_LCD_EDGE 2

// ---- Shared I2C bus: touch, AXP2101, PCF85063 RTC, QMI8658 IMU, ES8311 / ES7210 codecs ----
#define BOARD_I2C_SDA 15
#define BOARD_I2C_SCL 14
#define BOARD_TOUCH_INT 38
#define BOARD_TOUCH_RST 9

// PCF85063A real-time clock on the I2C bus (rtc_pcf85063.c): keeps the time while the board is off
#define BOARD_HAS_RTC 1

// ---- Audio (I2S) ----
#define BOARD_I2S_MCLK 16
#define BOARD_I2S_SCLK 41
#define BOARD_I2S_LRCK 45
#define BOARD_I2S_DOUT 40    // to the ES8311 (speaker)
#define BOARD_I2S_DIN 42     // from the ES7210 (microphones)
#define BOARD_PIN_AMP 46     // speaker amplifier enable

// ---- microSD (SDMMC, 1 bit) ----
#define BOARD_SD_CLK 2
#define BOARD_SD_CMD 1
#define BOARD_SD_D0 3

// No RGB LED on this board (BOARD_PIN_LED undefined: led.c does nothing)

// ---- Inputs ----
// BOOT button, active low (also a strapping pin: held at power-on it enters the ROM download mode)
#define BOARD_PIN_BUTTON 0
