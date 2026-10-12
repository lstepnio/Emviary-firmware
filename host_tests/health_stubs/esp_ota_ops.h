#pragma once
#include "esp_err.h"
typedef struct { int id; } esp_partition_t;
typedef enum { ESP_OTA_IMG_VALID, ESP_OTA_IMG_PENDING_VERIFY } esp_ota_img_states_t;
const esp_partition_t *esp_ota_get_running_partition(void);
esp_err_t esp_ota_get_state_partition(const esp_partition_t *partition, esp_ota_img_states_t *state);
esp_err_t esp_ota_mark_app_invalid_rollback_and_reboot(void);
esp_err_t esp_ota_mark_app_valid_cancel_rollback(void);
