#include "navigation_state.h"
#include <stdint.h>
#include "board_hal.h"
#include "esp_log.h"
#include "freertos/queue.h"
#include "power_manager.h"
#include "utils.h"

#ifdef EMVIARY_CLOUD_NAVIGATION
static const char *TAG = "navigation";
static QueueHandle_t navigation_queue;
static portMUX_TYPE navigation_mux = portMUX_INITIALIZER_UNLOCKED;
static bool navigation_queued, navigation_running, navigation_suspended;

esp_err_t emviary_navigation_init(void)
{
    if (navigation_queue) return ESP_OK;
    navigation_queue = xQueueCreate(1, sizeof(bool));
    return navigation_queue ? ESP_OK : ESP_ERR_NO_MEM;
}

bool emviary_navigation_pending(void)
{
    portENTER_CRITICAL(&navigation_mux);
    bool pending = navigation_queued || navigation_running;
    portEXIT_CRITICAL(&navigation_mux);
    return pending;
}

bool emviary_navigation_suspend_for_sleep(void)
{
    portENTER_CRITICAL(&navigation_mux);
    bool idle = !navigation_queued && !navigation_running;
    if (idle) navigation_suspended = true;
    portEXIT_CRITICAL(&navigation_mux);
    return idle;
}

void emviary_navigation_resume_after_sleep_deferred(void)
{
    portENTER_CRITICAL(&navigation_mux);
    navigation_suspended = false;
    portEXIT_CRITICAL(&navigation_mux);
}

void emviary_navigation_queue(bool previous)
{
    portENTER_CRITICAL(&navigation_mux);
    if (navigation_queue && !navigation_suspended) {
        navigation_queued = true;
        xQueueOverwrite(navigation_queue, &previous);
    }
    portEXIT_CRITICAL(&navigation_mux);
}

void emviary_navigation_wait(void)
{
    bool previous;
    if (navigation_queue) xQueuePeek(navigation_queue, &previous, portMAX_DELAY);
}

bool emviary_navigation_process_next(TickType_t timeout_ticks)
{
    // Waiting outside the navigation critical section permits taps to replace
    // the pending direction. Sleep uses the same operation-then-state order.
    if (!navigation_queue || !utils_image_operation_begin(timeout_ticks)) return false;
    bool previous;
    portENTER_CRITICAL(&navigation_mux);
    bool claimed = !navigation_suspended && !navigation_running &&
                   xQueueReceive(navigation_queue, &previous, 0) == pdTRUE;
    if (claimed) { navigation_queued = false; navigation_running = true; }
    portEXIT_CRITICAL(&navigation_mux);
    if (claimed) {
        power_manager_reset_sleep_timer();
        ESP_LOGI(TAG, "Processing queued %s image", previous ? "previous" : "next");
        // The image mutex is recursive: trigger keeps its own full-operation
        // contract, but cannot block behind another owner after dequeueing.
        trigger_image_navigation(previous);
        portENTER_CRITICAL(&navigation_mux);
        navigation_running = false;
        portEXIT_CRITICAL(&navigation_mux);
    }
    utils_image_operation_end();
    return claimed;
}
#else
bool emviary_navigation_pending(void) { return false; }
bool emviary_navigation_suspend_for_sleep(void) { return true; }
void emviary_navigation_resume_after_sleep_deferred(void) {}
esp_err_t emviary_navigation_init(void) { return ESP_OK; }
void emviary_navigation_queue(bool previous) { (void) previous; }
void emviary_navigation_wait(void) {}
bool emviary_navigation_process_next(TickType_t timeout_ticks) { (void) timeout_ticks; return false; }
#endif
