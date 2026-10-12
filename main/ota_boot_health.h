#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
bool ota_bootloader_rollback_ready(void);
esp_err_t ota_confirm_healthy_boot(void);

void ota_bootloader_rejected_version(char *version, size_t size);
