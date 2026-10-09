#include "ota_manager.h"
#include "ota_recovery.h"
#include "esp_system.h"

#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "board_hal.h"
#include "cJSON.h"
#include "config.h"
#include "config_manager.h"
#include "config_validation.h"
#include "psa/crypto.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_idf_version.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "ha_integration.h"
#include "nvs.h"
#include "periodic_tasks.h"
#include "power_manager.h"
#include "utils.h"

static const char *TAG = "ota_manager";
#define OTA_NVS_NAMESPACE "ota"
#define OTA_NVS_LATEST_VERSION_KEY "latest_ver"
#define OTA_NVS_STATE_KEY "state"
#define OTA_CHECK_INTERVAL_SECONDS (24 * 60 * 60)  // 24 hours

static ota_status_t ota_status = {.state = OTA_STATE_IDLE,
                                  .current_version = "",
                                  .latest_version = "",
                                  .error_message = "",
                                  .progress_percent = 0};

static SemaphoreHandle_t ota_status_mutex = NULL;
static bool update_available = false;
static char firmware_url[256] = "";
static bool cloud_update_offered;
static char expected_sha256[65];
static int expected_size;
#define OTA_WORKER_STACK_BYTES 12288
#define OTA_NVS_RECOVERY_KEY "recovery"
static ota_recovery_record_t recovery_record;
static esp_err_t ota_install(bool automatic);

static esp_err_t ota_recovery_store(const ota_recovery_record_t *record)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(OTA_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        err = record ? nvs_set_blob(handle, OTA_NVS_RECOVERY_KEY, record, sizeof(*record))
                     : nvs_erase_key(handle, OTA_NVS_RECOVERY_KEY);
        if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
        if (err == ESP_OK) err = nvs_commit(handle);
        nvs_close(handle);
    }
    return err;
}

static void ota_recovery_clear(void)
{
    if (ota_recovery_store(NULL) != ESP_OK)
        ESP_LOGW(TAG, "Unable to clear completed OTA recovery marker");
    memset(&recovery_record, 0, sizeof(recovery_record));
}

static void ota_recovery_load(void)
{
    nvs_handle_t handle;
    size_t length = sizeof(recovery_record);
    memset(&recovery_record, 0, sizeof(recovery_record));
    if (nvs_open(OTA_NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) return;
    esp_err_t err = nvs_get_blob(handle, OTA_NVS_RECOVERY_KEY, &recovery_record, &length);
    nvs_close(handle);
    if (err != ESP_OK || length != sizeof(recovery_record) ||
        !ota_recovery_record_valid(&recovery_record)) {
        memset(&recovery_record, 0, sizeof(recovery_record));
        return;
    }
    esp_reset_reason_t reset = esp_reset_reason();
    bool crashed = reset == ESP_RST_PANIC || reset == ESP_RST_INT_WDT ||
                   reset == ESP_RST_TASK_WDT || reset == ESP_RST_WDT;
    if (ota_recovery_hold_after_reset(&recovery_record, ota_status.current_version, crashed)) {
        if (ota_recovery_store(&recovery_record) != ESP_OK)
            ESP_LOGW(TAG, "OTA crash hold could not be persisted; holding this boot");
        ESP_LOGW(TAG, "Interrupted OTA crashed; unchanged automatic candidate held for recovery");
    }
}

static bool operation_busy;
static bool claim_operation(ota_state_t state) {
    if (!ota_status_mutex) return false;
    xSemaphoreTake(ota_status_mutex, portMAX_DELAY);
    bool claimed = !operation_busy;
    if (claimed) { operation_busy = true; ota_status.state = state; }
    xSemaphoreGive(ota_status_mutex);
    return claimed;
}
static void release_operation(void) {
    xSemaphoreTake(ota_status_mutex, portMAX_DELAY);
    operation_busy = false;
    xSemaphoreGive(ota_status_mutex);
}

// Forward declarations
static void ota_save_status_to_nvs(void);
static void ota_load_status_from_nvs(void);
static esp_err_t ota_check_periodic_callback(void);

static void set_ota_state(ota_state_t state, const char *error_msg)
{
    if (ota_status_mutex && xSemaphoreTake(ota_status_mutex, portMAX_DELAY) == pdTRUE) {
        ota_status.state = state;
        if (error_msg) {
            snprintf(ota_status.error_message, sizeof(ota_status.error_message), "%s", error_msg);
        } else {
            ota_status.error_message[0] = '\0';
        }
        xSemaphoreGive(ota_status_mutex);
    }
}

#define GITHUB_RESPONSE_MAX_LEN (64 * 1024)
typedef struct {
    char *buffer;
    size_t length;
    int64_t deadline;
    esp_err_t failure;
} metadata_context_t;

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    metadata_context_t *ctx = evt->user_data;
    if (ctx && evt->event_id != HTTP_EVENT_DISCONNECTED && evt->event_id != HTTP_EVENT_ERROR) {
        int64_t now = esp_timer_get_time();
        if (now >= ctx->deadline) ctx->failure = ESP_ERR_TIMEOUT;
        if (evt->event_id == HTTP_EVENT_ON_DATA && ctx->failure == ESP_OK) {
            if (evt->data_len < 0 || (size_t)evt->data_len > GITHUB_RESPONSE_MAX_LEN - ctx->length)
                ctx->failure = ESP_ERR_INVALID_SIZE;
            else {
                memcpy(ctx->buffer + ctx->length, evt->data, evt->data_len);
                ctx->length += evt->data_len;
            }
        }
        if (ctx->failure != ESP_OK) {
            esp_http_client_close(evt->client);
            return ctx->failure;
        }
        int remaining = (int)((ctx->deadline - now + 999) / 1000);
        esp_http_client_set_timeout_ms(evt->client, remaining < 5000 ? remaining : 5000);
    }
    switch (evt->event_id) {
    case HTTP_EVENT_ERROR:
        ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
        break;
    case HTTP_EVENT_ON_CONNECTED:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
        break;
    case HTTP_EVENT_HEADER_SENT:
        ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
        break;
    case HTTP_EVENT_ON_HEADER:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER (values withheld)");
        break;
    case HTTP_EVENT_ON_DATA:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
        break;
    case HTTP_EVENT_ON_FINISH:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
        break;
    case HTTP_EVENT_DISCONNECTED:
        ESP_LOGD(TAG, "HTTP_EVENT_DISCONNECTED");
        break;
    case HTTP_EVENT_REDIRECT:
        ESP_LOGD(TAG, "HTTP_EVENT_REDIRECT");
        break;
    default:
        break;
    }
    return ESP_OK;
}

