#pragma once

// Boot modes (docs/PLAN-bluetooth.md, section 2). On boards where Wi-Fi and Bluetooth don't fit
// together (BOARD_RADIO_EXCLUSIVE), each radio gets its own mode and switching is a controlled reboot.
typedef enum {
    BOOT_BLE,     // daily use: Bluetooth only, synced by the phone
    BOOT_WIFI,    // fallback: the web control page over Wi-Fi (as in 2.0.0)
    BOOT_UPDATE,  // one-shot: Wi-Fi client, fetch and apply an update, then back to the previous mode
} boot_mode_t;

// Mode until the Bluetooth transport exists (phase 2 switches it to BOOT_BLE)
#define BOOT_DEFAULT BOOT_WIFI

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
