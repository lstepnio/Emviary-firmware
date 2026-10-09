#pragma once
#include <stdint.h>
#include <pthread.h>
typedef uint32_t TickType_t;
typedef int BaseType_t;
typedef pthread_mutex_t portMUX_TYPE;
#define pdTRUE 1
#define pdFALSE 0
#define portMAX_DELAY UINT32_MAX
#define pdMS_TO_TICKS(ms) (ms)
#define portMUX_INITIALIZER_UNLOCKED PTHREAD_MUTEX_INITIALIZER
void test_navigation_enter(portMUX_TYPE *mutex);
void test_navigation_exit(portMUX_TYPE *mutex);
#define portENTER_CRITICAL(m) test_navigation_enter(m)
#define portEXIT_CRITICAL(m) test_navigation_exit(m)