typedef struct {
    int64_t deadline;
    size_t limit;
    size_t received;
    esp_err_t failure;
} firmware_transfer_context_t;

static esp_err_t firmware_http_event(esp_http_client_event_t *evt)
{
    firmware_transfer_context_t *ctx = evt->user_data;
    if (!ctx || evt->event_id == HTTP_EVENT_DISCONNECTED || evt->event_id == HTTP_EVENT_ERROR)
        return ESP_OK;
    int64_t now = esp_timer_get_time();
    if (now >= ctx->deadline) ctx->failure = ESP_ERR_TIMEOUT;
    if (evt->event_id == HTTP_EVENT_ON_DATA && esp_http_client_get_status_code(evt->client) == 200) {
        if (evt->data_len < 0 || (size_t)evt->data_len > ctx->limit - ctx->received)
            ctx->failure = ESP_ERR_INVALID_SIZE;
        else ctx->received += evt->data_len;
    }
    if (ctx->failure != ESP_OK) {
        esp_http_client_close(evt->client);
        return ctx->failure;
    }
    int remaining = (int)((ctx->deadline - now + 999) / 1000);
    esp_http_client_set_timeout_ms(evt->client, remaining < 10000 ? remaining : 10000);
    return ESP_OK;
}

// This project's own release (14 assets - 7 boards x merged+OTA binary) measured 34 KB of
// response JSON (GitHub's per-asset metadata, e.g. the uploader object, is verbose) - 64 KB
// leaves real headroom for more assets later.

