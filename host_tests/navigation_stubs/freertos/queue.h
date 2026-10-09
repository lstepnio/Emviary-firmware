#pragma once
#include <stddef.h>
#include "freertos/FreeRTOS.h"
typedef void *QueueHandle_t;
QueueHandle_t xQueueCreate(unsigned length, unsigned item_size);
BaseType_t xQueueOverwrite(QueueHandle_t queue, const void *item);
BaseType_t xQueueReceive(QueueHandle_t queue, void *item, TickType_t timeout);
BaseType_t xQueuePeek(QueueHandle_t queue, void *item, TickType_t timeout);
