#include "console.h"
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "control.h"
#include "esp_log.h"

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
    if (strncmp(line, "wifi ", 5) == 0) {
        char *pass = strchr(line + 5, ' ');
        if (pass) *pass = '\n';
        line[4] = '\n';
    }
    ESP_LOGI(TAG, "> %s", strncmp(line, "wifi\n", 5) == 0 ? "wifi ***" : line);
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
