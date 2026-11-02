#pragma once
#include <stdbool.h>
#include <stdint.h>

// Current weather for the configured location (Open-Meteo, no API key). The board fetches it
// itself over plain HTTP every few minutes; if it has no internet, the web page fetches it from
// the phone and pushes it with the "wx" command.
typedef struct {
    bool valid;
    int temp10;      // current temperature, tenths of °C
    int code;        // WMO weather code
    int hum;         // relative humidity, %
    int wind;        // km/h
    int hi10, lo10;  // today's max / min, tenths of °C
    bool is_day;
    uint32_t stamp;  // ms timestamp of the last update
    bool from_web;   // last update came from the web page instead of the board
} weather_t;

extern volatile weather_t g_wx;

typedef struct {
    char name[32];  // ASCII
    float lat, lon;
} weather_loc_t;
extern weather_loc_t g_wx_loc;

void weather_init(void);  // loads the location from NVS and starts the fetch task
void weather_set_location(float lat, float lon, const char *name);
void weather_set_from_web(int temp10, int code, int hum, int wind, int hi10, int lo10, bool is_day);
int weather_age_s(void);  // seconds since the last update, -1 if never
// Short Spanish description of a WMO code ("Despejado", "Lluvia"...)
const char *weather_desc(int code);
