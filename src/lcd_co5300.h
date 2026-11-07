#pragma once
#include <stdbool.h>

// Extras of the S3 display driver (lcd_co5300.c) beyond lcd.h

// Time the last frame took to be sent to the panel, in microseconds (copy from PSRAM included)
int lcd_send_us(void);
// Panel pixel -> canvas pixel for the current orientation (the canvas is enlarged and centred on the
// panel). The result may fall outside the canvas; returns whether it is inside.
bool lcd_panel_to_canvas(int px, int py, int *cx, int *cy);
