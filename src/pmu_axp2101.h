#pragma once
#include <stdbool.h>

// AXP2101 power manager. Phase 1 only reads it: the board's defaults already power the screen and
// every chip, so no rail is changed yet. Returns false if the chip does not answer.
bool pmu_log_status(void);

typedef struct {
    bool present;    // a battery is connected
    bool usb;        // USB power is there
    bool charging;
    int percent;     // fuel gauge 0..100, -1 unknown
    int mv;          // battery voltage, -1 unknown
} pmu_battery_t;

// Battery state; false if the chip does not answer
bool pmu_battery(pmu_battery_t *b);