static esp_err_t fetch_github_release_info(char *latest_version, size_t version_len,
                                           char *download_url, size_t url_len)
{
    esp_err_t err = ESP_FAIL;
    char *response_buffer = NULL;
    int response_len = 0;

    cloud_update_offered = false;
    update_available = false;
    expected_sha256[0] = '\0';
    expected_size = 0;
    const char *token = config_manager_get_access_token();
    if (!token || !token[0]) return ESP_ERR_INVALID_STATE;
    char check_url[256];
    snprintf(check_url, sizeof(check_url), "%s?current=%s", EMVIARY_UPDATE_API_URL,
             ota_status.current_version);
    response_buffer = heap_caps_malloc(GITHUB_RESPONSE_MAX_LEN + 1, MALLOC_CAP_SPIRAM);
    if (!response_buffer) return ESP_ERR_NO_MEM;
    metadata_context_t ctx = {
        .buffer = response_buffer, .deadline = esp_timer_get_time() + 20000000LL,
        .failure = ESP_OK,
    };
    esp_http_client_config_t config = {
        .url = check_url,
        .event_handler = http_event_handler,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 5000,
        .disable_auto_redirect = true,
        .is_async = true,
        .user_data = &ctx,
        .buffer_size = 4096,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        ESP_LOGE(TAG, "Failed to initialize HTTP client");
        free(response_buffer);
        return ESP_FAIL;
    }

    // Set User-Agent header (GitHub API requires it)
    esp_http_client_set_header(client, "User-Agent", "Emviary-E1002");
    char authorization[ACCESS_TOKEN_MAX_LEN + 8];
    if (snprintf(authorization, sizeof(authorization), "Bearer %s", token) >= sizeof(authorization)) {
        err = ESP_ERR_INVALID_SIZE; goto cleanup;
    }
    esp_http_client_set_header(client, "Authorization", authorization);
    do {
        int64_t now = esp_timer_get_time();
        if (now >= ctx.deadline) { err = ESP_ERR_TIMEOUT; goto cleanup; }
        int remaining = (int)((ctx.deadline - now + 999) / 1000);
        esp_http_client_set_timeout_ms(client, remaining < 5000 ? remaining : 5000);
        err = esp_http_client_perform(client);
        if (ctx.failure != ESP_OK) { err = ctx.failure; goto cleanup; }
        if (err == ESP_ERR_HTTP_EAGAIN) vTaskDelay(pdMS_TO_TICKS(20));
    } while (err == ESP_ERR_HTTP_EAGAIN);
    if (err != ESP_OK) goto cleanup;
    int64_t content_length = esp_http_client_get_content_length(client);
    response_len = (int)ctx.length;
    if (esp_http_client_get_status_code(client) != 200 || !response_len ||
        content_length > GITHUB_RESPONSE_MAX_LEN ||
        !esp_http_client_is_complete_data_received(client) ||
        (content_length > 0 && response_len != content_length) ||
        memchr(response_buffer, 0, response_len)) {
        err = ESP_ERR_INVALID_SIZE; goto cleanup;
    }
    response_buffer[response_len] = 0;

    if (!config_json_shape_valid(response_buffer, response_len)) {
        err = ESP_ERR_INVALID_ARG; goto cleanup;
    }
    // Parse JSON response
    cJSON *json = cJSON_ParseWithLengthOpts(response_buffer, response_len + 1, NULL, true);
    if (json == NULL || !cJSON_IsObject(json)) {
        cJSON_Delete(json);
        ESP_LOGE(TAG, "Failed to parse JSON response");
        err = ESP_FAIL;
        goto cleanup;
    }

    cJSON *offered = cJSON_GetObjectItem(json, "update_available");
    if (!cJSON_IsBool(offered)) { cJSON_Delete(json); err = ESP_FAIL; goto cleanup; }
    if (!cJSON_IsTrue(offered)) {
        snprintf(latest_version, version_len, "%s", ota_status.current_version);
        download_url[0] = '\0';
        cJSON_Delete(json);
        err = ESP_OK;
        goto cleanup;
    }
    cJSON *digest = cJSON_GetObjectItem(json, "sha256");
    cJSON *size = cJSON_GetObjectItem(json, "size");
    if (!cJSON_IsString(digest) || strlen(digest->valuestring) != 64 ||
        strspn(digest->valuestring, "0123456789abcdef") != 64 ||
        !cJSON_IsNumber(size) || size->valuedouble < 1024 || size->valuedouble > 0x380000 ||
        size->valuedouble != (double)size->valueint) {
        cJSON_Delete(json); err = ESP_ERR_INVALID_ARG; goto cleanup;
    }
    snprintf(expected_sha256, sizeof(expected_sha256), "%s", digest->valuestring);
    expected_size = size->valueint;

    // Get tag_name (version)
    cJSON *tag_name = cJSON_GetObjectItem(json, "tag_name");
    if (tag_name == NULL || !cJSON_IsString(tag_name) || strlen(tag_name->valuestring) >= version_len) {
        ESP_LOGE(TAG, "tag_name not found in response");
        cJSON_Delete(json);
        err = ESP_FAIL;
        goto cleanup;
    }

    snprintf(latest_version, version_len, "%s", tag_name->valuestring);

    // Get assets array and find .bin file
    cJSON *assets = cJSON_GetObjectItem(json, "assets");
    if (assets == NULL || !cJSON_IsArray(assets)) {
        ESP_LOGE(TAG, "assets not found in response");
        cJSON_Delete(json);
        err = ESP_FAIL;
        goto cleanup;
    }

    bool found_binary = false;
    cJSON *asset = NULL;

    const char *board_name = BOARD_HAL_NAME;

    char target_binary[64];
    snprintf(target_binary, sizeof(target_binary), "emviary-%s.bin", board_name);
    ESP_LOGI(TAG, "Searching for board-specific OTA binary: %s", target_binary);

    cJSON_ArrayForEach(asset, assets)
    {
        cJSON *name = cJSON_GetObjectItem(asset, "name");
        if (name && cJSON_IsString(name)) {
            const char *asset_name = name->valuestring;
            // Look for board-specific binary
            if (strcmp(asset_name, target_binary) == 0) {
                cJSON *browser_download_url = cJSON_GetObjectItem(asset, "browser_download_url");
                // Only an https:// address that fits the buffer is followed: a download over
                // plain http would carry the firmware without TLS, and a cut-off address would
                // download nothing sensible.
                if (browser_download_url && cJSON_IsString(browser_download_url) &&
                    strncmp(browser_download_url->valuestring,
                            "https://github.com/lstepnio/Emviary-firmware/releases/download/",
                            strlen("https://github.com/lstepnio/Emviary-firmware/releases/download/")) == 0 &&
                    strlen(browser_download_url->valuestring) < url_len &&
                    strrchr(browser_download_url->valuestring, '/') &&
                    !strcmp(strrchr(browser_download_url->valuestring, '/') + 1, target_binary)) {
                    snprintf(download_url, url_len, "%s", browser_download_url->valuestring);
                    found_binary = true;
                    ESP_LOGI(TAG, "Found firmware binary: %s", asset_name);
                    break;
                }
            }
        }
    }

    cJSON_Delete(json);

    if (!found_binary) {
        ESP_LOGE(TAG, "No .bin file found in release assets");
        err = ESP_FAIL;
        goto cleanup;
    }

    cloud_update_offered = true;
    err = ESP_OK;
    ESP_LOGI(TAG, "Latest version: %s", latest_version);
    ESP_LOGI(TAG, "Download URL: %s", download_url);

cleanup:
    if (response_buffer) {
        free(response_buffer);
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    return err;
}

static void ota_check_task(void *pvParameter)
{
    if (!utils_image_operation_begin(pdMS_TO_TICKS(5000))) {
        set_ota_state(OTA_STATE_ERROR, "Device operation busy");
        release_operation(); vTaskDelete(NULL); return;
    }
    // pvParameter is a boolean: true = notify HA, false/NULL = don't notify
    bool notify_ha = (pvParameter != NULL);

    ESP_LOGI(TAG, "Checking for firmware updates...");

    set_ota_state(OTA_STATE_CHECKING, NULL);

    char latest_version[32] = {0};
    char download_url[256] = {0};

    esp_err_t err = fetch_github_release_info(latest_version, sizeof(latest_version), download_url,
                                              sizeof(download_url));

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to fetch release info");
        set_ota_state(OTA_STATE_ERROR, "Failed to check for updates");
        utils_image_operation_end();
        release_operation();
        vTaskDelete(NULL);
        return;
    }

    // Store latest version and URL
    if (ota_status_mutex && xSemaphoreTake(ota_status_mutex, portMAX_DELAY) == pdTRUE) {
        snprintf(ota_status.latest_version, sizeof(ota_status.latest_version), "%s",
                 latest_version);
        xSemaphoreGive(ota_status_mutex);
    }
    snprintf(firmware_url, sizeof(firmware_url), "%s", download_url);

    // Compare versions
    if (cloud_update_offered) {
        ESP_LOGI(TAG, "Update available: %s -> %s", ota_status.current_version, latest_version);
        update_available = true;
        set_ota_state(OTA_STATE_UPDATE_AVAILABLE, NULL);
    } else {
        ESP_LOGI(TAG, "Already on latest version: %s", ota_status.current_version);
        update_available = false;
        set_ota_state(OTA_STATE_IDLE, NULL);
    }

    // Update last check time after successful check
    ota_update_last_check_time();

    // Save OTA status to NVS for persistence across reboots
    ota_save_status_to_nvs();

    // Notify HA if requested
    if (notify_ha) {
        ESP_LOGI(TAG, "Notifying HA of OTA status update");
        ha_notify_update();
    }

    utils_image_operation_end();
    release_operation();
    vTaskDelete(NULL);
}

