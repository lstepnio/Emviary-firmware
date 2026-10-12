#include "ota_boot_health.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "config.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "psa/crypto.h"
#include "storage.h"
#include "ota_bootloader_digest.h"
#include "approved_bootloaders.h"

#ifdef CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE
static bool bootloader_matches(size_t size, const char *expected_sha256)
{
    if (!size || size > 0x8000 || !expected_sha256 || strlen(expected_sha256) != 64)
        return false;
    psa_hash_operation_t op = PSA_HASH_OPERATION_INIT;
    unsigned char block[512], digest[32]; size_t length = 0;
    if (psa_hash_setup(&op, PSA_ALG_SHA_256) != PSA_SUCCESS) return false;
    bool ok = true;
    for (size_t offset = 0; ok && offset < size; offset += sizeof(block)) {
        size_t count = size - offset;
        if (count > sizeof(block)) count = sizeof(block);
        ok = esp_flash_read(esp_flash_default_chip, block, offset, count) == ESP_OK &&
             psa_hash_update(&op, block, count) == PSA_SUCCESS;
    }
    ok = ok && psa_hash_finish(&op, digest, sizeof(digest), &length) == PSA_SUCCESS && length == 32;
    psa_hash_abort(&op);
    char hex[65];
    if (ok) {
        for (int i = 0; i < 32; i++) snprintf(hex + 2 * i, 3, "%02x", digest[i]);
        return strcmp(hex, expected_sha256) == 0;
    }
    return false;
}
#endif

bool ota_bootloader_rollback_ready(void)
{
#ifdef CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE
    static int cached = -1;
    if (cached >= 0) return cached == 1;
    cached = bootloader_matches(EMVIARY_BOOTLOADER_SIZE, EMVIARY_BOOTLOADER_SHA256);
    for (size_t i = 0; !cached && i < sizeof(ota_approved_bootloaders) /
                                             sizeof(ota_approved_bootloaders[0]); ++i) {
        const ota_approved_bootloader_t *approved = &ota_approved_bootloaders[i];
        if (approved->size) cached = bootloader_matches(approved->size, approved->sha256);
    }
    return cached == 1;
#else
    return false;
#endif
}

esp_err_t ota_confirm_healthy_boot(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t state;
    if (!running || esp_ota_get_state_partition(running, &state) != ESP_OK ||
        state != ESP_OTA_IMG_PENDING_VERIFY) return ESP_OK;
    // Confirm only after board, storage, configuration, display, power, Wi-Fi
    // and provisioning initialization. No network, weather, or panel repaint
    // is required for this bounded offline check.
    bool healthy = storage_has_persistent_storage();
    void *probe = heap_caps_malloc(16384, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    healthy = healthy && probe != NULL;
    free(probe);
    const char *path = FS_MOUNT_POINT "/.boot-health";
    FILE *file = healthy ? fopen(path, "wb") : NULL;
    if (!file) healthy = false;
    else {
        healthy = fwrite("emviary", 1, 7, file) == 7;
        if (fclose(file) != 0) healthy = false;
        char value[7]; file = fopen(path, "rb");
        healthy = healthy && file && fread(value, 1, 7, file) == 7 && memcmp(value, "emviary", 7) == 0;
        if (file) fclose(file);
        unlink(path);
    }
    if (!healthy) {
        ESP_LOGE("ota_health", "Offline boot health failed; rolling back");
        return esp_ota_mark_app_invalid_rollback_and_reboot();
    }
    esp_err_t result = esp_ota_mark_app_valid_cancel_rollback();
    ESP_LOGI("ota_health", "Offline boot health passed, confirmation: %s", esp_err_to_name(result));
    return result;
}

void ota_bootloader_rejected_version(char *version, size_t size)
{
    if (!size) return;
    version[0] = 0;
    const esp_partition_t *candidate = esp_ota_get_next_update_partition(NULL);
    esp_ota_img_states_t state;
    esp_app_desc_t description;
    if (candidate && esp_ota_get_state_partition(candidate, &state) == ESP_OK &&
        (state == ESP_OTA_IMG_ABORTED || state == ESP_OTA_IMG_INVALID) &&
        esp_ota_get_partition_description(candidate, &description) == ESP_OK)
        snprintf(version, size, "%s", description.version);
}
