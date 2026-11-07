#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool valid;            // false until NTP, the phone or the RTC has set the time
    int h, m, s, ms;       // h is 0..23
    int day, mon, year;    // mon 1..12
    int wday;              // 0 = Sunday
    uint32_t minute_ms;    // ms elapsed in the current minute
} clock_now_t;

// Updated once per frame by the main loop
extern clock_now_t g_now;

void clock_init(void);         // time zone (Europe/Madrid) and, on boards with an RTC, the saved time
void clock_start_sntp(void);   // call once the network stack is up
void clock_update(void);       // refreshes g_now
void clock_set_epoch(int64_t sec, int ms);   // also stored in the RTC, if the board has one

// Helpers for faces
int clock_display_hour(void);                  // honours the 12/24 h setting
void clock_hhmm(char *buf, bool colon);        // "HH:MM" or "HH MM"; "--:--" if invalid
const char *clock_wday_name(int wday);         // "LUN"..."DOM"
const char *clock_month_name(int mon);         // "ENE"..."DIC"