static esp_err_t ota_install(bool automatic)
{
    // The operation claim prevents metadata mutation; retain a private candidate
    // throughout this install, including verification of the downloaded slot.
    char install_url[sizeof(firmware_url)], install_digest[sizeof(expected_sha256)];
    char install_version[sizeof(ota_status.latest_version)];
    snprintf(install_url, sizeof(install_url), "%s", firmware_url);
    snprintf(install_digest, sizeof(install_digest), "%s", expected_sha256);
    snprintf(install_version, sizeof(install_version), "%s", ota_status.latest_version);
    int install_size = expected_size;
    const esp_partition_t *target = esp_ota_get_next_update_partition(NULL);
    if (!target || install_size < 1024 || install_size > target->size || !install_url[0] ||
        strlen(install_digest) != 64) {
        set_ota_state(OTA_STATE_ERROR, "Invalid update candidate");
        return ESP_ERR_INVALID_SIZE;
    }
    if (ota_recovery_should_hold(&recovery_record, ota_status.current_version,
                                 install_digest, automatic)) {
        set_ota_state(OTA_STATE_ERROR, "Automatic retry held after crash; use recovery update");
        return ESP_ERR_INVALID_STATE;
    }
    ota_recovery_record_t attempt = {.format = OTA_RECOVERY_FORMAT, .phase = OTA_RECOVERY_ACTIVE};
    snprintf(attempt.source, sizeof(attempt.source), "%s", ota_status.current_version);
    snprintf(attempt.digest, sizeof(attempt.digest), "%s", install_digest);
    esp_err_t marker_err = ota_recovery_store(&attempt);
    if (marker_err != ESP_OK) {
        set_ota_state(OTA_STATE_ERROR, "Unable to save OTA recovery checkpoint");
        return marker_err;
    }
    recovery_record = attempt;
    ESP_LOGI(TAG, "Starting OTA update...");

    // Reset sleep timer to prevent auto-sleep during OTA
    power_manager_reset_sleep_timer();

    set_ota_state(OTA_STATE_DOWNLOADING, NULL);
    if (ota_status_mutex && xSemaphoreTake(ota_status_mutex, portMAX_DELAY) == pdTRUE) {
        ota_status.progress_percent = 0;
        xSemaphoreGive(ota_status_mutex);
    }

    int64_t transfer_started = esp_timer_get_time();
    firmware_transfer_context_t transfer = {
        .deadline = transfer_started + 180000000LL, .limit = install_size, .failure = ESP_OK,
    };
    esp_http_client_config_t config = {
        .url = install_url,
        .event_handler = firmware_http_event,
        .user_data = &transfer,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 10000,
        .keep_alive_enable = true,
        .buffer_size = 8192,
        .buffer_size_tx = 4096,
    };

    esp_https_ota_config_t ota_config = {
        .http_config = &config,
    };

    esp_https_ota_handle_t https_ota_handle = NULL;
    esp_err_t err = esp_https_ota_begin(&ota_config, &https_ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA begin failed: %s", esp_err_to_name(err));
        set_ota_state(OTA_STATE_ERROR, "Failed to start OTA update");
        return err;
    }

    int image_size = esp_https_ota_get_image_size(https_ota_handle);
    ESP_LOGI(TAG, "OTA image size: %d bytes", image_size);

    if (image_size > 0 && image_size != install_size) {
        esp_https_ota_abort(https_ota_handle);
        set_ota_state(OTA_STATE_ERROR, "Firmware size differs from release");
        return ESP_ERR_INVALID_SIZE;
    }
    esp_app_desc_t incoming;
    const esp_app_desc_t *current = esp_app_get_description();
    err = esp_https_ota_get_img_desc(https_ota_handle, &incoming);
    if (err != ESP_OK || strncmp(incoming.project_name, current->project_name,
                                 sizeof(incoming.project_name)) ||
        strncmp(incoming.version, install_version, sizeof(incoming.version))) {
        esp_https_ota_abort(https_ota_handle);
        set_ota_state(OTA_STATE_ERROR, "Firmware project or version mismatch");
        return ESP_ERR_OTA_VALIDATE_FAILED;
    }
    set_ota_state(OTA_STATE_INSTALLING, NULL);

    while (1) {
        if (esp_timer_get_time() - transfer_started > 180000000LL) { err = ESP_ERR_TIMEOUT; break; }
        err = esp_https_ota_perform(https_ota_handle);
        if (err != ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
            break;
        }

        int downloaded = esp_https_ota_get_image_len_read(https_ota_handle);
        if (downloaded > install_size) { err = ESP_ERR_INVALID_SIZE; break; }
        if (image_size > 0) {
            int progress = (downloaded * 100) / image_size;
            if (ota_status_mutex && xSemaphoreTake(ota_status_mutex, portMAX_DELAY) == pdTRUE) {
                ota_status.progress_percent = progress;
                xSemaphoreGive(ota_status_mutex);
            }
            ESP_LOGI(TAG, "OTA progress: %d%%", progress);
        }

        // Reset sleep timer periodically during OTA to prevent auto-sleep
        power_manager_reset_sleep_timer();

        vTaskDelay(pdMS_TO_TICKS(100));
    }

    if (transfer.failure != ESP_OK) err = transfer.failure;
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA perform failed: %s", esp_err_to_name(err));
        esp_https_ota_abort(https_ota_handle);
        set_ota_state(OTA_STATE_ERROR, "OTA update failed");
        return err;
    }

    // Verify the complete application bytes against the GitHub asset digest
    // before selecting the new boot slot. Partition reads include appended image hash.
    psa_hash_operation_t sha = PSA_HASH_OPERATION_INIT;
    unsigned char actual[32];
    unsigned char block[1024];
    err = (target && esp_https_ota_get_image_len_read(https_ota_handle) == install_size)
        ? ESP_OK : ESP_ERR_INVALID_SIZE;
    if (err == ESP_OK && (psa_crypto_init() != PSA_SUCCESS ||
                         psa_hash_setup(&sha, PSA_ALG_SHA_256) != PSA_SUCCESS)) err = ESP_FAIL;
    for (int offset = 0; err == ESP_OK && offset < install_size; offset += sizeof(block)) {
        size_t count = install_size - offset;
        if (count > sizeof(block)) count = sizeof(block);
        err = esp_partition_read(target, offset, block, count);
        if (err == ESP_OK && psa_hash_update(&sha, block, count) != PSA_SUCCESS) err = ESP_FAIL;
    }
    size_t digest_length = 0;
    if (err == ESP_OK && (psa_hash_finish(&sha, actual, sizeof(actual), &digest_length) != PSA_SUCCESS ||
                         digest_length != sizeof(actual))) err = ESP_FAIL;
    psa_hash_abort(&sha);
    char actual_hex[65];
    for (int i = 0; err == ESP_OK && i < 32; i++) snprintf(actual_hex + i * 2, 3, "%02x", actual[i]);
    if (err == ESP_OK && strcmp(actual_hex, install_digest) != 0) err = ESP_ERR_OTA_VALIDATE_FAILED;
    if (err != ESP_OK) {
        esp_https_ota_abort(https_ota_handle);
        set_ota_state(OTA_STATE_ERROR, "Release digest or size mismatch");
        return err;
    }
    ESP_LOGI(TAG, "Complete GitHub release digest verified");
    err = esp_https_ota_finish(https_ota_handle);
    if (err != ESP_OK) {
        if (err == ESP_ERR_OTA_VALIDATE_FAILED) {
            ESP_LOGE(TAG, "Image validation failed");
            set_ota_state(OTA_STATE_ERROR, "Firmware validation failed");
        } else {
            ESP_LOGE(TAG, "OTA finish failed: %s", esp_err_to_name(err));
            set_ota_state(OTA_STATE_ERROR, "Failed to finalize OTA update");
        }
        return err;
    }

    ESP_LOGI(TAG, "OTA update successful! Rebooting in 3 seconds...");
    set_ota_state(OTA_STATE_SUCCESS, NULL);
    if (ota_status_mutex && xSemaphoreTake(ota_status_mutex, portMAX_DELAY) == pdTRUE) {
        ota_status.progress_percent = 100;
        xSemaphoreGive(ota_status_mutex);
    }

    ESP_LOGI(TAG, "OTA worker minimum free stack: %u bytes",
             (unsigned)uxTaskGetStackHighWaterMark(NULL));
    ota_recovery_clear();
    vTaskDelay(pdMS_TO_TICKS(3000));
    esp_restart();

    return ESP_OK;
}

