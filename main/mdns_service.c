#include "mdns_service.h"

#include <stdio.h>
#include <string.h>

#include "board_hal.h"
#include "config_manager.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "mdns.h"
#include "utils.h"

static const char *TAG = "mdns_service";
static bool s_started;

void mdns_service_refresh_alias(void)
{
    if (!s_started) return;
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t ip;
    if (!netif || esp_netif_get_ip_info(netif, &ip) != ESP_OK || !ip.ip.addr) return;
    mdns_ip_addr_t address = { .addr = { .type = ESP_IPADDR_TYPE_V4,
                                      .u_addr.ip4 = ip.ip }, .next = NULL };
    esp_err_t err = mdns_hostname_exists("photoframe")
        ? mdns_delegate_hostname_set_address("photoframe", &address)
        : mdns_delegate_hostname_add("photoframe", &address);
    if (err != ESP_OK) ESP_LOGW(TAG, "Legacy hostname alias unavailable: %s", esp_err_to_name(err));
    else ESP_LOGI(TAG, "Legacy hostname alias: http://photoframe.local");
}

static esp_err_t mdns_register_services(void)
{
    const char *device_name = config_manager_get_device_name();
    if (strcmp(device_name, "PhotoFrame") == 0) {
        config_manager_set_device_name("Emviary");
        device_name = config_manager_get_device_name();
    }
    char hostname[64];
    sanitize_hostname(device_name, hostname, sizeof(hostname));

    ESP_LOGI(TAG, "Device name: %s, hostname: %s", device_name, hostname);

    esp_err_t err = mdns_hostname_set(hostname);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set mDNS hostname: %s", esp_err_to_name(err));
        return err;
    }

    err = mdns_instance_name_set(device_name);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set mDNS instance name: %s", esp_err_to_name(err));
        return err;
    }

    // Add HTTP service
    err = mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add HTTP service: %s", esp_err_to_name(err));
        return err;
    }

    // Add photoframe-specific service for app auto-discovery
    const esp_app_desc_t *app_desc = esp_app_get_description();
    char mdns_host[70];
    snprintf(mdns_host, sizeof(mdns_host), "%s.local", hostname);
    mdns_txt_item_t txt[] = {
        {"name", device_name},
        {"host", mdns_host},
        {"board", BOARD_HAL_NAME},
        {"version", app_desc->version},
    };
    err = mdns_service_add(NULL, "_esp32-pframe", "_tcp", 80, txt, 4);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to add photoframe service: %s", esp_err_to_name(err));
        // Non-fatal: HTTP service still works
    }

    s_started = true;
    mdns_service_refresh_alias();
    ESP_LOGI(TAG, "mDNS services registered, accessible at: http://%s.local", hostname);
    return ESP_OK;
}

esp_err_t mdns_service_init(void)
{
    esp_err_t err = mdns_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mDNS initialization failed: %s", esp_err_to_name(err));
        return err;
    }

    return mdns_register_services();
}

esp_err_t mdns_service_update_hostname(void)
{
    // Free existing mDNS service to send goodbye packets, then reinitialize
    s_started = false;
    mdns_free();

    esp_err_t err = mdns_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to reinitialize mDNS: %s", esp_err_to_name(err));
        return err;
    }

    return mdns_register_services();
}
