#pragma once
#include <stdarg.h>
typedef int (*vprintf_like_t)(const char *, va_list);
typedef enum { ESP_LOG_NONE, ESP_LOG_ERROR, ESP_LOG_WARN, ESP_LOG_INFO } esp_log_level_t;
void esp_log_level_set(const char *, esp_log_level_t);
vprintf_like_t esp_log_set_vprintf(vprintf_like_t);
#define ESP_LOGE(...) ((void) 0)
#define ESP_LOGW(...) ((void) 0)
#define ESP_LOGI(...) ((void) 0)
