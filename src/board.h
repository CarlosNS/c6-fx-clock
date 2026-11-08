#pragma once

// Marea runs on the Waveshare ESP32-S3-Touch-AMOLED-2.06: everything that depends on the board.
// Pins from Waveshare's BSP (examples/factory/components/waveshare__esp32_s3_touch_amoled_2_06).
#include "sdkconfig.h"

#define BOARD_NAME "Waveshare ESP32-S3-Touch-AMOLED-2.06"
// Board id in the status and in the update manifest: its image is under manifest.boards.s3amoled
// (the manifest's top-level keys belong to the old ESP32-C6 clocks and are never touched)
#define BOARD_ID "s3amoled"

// ---- Display: CO5300 AMOLED 410x502 over QSPI (sh8601-compatible command set) ----
// Faces not redrawn for this screen yet draw on a 172x320 canvas (LCD_W x LCD_H) that lcd_co5300.c
// enlarges while sending: portrait x1.5 (258x480), landscape x1.25 (400x215), centred.
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
#define BOARD_TOUCH_INT 38   // FT3168 touch (touch_ft3168.c)
#define BOARD_TOUCH_RST 9
// QMI8658 accelerometer (imu_qmi8658.c). Board axis <- {chip axis 0..2, sign}, for x (right),
// y (up) and z (out of the screen) with the screen upright.
// z: lying flat with the screen up the chip reads z = -1 g (2026-11-07). x / y not checked yet.
#define BOARD_IMU_MAP {{0, 1}, {1, 1}, {2, -1}}

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

// ---- Inputs ----
// BOOT button, active low (also a strapping pin: held at power-on it enters the ROM download mode)
#define BOARD_PIN_BUTTON 0

// Board bring-up that has to happen before anything else: I2C bus, power manager, touch, IMU
void board_init(void);
