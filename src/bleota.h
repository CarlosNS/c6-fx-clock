#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Firmware update from the phone over Bluetooth: the app downloads the image (it has the internet,
// the watch may not) and streams it to the watch, which writes it into the other OTA slot while it
// keeps running, checks it and reboots into it. No Wi-Fi involved.
//
//   phone -> "ota begin <size> <sha256 hex> <version>"   (command characteristic)
//   watch -> {"t":"ota","st":"ready","win":<bytes>}  or  {"t":"ota","st":"err","msg":"..."}
//   phone -> data packets on the OTA characteristic (write without response):
//            <offset, u32 little-endian><bytes>, in order, never more than "win" bytes past the last ack
//   watch -> {"t":"ota","st":"ack","off":<bytes written to flash>} every 16 KB and at the end
//   watch -> {"t":"ota","st":"done"} (verified and set to boot; it reboots 2 s later) or "err"
//   phone -> "ota abort" cancels; a dropped link cancels too. The old firmware stays on any failure.

// "ota begin ..." / "ota abort" (from the Bluetooth worker); false if it is not an ota command
bool bleota_command(const char *cmd);
// One data packet, from the Bluetooth host task (never blocks); returns 0 or an ATT error
int bleota_data(const uint8_t *p, size_t n);
// The link dropped
void bleota_link_lost(void);
