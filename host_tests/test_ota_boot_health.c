#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ota_boot_health.h"
#include "esp_ota_ops.h"
#include "esp_heap_caps.h"
#include "esp_flash.h"
#include "psa/crypto.h"
#undef fopen
#undef fwrite
#undef fread
#undef fclose
#undef unlink

static esp_partition_t running;
static esp_ota_img_states_t image_state = ESP_OTA_IMG_PENDING_VERIFY;
static int no_partition, state_failure, storage_missing, heap_failure;
static int open_failure, write_failure, read_failure, corrupt_read, close_failure;
static int confirmations, rollbacks, opens, closes, unlinks, allocations;
static int confirmation_result, rollback_result;
static FILE *probe_file;
static int read_mode;
static int flash_failure, hash_setup_failure, hash_update_failure, hash_finish_failure;
static int digest_mismatch, short_digest, flash_reads, hash_bytes, aborts;
static size_t current_hash_bytes;
static int approved_previous, unapproved_previous, approved_wrong_size;
void *esp_flash_default_chip = (void *)1;

const char *esp_err_to_name(esp_err_t value) { return "stub"; }
const esp_partition_t *esp_ota_get_running_partition(void) {
    return no_partition ? NULL : &running;
}
esp_err_t esp_ota_get_state_partition(const esp_partition_t *partition,
                                     esp_ota_img_states_t *state) {
    assert(partition == &running); *state = image_state;
    return state_failure ? ESP_FAIL : ESP_OK;
}
bool storage_has_persistent_storage(void) { return !storage_missing; }
void *heap_caps_malloc(size_t size, unsigned caps) {
    assert(size == 16384 && caps == (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    allocations++;
    return heap_failure ? NULL : malloc(size);
}
esp_err_t esp_ota_mark_app_invalid_rollback_and_reboot(void) {
    rollbacks++; return rollback_result;
}
esp_err_t esp_ota_mark_app_valid_cancel_rollback(void) {
    confirmations++; return confirmation_result;
}
FILE *health_fopen(const char *path, const char *mode) {
    assert(strcmp(path, "/host-health-test/.boot-health") == 0); opens++;
    if (open_failure) return NULL;
    read_mode = strcmp(mode, "rb") == 0;
    if (read_mode) { assert(probe_file); rewind(probe_file); return probe_file; }
    assert(strcmp(mode, "wb") == 0); probe_file = tmpfile(); assert(probe_file);
    return probe_file;
}
size_t health_fwrite(const void *ptr, size_t size, size_t count, FILE *file) {
    assert(size == 1 && count == 7 && memcmp(ptr, "emviary", 7) == 0);
    return fwrite(ptr, size, write_failure ? count - 1 : count, file);
}
size_t health_fread(void *ptr, size_t size, size_t count, FILE *file) {
    size_t result = fread(ptr, size, read_failure ? count - 1 : count, file);
    if (corrupt_read) ((char *)ptr)[0] = '!';
    return result;
}
int health_fclose(FILE *file) {
    closes++;
    if (!read_mode) { fflush(file); return close_failure ? -1 : 0; }
    int result = fclose(file); probe_file = NULL; return result;
}
int health_unlink(const char *path) { unlinks++; return 0; }
esp_err_t esp_flash_read(void *chip, void *buffer, uint32_t address, uint32_t size) {
    assert(chip == esp_flash_default_chip);
    assert(address == current_hash_bytes);
    assert(size == 512 || size == 7 || size == 265);
    flash_reads++; memset(buffer, 0xA5, size);
    return flash_failure ? ESP_FAIL : ESP_OK;
}
int psa_hash_setup(psa_hash_operation_t *op, int alg) {
    assert(alg == PSA_ALG_SHA_256); op->active = 1; current_hash_bytes = 0;
    return hash_setup_failure ? -1 : PSA_SUCCESS;
}
int psa_hash_update(psa_hash_operation_t *op, const uint8_t *input, size_t size) {
    assert(op->active && input[0] == 0xA5); hash_bytes += size; current_hash_bytes += size;
    return hash_update_failure ? -1 : PSA_SUCCESS;
}
int psa_hash_finish(psa_hash_operation_t *op, uint8_t *output, size_t capacity, size_t *size) {
    assert(op->active && capacity == 32); memset(output, digest_mismatch ? 1 : 0, capacity);
    if (approved_previous) memset(output, current_hash_bytes == 777 ? 1 : 2, capacity);
    if (unapproved_previous) memset(output, 2, capacity);
    if (approved_wrong_size) memset(output, current_hash_bytes == 1031 ? 1 : 2, capacity);
    *size = short_digest ? 31 : 32;
    return hash_finish_failure ? -1 : PSA_SUCCESS;
}
int psa_hash_abort(psa_hash_operation_t *op) { aborts++; op->active = 0; return PSA_SUCCESS; }

int main(int argc, char **argv) {
    assert(argc == 2); const char *scenario = argv[1];
    if (strncmp(scenario, "bootloader_", 11) == 0) {
        flash_failure = strcmp(scenario, "bootloader_read_failure") == 0;
        hash_setup_failure = strcmp(scenario, "bootloader_setup_failure") == 0;
        hash_update_failure = strcmp(scenario, "bootloader_update_failure") == 0;
        hash_finish_failure = strcmp(scenario, "bootloader_finish_failure") == 0;
        short_digest = strcmp(scenario, "bootloader_short_digest") == 0;
        digest_mismatch = strcmp(scenario, "bootloader_mismatch") == 0;
        approved_previous = strcmp(scenario, "bootloader_approved_previous") == 0;
        unapproved_previous = strcmp(scenario, "bootloader_unapproved_previous") == 0;
        approved_wrong_size = strcmp(scenario, "bootloader_approved_wrong_size") == 0;
        bool expected = strcmp(scenario, "bootloader_verified") == 0 || approved_previous;
        assert(ota_bootloader_rollback_ready() == expected);
        int reads = flash_reads;
        assert(ota_bootloader_rollback_ready() == expected && flash_reads == reads);
        if (approved_previous || unapproved_previous || approved_wrong_size)
            assert(reads == 5 && hash_bytes == 1808 && aborts == 2);
        else if (expected || digest_mismatch)
            assert(reads == 3 && hash_bytes == 1031 && aborts == 1);
        if (flash_failure) assert(reads == 1 && hash_bytes == 0 && aborts == 1);
        if (strcmp(scenario, "bootloader_bootstrap") == 0)
            assert(reads == 0 && hash_bytes == 0 && aborts == 0);
        puts(scenario); return 0;
    }
    no_partition = strcmp(scenario, "no_partition") == 0;
    state_failure = strcmp(scenario, "state_unavailable") == 0;
    bool nonpending = strcmp(scenario, "already_valid") == 0;
    if (nonpending) image_state = ESP_OTA_IMG_VALID;
    storage_missing = strcmp(scenario, "storage_missing") == 0;
    heap_failure = strcmp(scenario, "heap_failure") == 0;
    open_failure = strcmp(scenario, "file_open_failure") == 0;
    write_failure = strcmp(scenario, "file_write_failure") == 0;
    read_failure = strcmp(scenario, "file_read_failure") == 0;
    corrupt_read = strcmp(scenario, "file_corrupt") == 0;
    close_failure = strcmp(scenario, "file_close_failure") == 0;
    if (strcmp(scenario, "confirmation_error") == 0) confirmation_result = ESP_FAIL;
    if (strcmp(scenario, "rollback_error") == 0) { heap_failure = 1; rollback_result = ESP_FAIL; }
    esp_err_t result = ota_confirm_healthy_boot();
    bool skipped = no_partition || state_failure || nonpending;
    bool failed = storage_missing || heap_failure || open_failure || write_failure ||
                  read_failure || corrupt_read || close_failure;
    assert(confirmations == (!skipped && !failed));
    assert(rollbacks == (!skipped && failed));
    assert(result == (skipped ? ESP_OK : failed ? rollback_result : confirmation_result));
    if (skipped) assert(opens == 0 && allocations == 0);
    if (storage_missing || heap_failure) assert(opens == 0);
    if (!skipped && !failed) assert(opens == 2 && closes == 2 && unlinks == 1);
    if (probe_file) fclose(probe_file);
    puts(scenario); return 0;
}