static void ota_update_task(void *pvParameter)
{
    if (utils_image_operation_begin(pdMS_TO_TICKS(5000))) {
        ota_install(false);
        // Ordinary failures return normally and can be retried. A panic leaves
        // the active marker for the next boot to turn into an automatic hold.
        if (recovery_record.phase == OTA_RECOVERY_ACTIVE) ota_recovery_clear();
        utils_image_operation_end();
    } else set_ota_state(OTA_STATE_ERROR, "Device operation busy");
    release_operation();
    vTaskDelete(NULL);
}

static esp_err_t ota_check_on_wake_worker(void)
{
    if (!utils_image_operation_begin(pdMS_TO_TICKS(5000))) {
        set_ota_state(OTA_STATE_ERROR, "Device operation busy"); release_operation();
        return ESP_ERR_TIMEOUT;
    }
    power_manager_reset_sleep_timer();
    set_ota_state(OTA_STATE_CHECKING, NULL);
    char version[32], url[256];
    esp_err_t err = fetch_github_release_info(version, sizeof(version), url, sizeof(url));
    if (err != ESP_OK) { set_ota_state(OTA_STATE_ERROR, "Cloud firmware check unavailable"); utils_image_operation_end(); release_operation(); return err; }
    xSemaphoreTake(ota_status_mutex, portMAX_DELAY);
    snprintf(ota_status.latest_version, sizeof(ota_status.latest_version), "%s", version);
    xSemaphoreGive(ota_status_mutex);
    update_available = cloud_update_offered;
    ota_update_last_check_time();
    if (!cloud_update_offered) {
        ESP_LOGI(TAG, "Cloud policy: no firmware update offered");
        set_ota_state(OTA_STATE_IDLE, NULL);
        utils_image_operation_end();
        release_operation();
        return ESP_OK;
    }
    snprintf(firmware_url, sizeof(firmware_url), "%s", url);
    ESP_LOGI(TAG, "Cloud policy offered %s; installing before wake completes", version);
    err = ota_install(true);
    if (recovery_record.phase == OTA_RECOVERY_ACTIVE) ota_recovery_clear();
    utils_image_operation_end();
    release_operation();
    return err;
}

