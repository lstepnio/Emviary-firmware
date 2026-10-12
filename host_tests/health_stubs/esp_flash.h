#pragma once
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
extern void *esp_flash_default_chip;
esp_err_t esp_flash_read(void *chip, void *buffer, uint32_t address, uint32_t size);
