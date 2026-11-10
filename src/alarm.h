#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Alarms: up to ALARMS_MAX, kept in NVS. days: bit 0 = Monday ... bit 6 = Sunday; 0 = once (switched
// off after it rings).

#define ALARMS_MAX 6
#define SNOOZE_MIN 5

// Ringing screen animations (app_alarms.c); the app and the page list them by name ("get alarms")
enum { ALARM_ANIM_SUNRISE, ALARM_ANIM_BOMB, ALARM_ANIMS };
#define ALARM_LABEL_MAX 24  // bytes of UTF-8, with the terminator

typedef struct {
    uint8_t h, m, days, on;
    uint8_t anim;                 // ALARM_ANIM_*
    uint8_t rsv[3];
    char label[ALARM_LABEL_MAX];  // set from the phone ("alarm <i> label <text>"); empty = none
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
// The alarms as JSON for the app and the page: {"list":[{"h":7,"m":30,"d":31,"on":1,"a":0,"l":"Trabajo"}],
// "anims":["Amanecer","Bomba"]} (GET /alarms, "get alarms" over Bluetooth)
size_t alarms_json(char *buf, size_t size);
// "alarm add <h> <m> <days>" / "alarm <i> del" / "alarm <i> time <h> <m>" / "alarm <i> days <d>" /
// "alarm <i> label <text>" / "alarm <i> anim <n>" / "alarm <i> on <0|1>" / "alarm <i> test";
// true if it was an alarm command
bool alarm_command(const char *cmd);
const char *alarm_anim_name(int anim);
