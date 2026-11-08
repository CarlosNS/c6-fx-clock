#pragma once
#include <stdbool.h>
#include <stdint.h>

// Alarms: up to ALARMS_MAX, kept in NVS. days: bit 0 = Monday ... bit 6 = Sunday; 0 = once (switched
// off after it rings).

#define ALARMS_MAX 6
#define SNOOZE_MIN 5

typedef struct {
    uint8_t h, m, days, on;
} alarm_t;

extern alarm_t g_alarms[ALARMS_MAX];
extern int g_nalarms;

void alarms_init(void);
void alarms_save(void);
// Called once a minute by the main loop (valid time only): starts ringing when one is due
void alarms_minute(void);
bool alarm_ringing(void);
void alarm_snooze(void);
void alarm_stop(void);
// The alarm that is ringing (for its screen)
const alarm_t *alarm_current(void);
// "L M X J V S D" style day letters for the current language
const char *alarm_day_letter(int i);
