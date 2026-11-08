#include "console.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "app.h"
#include "batlog.h"
#include "control.h"
#include "esp_log.h"
#include "esp_pm.h"
#include "esp_timer.h"

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
