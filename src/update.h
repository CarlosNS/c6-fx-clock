#pragma once
#include <stdbool.h>
#include <stddef.h>

// Updates over the internet (docs/PLAN-bluetooth.md, 3.4). The phone (or the web page) sends
// "update apply <manifest url>": the URL is stored and the clock reboots into UPDATE mode, which
// joins the stored Wi-Fi, reads the manifest, downloads the image into the other OTA slot checking
// project, size and SHA-256, stores the outcome and reboots into the mode it came from.
//
// Manifest: {..., "boards":{"s3amoled":{"version":"3.0.0","url":"https://.../marea-ota.bin","size":N,"sha256":"<hex>"}}}
// Only https:// is accepted, except http:// to a private LAN address (the PC test server).

// Validates and stores the manifest URL; false (and *msg, Spanish) if it is not acceptable.
bool update_request(const char *url, const char **msg);
// UPDATE boot mode. Never returns.
void update_run(void);
// Other modes, at boot: an update cut short by a reset is recorded as "interrupted".
void update_boot_check(void);
// The last attempt as JSON: {"state":"updated","at":3,"from":"2.0.0","to":"2.1.0"}. "at" grows
// with every attempt, so the app can tell a new result from an old one. state is one of none,
// running, interrupted, updated, up_to_date, no_wifi, no_internet, bad_manifest, bad_image, timeout.
size_t update_result_json(char *buf, size_t size);
// Stores an outcome (also for the Bluetooth update, bleota.c); new_attempt bumps "at"
void update_record(const char *state, const char *from, const char *to, bool new_attempt);
