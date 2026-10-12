#pragma once
#include "esp_err.h"
typedef struct { int id; } esp_partition_t;
typedef enum {
    ESP_OTA_IMG_NEW = 0, ESP_OTA_IMG_PENDING_VERIFY = 1, ESP_OTA_IMG_VALID = 2,
    ESP_OTA_IMG_INVALID = 3, ESP_OTA_IMG_ABORTED = 4
} esp_ota_img_states_t;
typedef struct { char version[32]; } esp_app_desc_t;
const esp_partition_t *esp_ota_get_running_partition(void);
const esp_partition_t *esp_ota_get_next_update_partition(const esp_partition_t *partition);
esp_err_t esp_ota_get_partition_description(const esp_partition_t *partition,
                                         esp_app_desc_t *description);
esp_err_t esp_ota_get_state_partition(const esp_partition_t *partition, esp_ota_img_states_t *state);
esp_err_t esp_ota_mark_app_invalid_rollback_and_reboot(void);
esp_err_t esp_ota_mark_app_valid_cancel_rollback(void);
