#pragma once
#include <stdint.h>
typedef void *TaskHandle_t;
int xTaskCreate(void (*)(void *), const char *, unsigned, void *, unsigned, TaskHandle_t *);
void vTaskDelete(TaskHandle_t);
unsigned ulTaskNotifyTake(int, uint32_t);
void xTaskNotifyGive(TaskHandle_t);