typedef struct {
    StaticSemaphore_t semaphore_storage;
    SemaphoreHandle_t completed;
    esp_err_t result;
} ota_wake_completion_t;

static void ota_wake_task(void *parameter)
{
    ota_wake_completion_t *completion = parameter;
    esp_err_t result = ota_check_on_wake_worker();
    ESP_LOGI(TAG, "OTA wake worker completed (%s), minimum free stack: %u bytes",
             esp_err_to_name(result), (unsigned)uxTaskGetStackHighWaterMark(NULL));
    completion->result = result;
    SemaphoreHandle_t completed = completion->completed;
    // Last access to caller-owned completion storage precedes notification.
    // The caller never keeps or touches this task's handle after deletion.
    xSemaphoreGive(completed);
    vTaskDelete(NULL);
}

esp_err_t ota_check_on_wake(void)
{
    if (!claim_operation(OTA_STATE_CHECKING)) return ESP_ERR_INVALID_STATE;
    ota_wake_completion_t completion = {.result = ESP_FAIL};
    completion.completed = xSemaphoreCreateBinaryStatic(&completion.semaphore_storage);
    if (!completion.completed ||
        xTaskCreate(ota_wake_task, "ota_wake", OTA_WORKER_STACK_BYTES,
                    &completion, 5, NULL) != pdPASS) {
        if (completion.completed) vSemaphoreDelete(completion.completed);
        set_ota_state(OTA_STATE_ERROR, "Unable to allocate OTA wake worker");
        release_operation();
        return ESP_ERR_NO_MEM;
    }
    // The worker owns image/sleep serialization until bounded work returns.
    // Keep caller storage alive until its final notification, even on failure.
    xSemaphoreTake(completion.completed, portMAX_DELAY);
    esp_err_t result = completion.result;
    vSemaphoreDelete(completion.completed);
    return result;
}

