#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cJSON.h"
#include "esp_err.h"
#define OFFLINE_HEADER_MAX 4096
#define OFFLINE_PATCH_MAX (64u * 1024u)
typedef struct {
    unsigned x, y, w, h;
    size_t length;
    uint8_t *pixels;
} offline_patch_t;
/* Admission is called only after a successful online panel publication. */
esp_err_t offline_cache_admit(const char *path, const char *header);
esp_err_t offline_cache_rotate(bool previous);
bool offline_cache_available(void);
bool offline_cache_is_offline(void);
bool offline_cache_last_rotation_ok(void);
void offline_cache_mark_cloud_success(void);
void offline_cache_add_metrics(cJSON *object);
void offline_cache_status(char *out, size_t capacity);
