#include "console.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "app.h"
#include "batlog.h"
#include "intro.h"
#include "voice.h"
#include "app_plug.h"
#include "control.h"
#include "esp_log.h"
#include "esp_pm.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "console";
static char s_line[160];
static size_t s_len;

void console_start(void)
{
    int fd = fileno(stdin);
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
}

static void run_line(char *line)
{
    // The "wifi" command takes the network on separate lines; on the console it is typed as
    // "wifi <ssid> <password>" (so no spaces in the network name)
    // "wifi 0" / "wifi 1" (one word) is the on/off setting, as over Bluetooth
    if (strncmp(line, "wifi ", 5) == 0) {
        char *pass = strchr(line + 5, ' ');
        if (pass) {
            *pass = '\n';
            line[4] = '\n';
        }
    }
    ESP_LOGI(TAG, "> %s", strncmp(line, "wifi\n", 5) == 0 ? "wifi ***" : line);
    if (strcmp(line, "status") == 0) {  // for the tests: the state as JSON, whatever link is in use
        char *buf = malloc(1536);
        if (buf) {
            app_status_json(buf, 1536);
            printf("status %s\n", buf);
            free(buf);
        }
        return;
    }
    // Power measurements: which locks keep the chip awake and how long it spent in each mode
    if (strcmp(line, "pm") == 0) {
        esp_pm_dump_locks(stdout);
        return;
    }
    if (strcmp(line, "batlog") == 0) {  // the battery log (batlog.c) without Wi-Fi
        static char buf[192 * 32 + 64];
        batlog_csv(buf, sizeof(buf));
        printf("%s\nbatlog end\n", buf);
        return;
    }
    if (strncmp(line, "introshot ", 10) == 0) {  // one frame of the animation, held 3 s for /screenshot (the watchdog bites at 8)
        g_app.freeze_req = true;
        for (int i = 0; i < 100 && !g_app.frozen; i++) vTaskDelay(pdMS_TO_TICKS(5));
        intro_frame((uint32_t)atoi(line + 10));
        printf("introshot: drawn\n");
        vTaskDelay(pdMS_TO_TICKS(3000));
        g_app.freeze_req = false;
        return;
    }
    if (strcmp(line, "intro") == 0) {  // see the power-on animation without cutting the power
        intro_request();
        esp_restart();
    }
    if (strcmp(line, "why") == 0) {  // why the watch last restarted (esp_reset_reason)
        static const char *const R[] = {"unknown", "power-on", "external", "software", "panic", "int-wdt", "task-wdt",
                                        "wdt", "deep-sleep", "brownout", "sdio", "usb", "jtag", "efuse", "pwr-glitch", "cpu-lockup"};
        int r = esp_reset_reason();
        printf("why: %s (%d)\n", r >= 0 && r < (int)(sizeof(R) / sizeof(R[0])) ? R[r] : "?", r);
        return;
    }
    if (strncmp(line, "voicetest ", 10) == 0) {  // as if BOOT were held this many ms (voice commands)
        voice_begin();
        vTaskDelay(pdMS_TO_TICKS(atoi(line + 10)));
        voice_end();
        return;
    }
    if (strcmp(line, "plugtest") == 0) {  // the USB cable notice, without unplugging
        plug_notice();
        return;
    }
    if (strcmp(line, "mictest") == 0) {  // microphone levels for 3 s
        voice_mictest();
        return;
    }
    if (strcmp(line, "sdinfo") == 0) {
        void sd_info(void);
        sd_info();
        return;
    }
    if (strcmp(line, "batoff test") == 0) {  // the empty-battery screen, without powering off
        power_off_now(false);
        return;
    }
    app_command(line);
}

void console_poll(void)
{
    char buf[64];
    int n;
    while ((n = read(fileno(stdin), buf, sizeof(buf))) > 0) {
        for (int i = 0; i < n; i++) {
            char ch = buf[i];
            if (ch == '\r') continue;
            if (ch != '\n') {
                if (s_len < sizeof(s_line) - 1) s_line[s_len++] = ch;
                continue;
            }
            s_line[s_len] = 0;
            s_len = 0;
            if (s_line[0]) run_line(s_line);
        }
    }
}
