#include <assert.h>
#include <stdio.h>
#include "storage.h"
#include "esp_littlefs.h"
#include "esp_partition.h"
static int mount_fail, formats, mounts, unmounts, grows, albums, grow_fail;
static size_t fs_size = 16;
static esp_partition_t partition = {16};
const char *esp_err_to_name(esp_err_t err) { return "mock"; }
esp_err_t esp_vfs_littlefs_register(const esp_vfs_littlefs_conf_t *config) {
    // Library must never erase bytes to recover from a bad mount.
    assert(!config->format_if_mount_failed);
    mounts++;
    if (config->grow_on_mount) { grows++; if (grow_fail) return ESP_FAIL; fs_size=partition.size; }
    return mount_fail ? ESP_FAIL : ESP_OK;
}
esp_err_t esp_vfs_littlefs_unregister(const char *label) { unmounts++; return ESP_OK; }
esp_err_t esp_littlefs_info(const char *label, size_t *total, size_t *used) {
    *total=fs_size; *used=0; return ESP_OK;
}
esp_err_t esp_littlefs_format(const char *label) { formats++; fs_size=partition.size; return ESP_OK; }
const esp_partition_t *esp_partition_find_first(int type, int subtype, const char *label) { return &partition; }
esp_err_t memfs_mount(const char *path, int max_files) { return ESP_OK; }
esp_err_t album_manager_ensure_default_album(void) { albums++; return ESP_OK; }
int main(void) {
    mount_fail=1;
    assert(storage_init() == ESP_OK);
    assert(storage_get_type() == STORAGE_TYPE_MEMFS && formats == 0);
    mount_fail=0; fs_size=32;
    assert(storage_init() == ESP_OK);
    assert(storage_get_type() == STORAGE_TYPE_MEMFS && formats == 0 && unmounts == 1);
    fs_size=8;
    assert(storage_init() == ESP_OK);
    assert(storage_get_type() == STORAGE_TYPE_LITTLEFS && grows == 1 && formats == 0);
    fs_size=8; grow_fail=1;
    assert(storage_init() == ESP_OK);
    assert(storage_get_type() == STORAGE_TYPE_LITTLEFS && fs_size == 8 && formats == 0);
    grow_fail=0;
    assert(storage_format() == ESP_OK);
    assert(formats == 1 && albums == 1 && fs_size == partition.size);
    puts("nondestructive mount failure, size mismatch, grow fallback and explicit format tests passed");
    return 0;
}
