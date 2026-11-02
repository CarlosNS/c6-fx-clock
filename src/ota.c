#include "ota.h"
#include <string.h>
#include "esp_app_desc.h"
#include "esp_app_format.h"
#include "esp_log.h"
#include "esp_ota_ops.h"

static const char *TAG = "ota";
static esp_ota_handle_t s_handle;
static const esp_partition_t *s_part;
static size_t s_total, s_done;
static volatile int s_progress = -1;

size_t ota_max_size(void)
{
    const esp_partition_t *p = esp_ota_get_next_update_partition(NULL);
    return p ? p->size : 0;
}

const char *ota_running_label(void)
{
    const esp_partition_t *p = esp_ota_get_running_partition();
    return p ? p->label : "?";
}

int ota_progress(void) { return s_progress; }

esp_err_t ota_begin(size_t total)
{
    if (s_progress >= 0) return ESP_ERR_INVALID_STATE;
    s_part = esp_ota_get_next_update_partition(NULL);
    if (!s_part) return ESP_ERR_NOT_FOUND;
    if (total == 0 || total > s_part->size) return ESP_ERR_INVALID_SIZE;
    esp_err_t err = esp_ota_begin(s_part, OTA_WITH_SEQUENTIAL_WRITES, &s_handle);
    if (err != ESP_OK) return err;
    s_total = total;
    s_done = 0;
    s_progress = 0;
    ESP_LOGI(TAG, "Updating %s (%u bytes)", s_part->label, (unsigned)total);
    return ESP_OK;
}

void ota_abort(void)
{
    if (s_progress < 0) return;
    esp_ota_abort(s_handle);
    s_progress = -1;
    ESP_LOGW(TAG, "Aborted");
}

// The app description sits right after the image header and the first segment header
static bool same_project(const uint8_t *p, size_t len)
{
    size_t off = sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t);
    if (len < off + sizeof(esp_app_desc_t) || p[0] != ESP_IMAGE_HEADER_MAGIC) return false;
    esp_app_desc_t d;
    memcpy(&d, p + off, sizeof(d));
    return d.magic_word == ESP_APP_DESC_MAGIC_WORD &&
           strncmp(d.project_name, esp_app_get_description()->project_name, sizeof(d.project_name)) == 0;
}

esp_err_t ota_write(const void *data, size_t len, const char **msg)
{
    if (s_progress < 0) {
        *msg = "No hay ninguna actualización en curso";
        return ESP_ERR_INVALID_STATE;
    }
    if (s_done == 0 && !same_project(data, len)) {
        ota_abort();
        *msg = "Ese archivo no es un firmware de este reloj (usa c6-fx-clock-ota.bin)";
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = esp_ota_write(s_handle, data, len);
    if (err != ESP_OK) {
        ota_abort();
        *msg = "Error al escribir en la memoria";
        return err;
    }
    s_done += len;
    int p = (int)(s_done * 100 / s_total);
    s_progress = p > 99 ? 99 : p;
    return ESP_OK;
}

esp_err_t ota_finish(const char **msg)
{
    esp_err_t err = esp_ota_end(s_handle);  // verifies the image
    if (err == ESP_OK) err = esp_ota_set_boot_partition(s_part);
    if (err != ESP_OK) {
        s_progress = -1;
        *msg = err == ESP_ERR_OTA_VALIDATE_FAILED ? "El firmware está dañado o incompleto" : "No se pudo activar el firmware nuevo";
        ESP_LOGE(TAG, "Finish failed: %s", esp_err_to_name(err));
        return err;
    }
    s_progress = 100;
    ESP_LOGI(TAG, "Done, next boot from %s", s_part->label);
    return ESP_OK;
}

void ota_confirm_app(void)
{
    esp_ota_img_states_t st;
    if (esp_ota_get_state_partition(esp_ota_get_running_partition(), &st) == ESP_OK && st == ESP_OTA_IMG_PENDING_VERIFY) {
        esp_ota_mark_app_valid_cancel_rollback();
        ESP_LOGI(TAG, "New firmware confirmed");
    }
}
