#pragma once

// Boot modes. Wi-Fi and Bluetooth always run together; the only special boot is UPDATE. BLE and WIFI
// are kept as names because older apps, the tests and the status JSON use them: both mean "normal".
typedef enum {
    BOOT_BLE,     // normal
    BOOT_WIFI,    // normal (same as BLE)
    BOOT_UPDATE,  // one-shot: Wi-Fi client, fetch and apply an update, then back to the previous mode
} boot_mode_t;

#define BOOT_DEFAULT BOOT_BLE

// Reads the mode stored in NVS. When it is UPDATE, the stored mode is reset to the return mode
// *before* anything else runs, so a crash during the update can never trap the clock in it.
boot_mode_t boot_mode_init(void);
boot_mode_t boot_mode(void);
const char *boot_mode_name(boot_mode_t m);
boot_mode_t boot_mode_from_name(const char *name);  // -1 if unknown
// Mode the clock goes back to after an UPDATE boot
boot_mode_t boot_mode_return(void);
// Stores the mode and restarts (does not return). Entering UPDATE remembers the current mode.
void boot_mode_switch(boot_mode_t m);
