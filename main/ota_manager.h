#ifndef OTA_MANAGER_H
#define OTA_MANAGER_H

#include <stdbool.h>

#include "esp_err.h"

typedef enum {
    OTA_STATE_IDLE,
    OTA_STATE_CHECKING,
    OTA_STATE_UPDATE_AVAILABLE,
    OTA_STATE_DOWNLOADING,
    OTA_STATE_INSTALLING,
    OTA_STATE_SUCCESS,
    OTA_STATE_ERROR
} ota_state_t;

// SUCCESS is still an active operation until the installer's pending reboot.
// UPDATE_AVAILABLE is only an idle offer and does not block USB recovery.
static inline bool ota_state_blocks_recovery(ota_state_t state)
{
    return state == OTA_STATE_CHECKING || state == OTA_STATE_DOWNLOADING ||
           state == OTA_STATE_INSTALLING || state == OTA_STATE_SUCCESS;
}

typedef struct {
    ota_state_t state;
    char current_version[32];
    char latest_version[32];
    char error_message[128];
    int progress_percent;
} ota_status_t;

esp_err_t ota_check_on_wake(void);
esp_err_t ota_manager_init(void);
esp_err_t ota_check_for_update(bool *update_available, int timeout);
esp_err_t ota_start_update(void);
void ota_get_status(ota_status_t *status);
// Recovery UART must never wait behind OTA status/NVS work.
bool ota_try_get_status(ota_status_t *status);
const char *ota_get_current_version(void);
bool ota_should_check_daily(void);
void ota_update_last_check_time(void);

#endif  // OTA_MANAGER_H
