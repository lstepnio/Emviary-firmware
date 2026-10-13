#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "debug_log.h"
#include "esp_log.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static bool enabled, persistent = true;
static unsigned allocations, tasks, hooks;
static esp_log_level_t level = ESP_LOG_INFO;
static vprintf_like_t hook;
bool config_manager_get_debug_log_enabled(void)
{
    return enabled;
}
void config_manager_set_debug_log_enabled(bool value)
{
    enabled = value;
}
bool storage_has_persistent_storage(void)
{
    return persistent;
}
void esp_log_level_set(const char *tag, esp_log_level_t value)
{
    assert(!strcmp(tag, "*"));
    level = value;
}
vprintf_like_t esp_log_set_vprintf(vprintf_like_t value)
{
    vprintf_like_t old = hook;
    hook = value;
    ++hooks;
    return old;
}
SemaphoreHandle_t xSemaphoreCreateMutex(void)
{
    ++allocations;
    return (void *) 1;
}
SemaphoreHandle_t xSemaphoreCreateBinary(void)
{
    ++allocations;
    return (void *) 2;
}
int xSemaphoreTake(SemaphoreHandle_t h, TickType_t t)
{
    assert(h);
    return 1;
}
int xSemaphoreGive(SemaphoreHandle_t h)
{
    assert(h);
    return 1;
}
int xTaskCreate(void (*f)(void *), const char *n, unsigned s, void *a, unsigned p, TaskHandle_t *h)
{
    ++tasks;
    *h = NULL;
    return pdPASS;
}
void vTaskDelay(TickType_t t) {}
void vTaskDelete(TaskHandle_t t) {}
TickType_t xTaskGetTickCount(void)
{
    return 0;
}

static void emit(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    assert(hook);
    hook(fmt, args);
    va_end(args);
}
int main(int argc, char **argv)
{
    assert(argc == 2);
    if (!strcmp(argv[1], "disabled")) {
        assert(debug_log_init() == ESP_OK);
        debug_log_set_enabled(false);
        debug_log_flush();
        assert(level == ESP_LOG_NONE && allocations == 0 && tasks == 0 && hooks == 0);
        assert(access(debug_log_current_path(), F_OK) != 0);
    } else if (!strcmp(argv[1], "clear_disabled")) {
        mkdir(FS_MOUNT_POINT, 0700);
        FILE *f = fopen(debug_log_current_path(), "w");
        assert(f);
        fputs("old diagnostic\n", f);
        fclose(f);
        assert(debug_log_init() == ESP_OK);
        debug_log_clear();
        assert(access(debug_log_current_path(), F_OK) != 0);
        assert(allocations == 0 && tasks == 0 && hooks == 0);
        rmdir(FS_MOUNT_POINT);
    } else if (!strcmp(argv[1], "no_storage")) {
        enabled = true;
        persistent = false;
        assert(debug_log_init() == ESP_ERR_INVALID_STATE);
        assert(allocations == 0 && tasks == 0 && hooks == 0);
        debug_log_set_enabled(false);
        assert(level == ESP_LOG_NONE);
    } else {
        mkdir(FS_MOUNT_POINT, 0700);
        assert(debug_log_init() == ESP_OK);
        debug_log_set_enabled(true);
        assert(level == ESP_LOG_INFO && allocations == 2 && tasks == 1 && hooks == 1);
        emit("opt-in diagnostic\n");
        debug_log_set_enabled(true);
        assert(allocations == 2 && tasks == 1 && hooks == 1);
        debug_log_set_enabled(false);
        assert(level == ESP_LOG_NONE && hook == NULL && hooks == 2);
        FILE *f = fopen(debug_log_current_path(), "r");
        assert(f);
        char line[80];
        assert(fgets(line, sizeof(line), f));
        fclose(f);
        assert(!strcmp(line, "opt-in diagnostic\n"));
        remove(debug_log_current_path());
        rmdir(FS_MOUNT_POINT);
    }
    return 0;
}
