#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#define FS_MOUNT_POINT "/tmp/emviary-gift-host-test"
#define NVS_READONLY 0
#define NVS_READWRITE 1
#define pdMS_TO_TICKS(n) (n)
typedef int nvs_handle_t;
esp_err_t nvs_open(const char *, int, nvs_handle_t *);
esp_err_t nvs_get_blob(nvs_handle_t, const char *, void *, size_t *);
esp_err_t nvs_set_blob(nvs_handle_t, const char *, const void *, size_t);
esp_err_t nvs_commit(nvs_handle_t);
void nvs_close(nvs_handle_t);
bool storage_has_persistent_storage(void);
const char *config_manager_get_image_url(void);
void config_manager_set_image_etag(const char *);
esp_err_t display_manager_show_image(const char *);
esp_err_t display_manager_restore_artwork(void);
bool recovery_image_sha256(const char *, char[65]);
int GUI_ValidateEPDGZ(const char *);
int64_t esp_timer_get_time(void);
bool utils_image_operation_begin(int);
void utils_image_operation_end(void);

typedef void *TaskHandle_t;
TaskHandle_t xTaskGetCurrentTaskHandle(void);
esp_err_t fetch_and_display_image_from_url(const char *, bool *);

void emviary_navigation_discard_pending(void);
