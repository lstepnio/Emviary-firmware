#include <assert.h>
#include <stdio.h>
#include "periodic_tasks.h"
#include "esp_timer.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
static void (*timer_callback)(void *);
static int callbacks, notifications, commits, locked, task_fail, deleted_tasks, deleted_mutexes;
const char *esp_err_to_name(esp_err_t err) { return "mock"; }
SemaphoreHandle_t xSemaphoreCreateMutex(void) { return (void *)1; }
void vSemaphoreDelete(SemaphoreHandle_t sem) { deleted_mutexes++; }
int xSemaphoreTake(SemaphoreHandle_t sem, uint32_t ticks) {
    assert(ticks == 15000 || ticks == 0); if (locked) return 0; locked = 1; return 1;
}
void xSemaphoreGive(SemaphoreHandle_t sem) { assert(locked); locked = 0; }
int xTaskCreate(void (*fn)(void *), const char *name, unsigned stack, void *arg,
                unsigned priority, TaskHandle_t *handle) {
    assert(stack == 4096); if (task_fail) return 0; *handle = (void *)2; return pdPASS;
}
void vTaskDelete(TaskHandle_t task) { deleted_tasks++; }
unsigned ulTaskNotifyTake(int clear, uint32_t ticks) { return 1; }
void xTaskNotifyGive(TaskHandle_t task) { assert(task == (void *)2); notifications++; }
esp_err_t esp_timer_create(const esp_timer_create_args_t *args, esp_timer_handle_t *handle) {
    timer_callback = args->callback; *handle = (void *)3; return ESP_OK;
}
esp_err_t esp_timer_start_periodic(esp_timer_handle_t timer, uint64_t interval) {
    assert(interval == 3600000000ULL); return ESP_OK;
}
esp_err_t esp_timer_delete(esp_timer_handle_t timer) { return ESP_OK; }
esp_err_t nvs_open(const char *name, int mode, nvs_handle_t *handle) { *handle=1; return ESP_OK; }
esp_err_t nvs_get_i64(nvs_handle_t handle, const char *key, int64_t *value) { return ESP_ERR_NVS_NOT_FOUND; }
esp_err_t nvs_set_i64(nvs_handle_t handle, const char *key, int64_t value) { return ESP_OK; }
esp_err_t nvs_erase_key(nvs_handle_t handle, const char *key) { return ESP_OK; }
esp_err_t nvs_commit(nvs_handle_t handle) { commits++; return ESP_OK; }
void nvs_close(nvs_handle_t handle) {}
static esp_err_t callback(void) {
    callbacks++;
    assert(!periodic_tasks_try_suspend_for_sleep());
    // Simulate a duplicate API check while the first callback owns the mutex.
    assert(periodic_tasks_check_and_run() == ESP_ERR_TIMEOUT);
    return ESP_OK;
}
int main(void) {
    assert(periodic_tasks_check_and_run() == ESP_ERR_INVALID_STATE);
    task_fail = 1;
    assert(periodic_tasks_init() == ESP_ERR_NO_MEM);
    assert(deleted_mutexes == 1);
    task_fail = 0;
    assert(periodic_tasks_init() == ESP_OK);
    assert(periodic_tasks_register("sntp_sync", callback, 86400) == ESP_OK);
    timer_callback(NULL);
    assert(notifications == 1 && callbacks == 0 && commits == 0);
    assert(periodic_tasks_check_and_run() == ESP_OK);
    assert(callbacks == 1 && commits == 1 && locked == 0);
    assert(periodic_tasks_check_and_run() == ESP_OK);
    assert(callbacks == 2 && locked == 0);
    assert(periodic_tasks_try_suspend_for_sleep());
    assert(periodic_tasks_check_and_run() == ESP_ERR_TIMEOUT);
    assert(callbacks == 2);
    puts("periodic dispatch, serialization, sleep barrier, allocation failure tests passed");
    return 0;
}
