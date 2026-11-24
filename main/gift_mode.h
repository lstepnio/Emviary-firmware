#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cJSON.h"
#include "esp_err.h"
#include "gift_policy.h"
/* Call mutation functions under utils' recursive image-operation lock. */
bool gift_mode_active(void);
bool gift_mode_navigation_blocked(void);
bool gift_mode_hold(void);
void gift_mode_end_hold(void);
bool gift_mode_display_allowed(const char *path);
esp_err_t gift_mode_apply(const char *manifest);
esp_err_t gift_mode_sync(void);
esp_err_t gift_mode_report(void);
esp_err_t gift_mode_reveal(void);
esp_err_t gift_mode_restore(void);
esp_err_t gift_mode_resume(void);
esp_err_t gift_mode_note_diagnostics(void);
void gift_mode_add_metrics(cJSON *json);
/* Takes ownership of unformatted JSON; preserves gift telemetry under header cap. */
char *gift_mode_bound_metrics(char *plain);
int64_t gift_mode_hold_until(void);
/* Bounded, same-origin, authenticated, redirect-disabled transport in utils. */
esp_err_t utils_gift_download(const char *url, const char *path, size_t limit);
