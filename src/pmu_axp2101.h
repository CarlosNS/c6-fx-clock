#pragma once
#include <stdbool.h>

// AXP2101 power manager. Phase 1 only reads it: the board's defaults already power the screen and
// every chip, so no rail is changed yet. Returns false if the chip does not answer.
bool pmu_log_status(void);
