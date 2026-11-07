#pragma once

// Board selection: one header per board, picked from the chip the firmware is built for
// (PlatformIO environments c6lcd and s3amoled). See docs/PLAN-s3.md.
#include "sdkconfig.h"

#if CONFIG_IDF_TARGET_ESP32S3
#include "board_s3amoled.h"
#elif CONFIG_IDF_TARGET_ESP32C6
#include "board_c6lcd.h"
#else
#error "No board header for this chip"
#endif