esp_err_t ota_manager_init(void)
{
    ESP_LOGI(TAG, "Initializing OTA manager");

    // Zero out the entire ota_status struct to prevent garbage data
    memset(&ota_status, 0, sizeof(ota_status_t));
    ota_status.state = OTA_STATE_IDLE;

    // Create mutex for ota_status protection
    ota_status_mutex = xSemaphoreCreateMutex();
    if (ota_status_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create OTA status mutex");
        return ESP_ERR_NO_MEM;
    }

    // Get current firmware version
    const esp_app_desc_t *app_desc = esp_app_get_description();
    snprintf(ota_status.current_version, sizeof(ota_status.current_version), "%s",
             app_desc->version);

    ESP_LOGI(TAG, "Current firmware version: %s", ota_status.current_version);

    ota_recovery_load();

    // Load last known OTA status from NVS (latest_version, state)
    ota_load_status_from_nvs();

    // Mark current partition as valid (for rollback support)
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t ota_state;
    if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK) {
        if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
            ESP_LOGI(TAG, "First boot after OTA update, marking as valid");
            esp_ota_mark_app_valid_cancel_rollback();
        }
    }

    // Wake paths invoke the cloud policy synchronously. The timer registration
    // only persists last-check timestamps; its callback does no network work.
    periodic_tasks_register(OTA_CHECK_TASK_NAME, ota_check_periodic_callback,
                            OTA_CHECK_INTERVAL_SECONDS);

    return ESP_OK;
}

