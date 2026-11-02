#pragma once
#include <stdbool.h>

// Bluetooth transport (docs/PLAN-bluetooth.md, 3.1-3.2): a NimBLE peripheral with the clock service
//   CMD (write)   one text command, same syntax as the web page (control.h)
//   EVT (notify)  JSON messages split in frames: 1 header byte (bit7 last, bit6 first, bits0-5 id)
// plus Device Information with the firmware version. Requests answered here: "get info",
// "get status", "get faces", "sync done"; everything else goes to app_command().
void ble_start(void);
bool ble_connected(void);
// Advertised name ("C6 Clock XXXX"); empty until the stack is up
const char *ble_name(void);
