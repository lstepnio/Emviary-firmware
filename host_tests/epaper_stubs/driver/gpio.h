#pragma once
#include <stdint.h>
#include "esp_err.h"
typedef int gpio_num_t;
typedef struct { int mode; uint64_t pin_bit_mask; int pull_up_en; } gpio_config_t;
#define GPIO_MODE_OUTPUT 1
#define GPIO_MODE_INPUT 2
#define GPIO_PULLUP_ENABLE 1
esp_err_t gpio_set_level(int pin, int level);
int gpio_get_level(int pin);
esp_err_t gpio_config(const gpio_config_t *cfg);
esp_err_t gpio_hold_dis(int pin);
esp_err_t gpio_hold_en(int pin);
void gpio_deep_sleep_hold_en(void);
