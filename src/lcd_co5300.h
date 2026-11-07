#pragma once

// Extras of the S3 display driver (lcd_co5300.c) beyond lcd.h

// Time the last frame took to be sent to the panel, in microseconds (copy from PSRAM included)
int lcd_send_us(void);
