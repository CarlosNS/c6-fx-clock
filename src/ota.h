#pragma once
#include <stddef.h>
#include "esp_err.h"

// Over-the-air update into the inactive app slot. Transport-independent: the web server streams
// the uploaded file through it, and a Bluetooth link could do the same later.
//
// Safety: the first chunk must be an app image of this same project (checked before anything is
// written), esp_ota_end() verifies the checksum, and the new firmware boots in "pending verify"
// state: if it does not confirm itself (ota_confirm_app) the bootloader rolls back on the next reset.

esp_err_t ota_begin(size_t total_size);
// Feeds the next chunk; on error the update is aborted and *msg explains why (Spanish, for the user)
esp_err_t ota_write(const void *data, size_t len, const char **msg);
// Verifies the image and selects it for the next boot (the caller then restarts)
esp_err_t ota_finish(const char **msg);
void ota_abort(void);

int ota_progress(void);              // -1 when idle, 0..100 during an update
size_t ota_max_size(void);           // size of the slot an update would go into
const char *ota_running_label(void); // "ota_0" / "ota_1"
// Marks the running firmware as good (cancels the pending rollback). Call once it works.
void ota_confirm_app(void);
