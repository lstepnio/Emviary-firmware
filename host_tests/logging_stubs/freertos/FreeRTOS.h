#pragma once
#include <stddef.h>
typedef unsigned TickType_t;
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(x) ((void) (x))
#define portEXIT_CRITICAL(x) ((void) (x))
#define pdMS_TO_TICKS(x) (x)
#define portMAX_DELAY 0xffffffff
#define pdPASS 1
#define tskIDLE_PRIORITY 0
static inline int xPortInIsrContext(void)
{
    return 0;
}
