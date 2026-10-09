#pragma once
#include <stdint.h>
typedef void *SemaphoreHandle_t;
SemaphoreHandle_t xSemaphoreCreateMutex(void);
void vSemaphoreDelete(SemaphoreHandle_t);
int xSemaphoreTake(SemaphoreHandle_t, uint32_t);
void xSemaphoreGive(SemaphoreHandle_t);
