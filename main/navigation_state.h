#pragma once
#include <stdbool.h>
bool emviary_navigation_pending(void);
bool emviary_navigation_suspend_for_sleep(void);
void emviary_navigation_resume_after_sleep_deferred(void);

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
// One-slot, last-press-wins navigation shared by awake and scheduled workers.
esp_err_t emviary_navigation_init(void);
void emviary_navigation_queue(bool previous);
void emviary_navigation_wait(void);
// Reserve the image operation before consuming the queue. A timeout leaves the
// latest queued direction intact. Caller supplies retry pacing after false.
bool emviary_navigation_process_next(TickType_t timeout_ticks);
