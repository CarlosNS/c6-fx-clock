#pragma once
#include <stddef.h>

// Transport-independent control layer. The web page (WebSocket) and, later, a Bluetooth link
// both just pass text commands in and read the status JSON back.
//
// Commands (one per message):
//   <setting> <int>          any key from settings.c, e.g. "bri 80", "fx 3", "chue 200"
//   night <on> <from> <to> <bri>
//   time <epoch_s> <ms>      set the clock from the phone
//   loc <lat> <lon> <name>   weather location
//   wx <t10> <code> <hum> <wind> <hi10> <lo10> <is_day>   weather pushed by the phone
//   wifi\n<ssid>\n<password> join a home network
//   drag <x> <y> <dx> <dy> / up                          touch pad
//   dbgtime <h> <m>          preview another time of day (-1 = real time)
//   mode ble|wifi|update     reboot into that mode (bootmode.h)
void app_command(const char *cmd);

// Full status as a JSON object; returns its length
size_t app_status_json(char *buf, size_t size);

// Returns and clears the drag delta accumulated since the last call
void app_take_drag(int *dx, int *dy);

// Copies s into out as the body of a JSON string (quotes and backslashes escaped, control chars dropped)
void json_escape(char *out, size_t size, const char *s);