esp_err_t ota_check_for_update(bool *update_available_out, int timeout)
{
    if (!claim_operation(OTA_STATE_CHECKING)) return ESP_ERR_INVALID_STATE;
    update_available = false;
    if (xTaskCreate(&ota_check_task, "ota_check_task", 12288, NULL, 5, NULL) != pdPASS) {
        set_ota_state(OTA_STATE_ERROR, "Unable to start update check");
        release_operation();
        return ESP_ERR_NO_MEM;
    }

    // Read the worker's published state under the same synchronization used
    // to publish it. A caller timeout does not cancel the bounded worker.
    ota_state_t state = OTA_STATE_CHECKING;
    while (timeout > 0) {
        xSemaphoreTake(ota_status_mutex, portMAX_DELAY);
        state = ota_status.state;
        xSemaphoreGive(ota_status_mutex);
        if (state != OTA_STATE_CHECKING) break;
        vTaskDelay(pdMS_TO_TICKS(1000));
        timeout--;
    }
    xSemaphoreTake(ota_status_mutex, portMAX_DELAY);
    state = ota_status.state;
    if (update_available_out) *update_available_out = update_available;
    xSemaphoreGive(ota_status_mutex);
    if (state == OTA_STATE_CHECKING) return ESP_ERR_TIMEOUT;
    if (state == OTA_STATE_ERROR) return ESP_FAIL;

    return ESP_OK;
}

esp_err_t ota_start_update(void)
{
    if (!claim_operation(OTA_STATE_DOWNLOADING)) return ESP_ERR_INVALID_STATE;
    if (!update_available) {
        set_ota_state(OTA_STATE_IDLE, NULL);
        release_operation();
        return ESP_ERR_INVALID_STATE;
    }
    if (xTaskCreate(&ota_update_task, "ota_update_task", OTA_WORKER_STACK_BYTES, NULL, 5, NULL) != pdPASS) {
        set_ota_state(OTA_STATE_ERROR, "Unable to start update");
        release_operation();
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void ota_get_status(ota_status_t *status)
{
    if (status && ota_status_mutex) {
        if (xSemaphoreTake(ota_status_mutex, portMAX_DELAY) == pdTRUE) {
            memcpy(status, &ota_status, sizeof(ota_status_t));
            xSemaphoreGive(ota_status_mutex);
        }
    }
}

const char *ota_get_current_version(void)
{
    return ota_status.current_version;
}

bool ota_should_check_daily(void)
{
    return periodic_tasks_should_run(OTA_CHECK_TASK_NAME);
}

void ota_update_last_check_time(void)
{
    periodic_tasks_update_last_run(OTA_CHECK_TASK_NAME);
}

static esp_err_t ota_check_periodic_callback(void)
{
    return ESP_OK;
}

static void ota_save_status_to_nvs(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(OTA_NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS for saving OTA status: %s", esp_err_to_name(err));
        return;
    }

    // Save latest_version and state
    if (ota_status_mutex && xSemaphoreTake(ota_status_mutex, portMAX_DELAY) == pdTRUE) {
        err = nvs_set_str(nvs_handle, OTA_NVS_LATEST_VERSION_KEY, ota_status.latest_version);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to save latest_version to NVS: %s", esp_err_to_name(err));
        }

        err = nvs_set_u8(nvs_handle, OTA_NVS_STATE_KEY, (uint8_t) ota_status.state);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to save state to NVS: %s", esp_err_to_name(err));
        }

        xSemaphoreGive(ota_status_mutex);
    }

    nvs_commit(nvs_handle);
    nvs_close(nvs_handle);
}

static void ota_load_status_from_nvs(void)
{
    // Initialize to safe defaults first
    ota_status.latest_version[0] = '\0';
    ota_status.state = OTA_STATE_IDLE;

    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(OTA_NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "No saved OTA status in NVS (first boot or cleared), using defaults");
        return;
    }

    // Load latest_version
    size_t required_size = sizeof(ota_status.latest_version);
    err = nvs_get_str(nvs_handle, OTA_NVS_LATEST_VERSION_KEY, ota_status.latest_version,
                      &required_size);
    if (err != ESP_OK) {
        ota_status.latest_version[0] = '\0';
    }

    // Load state
    uint8_t saved_state = 0;
    err = nvs_get_u8(nvs_handle, OTA_NVS_STATE_KEY, &saved_state);
    if (err == ESP_OK) {
        ota_status.state = (ota_state_t) saved_state;
    } else {
        ota_status.state = OTA_STATE_IDLE;
    }

    nvs_close(nvs_handle);
}
