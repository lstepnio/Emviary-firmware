#pragma once
#include "esp_err.h"
#include "esp_http_client.h"
#include <stdbool.h>
void usb_recovery_start(void);
bool usb_recovery_active(void);
void usb_recovery_stage(const char *stage, esp_err_t result);
void usb_recovery_warning(const char *stage, esp_err_t result);
void usb_recovery_ready(void);
void usb_recovery_persistence_ready(void);
void usb_recovery_cleanup(void);
void usb_recovery_add_headers(esp_http_client_handle_t client);
void usb_recovery_response_header(const char *key, const char *value);
esp_err_t usb_recovery_verify_image(const char *path);
void usb_recovery_panel_result(esp_err_t result);
void usb_recovery_fetch_failed(esp_err_t result);
