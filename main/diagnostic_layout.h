#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef struct {
    char frame_name[64], hostname[64], ip_address[48], wifi_name[64];
    char cloud_status[48], last_refresh[48], battery_status[48], next_wake[48], firmware[32];
    int rssi_dbm;
    bool wifi_connected, rssi_valid, ip_last_known;
} display_diagnostic_status_t;
typedef void (*diagnostic_pixel_fn)(void *context, int x, int y);
// Draws black pixels only. Caller provides a white transient canvas.
void diagnostic_layout_render(int width, int height, const display_diagnostic_status_t *status,
                              diagnostic_pixel_fn pixel, void *context);
