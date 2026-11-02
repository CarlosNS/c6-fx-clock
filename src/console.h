#pragma once

// Serial transport: each line received over the USB port is passed to app_command(), the same as a
// message from the web page or (later) Bluetooth. Lets the PC tests drive the clock in any boot
// mode, even with every radio off.
//
// No driver and no task: the main loop polls the port (non-blocking reads of the default USB
// serial VFS), which costs no RAM. The USB driver would take ~8 KB, too much in Wi-Fi mode.
void console_start(void);
// Reads whatever arrived and runs complete lines. Call often (every frame).
void console_poll(void);
