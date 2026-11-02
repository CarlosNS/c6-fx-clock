#include "weather.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "app.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"

#define REFRESH_MS (10 * 60 * 1000)
#define RETRY_MS (60 * 1000)

static const char *TAG = "weather";
volatile weather_t g_wx;
weather_loc_t g_wx_loc = {"Sevilla", 37.3886f, -5.9823f};
static TaskHandle_t s_task;
static char s_body[1600];
static int s_len;

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

static esp_err_t on_http(esp_http_client_event_t *e)
{
    if (e->event_id == HTTP_EVENT_ON_DATA) {
        int n = e->data_len;
        if (s_len + n >= (int)sizeof(s_body)) n = sizeof(s_body) - 1 - s_len;
        if (n > 0) {
            memcpy(s_body + s_len, e->data, n);
            s_len += n;
            s_body[s_len] = 0;
        }
    }
    return ESP_OK;
}

// Number after "key": inside the section that starts at `section` (e.g. "\"current\":{")
static bool json_num(const char *section, const char *key, float *out)
{
    const char *s = strstr(s_body, section);
    if (!s) return false;
    char pat[40];
    snprintf(pat, sizeof(pat), "\"%s\":", key);
    const char *p = strstr(s, pat);
    if (!p) return false;
    p += strlen(pat);
    if (*p == '[') p++;
    char *end;
    *out = strtof(p, &end);
    return end != p;
}

static int round10(float v) { return (int)(v * 10 + (v < 0 ? -0.5f : 0.5f)); }

static bool fetch(void)
{
    char url[300];
    snprintf(url, sizeof(url),
             "http://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
             "&current=temperature_2m,relative_humidity_2m,weather_code,wind_speed_10m,is_day"
             "&daily=temperature_2m_max,temperature_2m_min&timezone=auto&forecast_days=1",
             g_wx_loc.lat, g_wx_loc.lon);
    esp_http_client_config_t cfg = {.url = url, .event_handler = on_http, .timeout_ms = 8000, .buffer_size = 1024};
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) return false;
    s_len = 0;
    s_body[0] = 0;
    esp_err_t err = esp_http_client_perform(c);
    int status = esp_http_client_get_status_code(c);
    esp_http_client_cleanup(c);
    if (err != ESP_OK || status != 200) {
        ESP_LOGW(TAG, "fetch failed: %s, HTTP %d", esp_err_to_name(err), status);
        return false;
    }
    float t, code, hum, wind, day, hi, lo;
    if (!json_num("\"current\":{", "temperature_2m", &t) || !json_num("\"current\":{", "weather_code", &code) ||
        !json_num("\"current\":{", "relative_humidity_2m", &hum) || !json_num("\"current\":{", "wind_speed_10m", &wind) ||
        !json_num("\"current\":{", "is_day", &day) || !json_num("\"daily\":{", "temperature_2m_max", &hi) ||
        !json_num("\"daily\":{", "temperature_2m_min", &lo)) {
        ESP_LOGW(TAG, "unexpected response");
        return false;
    }
    g_wx.temp10 = round10(t);
    g_wx.code = (int)code;
    g_wx.hum = (int)hum;
    g_wx.wind = (int)(wind + 0.5f);
    g_wx.hi10 = round10(hi);
    g_wx.lo10 = round10(lo);
    g_wx.is_day = day > 0.5f;
    g_wx.from_web = false;
    g_wx.stamp = now_ms();
    g_wx.valid = true;
    ESP_LOGI(TAG, "%s: %.1f C, code %d", g_wx_loc.name, t, g_wx.code);
    return true;
}

static void weather_task(void *arg)
{
    for (;;) {
        uint32_t wait = 5000;  // until the station has an IP
        if (g_app.sta_ip[0]) {
            wait = RETRY_MS;
            // A fresh update pushed by the web page counts too: don't hammer a network without internet
            if (g_wx.valid && g_wx.from_web && now_ms() - g_wx.stamp < REFRESH_MS) wait = REFRESH_MS - (now_ms() - g_wx.stamp);
            else if (fetch()) wait = REFRESH_MS;
        }
        // Woken early when the location changes
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(wait));
    }
}

void weather_init(void)
{
    nvs_handle_t h;
    if (nvs_open("clock", NVS_READONLY, &h) == ESP_OK) {
        size_t n = sizeof(g_wx_loc);
        weather_loc_t loc;
        if (nvs_get_blob(h, "wloc", &loc, &n) == ESP_OK && n == sizeof(loc)) {
            loc.name[sizeof(loc.name) - 1] = 0;
            g_wx_loc = loc;
        }
        nvs_close(h);
    }
    xTaskCreate(weather_task, "weather", 4096, NULL, 3, &s_task);
}

void weather_set_location(float lat, float lon, const char *name)
{
    if (lat < -90 || lat > 90 || lon < -180 || lon > 180) return;
    weather_loc_t loc = {.lat = lat, .lon = lon};
    strlcpy(loc.name, name, sizeof(loc.name));
    for (char *c = loc.name; *c; c++)  // goes into JSON and the ASCII screen font
        if (*c == '"' || *c == '\\' || (unsigned char)*c < 32 || (unsigned char)*c > 126) *c = ' ';
    g_wx_loc = loc;
    g_wx.valid = false;
    nvs_handle_t h;
    if (nvs_open("clock", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_blob(h, "wloc", &loc, sizeof(loc));
        nvs_commit(h);
        nvs_close(h);
    }
    if (s_task) xTaskNotifyGive(s_task);
}

void weather_set_from_web(int temp10, int code, int hum, int wind, int hi10, int lo10, bool is_day)
{
    // The board's own data wins while it is fresh
    if (g_wx.valid && !g_wx.from_web && now_ms() - g_wx.stamp < REFRESH_MS) return;
    g_wx.temp10 = temp10;
    g_wx.code = code;
    g_wx.hum = hum;
    g_wx.wind = wind;
    g_wx.hi10 = hi10;
    g_wx.lo10 = lo10;
    g_wx.is_day = is_day;
    g_wx.from_web = true;
    g_wx.stamp = now_ms();
    g_wx.valid = true;
}

int weather_age_s(void)
{
    return g_wx.valid ? (int)((now_ms() - g_wx.stamp) / 1000) : -1;
}

const char *weather_desc(int c)
{
    if (c == 0) return "Despejado";
    if (c <= 2) return "Poco nuboso";
    if (c == 3) return "Nublado";
    if (c == 45 || c == 48) return "Niebla";
    if (c >= 51 && c <= 57) return "Llovizna";
    if (c >= 61 && c <= 67) return "Lluvia";
    if (c >= 71 && c <= 77) return "Nieve";
    if (c >= 80 && c <= 82) return "Chubascos";
    if (c == 85 || c == 86) return "Nevadas";
    if (c >= 95) return "Tormenta";
    return "--";
}
