// The USB cable was plugged in: light the screen and say what it brings (app_plug.c)
#pragma once
void plug_notice(void);
// For the tests: the notice as if on a charger (1 charging, 2 full) or a computer (3 charging, 4 full)
void plug_test(int mode);
