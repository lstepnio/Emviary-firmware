#pragma once
#include <stdint.h>
#include "esp_err.h"
typedef int nvs_handle_t;
#define NVS_READONLY 0
#define NVS_READWRITE 1
#define ESP_ERR_NVS_NOT_FOUND 10
esp_err_t nvs_open(const char *, int, nvs_handle_t *);
esp_err_t nvs_get_i64(nvs_handle_t, const char *, int64_t *);
esp_err_t nvs_set_i64(nvs_handle_t, const char *, int64_t);
esp_err_t nvs_erase_key(nvs_handle_t, const char *);
esp_err_t nvs_commit(nvs_handle_t);
void nvs_close(nvs_handle_t);
