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
// Cuts the power (software power off); does not return if it works
void pmu_power_off(void);

// PWR key: enables its events and sets the PMU's forced power-off to 10 s held
void pmu_pwrkey_init(void);
// Events since the last call: PMU_PKEY_* bits (0 if none)
#define PMU_PKEY_UP 0x01     // released
#define PMU_PKEY_DOWN 0x02   // pressed
#define PMU_PKEY_LONG 0x04
#define PMU_PKEY_SHORT 0x08
int pmu_pwrkey_poll(void);
