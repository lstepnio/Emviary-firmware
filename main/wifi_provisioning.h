#ifndef WIFI_PROVISIONING_H
#define WIFI_PROVISIONING_H

#include <stdbool.h>

#include "esp_err.h"

esp_err_t wifi_provisioning_init(void);
esp_err_t wifi_provisioning_start_ap(void);
esp_err_t wifi_provisioning_stop_ap(void);
bool wifi_provisioning_is_provisioned(void);


// Result of this AP provisioning session, independent of existing stored profiles.
// Reset when AP starts; true only after tested credentials are saved successfully.
bool wifi_provisioning_successful(void);

#endif
