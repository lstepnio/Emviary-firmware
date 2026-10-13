#include "usb_recovery.h"
#include "board_hal.h"
#include "cJSON.h"
#include "config.h"
#include "config_manager.h"
#include "display_manager.h"
#include "driver/uart.h"
#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "mbedtls/sha256.h"
#include "nvs.h"
#include "ota_manager.h"
#include "power_manager.h"
#include "recovery_checkpoint.h"
#include "recovery_protocol.h"
#include "storage.h"
#include "utils.h"
#include "wifi_manager.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static recovery_lease_t lease, image_lease;
static recovery_checkpoint_t checkpoint;
static bool persistence_ready, marker_present, usb_seen, session_ever;
static StaticSemaphore_t checkpoint_mutex_storage;
static SemaphoreHandle_t checkpoint_mutex;
static char prior_stage[32], prior_error[48], diagnostic_error[48];
static const char *checkpoint_path = FS_MOUNT_POINT "/.usb_recovery.json";
static void checkpoint_tick(void);
static char boot_id[17], device_id[13], bundle[65], stage[32] = "boot", error[48] = "";
static char expected_sha[65], actual_sha[65], image_id[49];
static int network_count;
static const char *cloud = "idle", *image = "idle", *panel = "idle";
static bool ready, configured, started, fetch_pending, fetch_busy, worker_running;
static recovery_display_state_t display_test;
static const char *test_pattern = "idle";
static void worker(void *arg);
static uint64_t now_ms(void) { return esp_timer_get_time() / 1000; }
bool usb_recovery_active(void)
{
    portENTER_CRITICAL(&lock);
    bool active = recovery_lease_active(&lease, now_ms());
    portEXIT_CRITICAL(&lock);
    return active;
}
void usb_recovery_stage(const char *s, esp_err_t result)
{
    portENTER_CRITICAL(&lock);
    snprintf(stage, sizeof(stage), "%s", s);
    snprintf(error, sizeof(error), "%s", result == ESP_OK ? "" : esp_err_to_name(result));
    if (!strcmp(s, "configuration") && result == ESP_OK)
        configured = true;
    portEXIT_CRITICAL(&lock);
}
void usb_recovery_warning(const char *s, esp_err_t result)
{
    portENTER_CRITICAL(&lock);
    snprintf(diagnostic_error, sizeof(diagnostic_error), "%s:%s", s, esp_err_to_name(result));
    portEXIT_CRITICAL(&lock);
}
void usb_recovery_ready(void)
{
    if (!started)
        return;
    portENTER_CRITICAL(&lock);
    ready = true;
    portEXIT_CRITICAL(&lock);
    cJSON *networks = wifi_manager_get_networks(false);
    network_count = cJSON_GetArraySize(networks);
    cJSON_Delete(networks);
    usb_recovery_stage("ready", ESP_OK);
}
static const char *string(cJSON *root, const char *key)
{
    cJSON *v = cJSON_GetObjectItemCaseSensitive(root, key);
    return cJSON_IsString(v) ? v->valuestring : NULL;
}
static void respond(uint32_t seq, const char *command_error, bool reveal)
{
    char sid[65], nonce[65], st[32], err[48], sha[65], iid[49];
    bool active, config;
    const char *c, *i, *p;
    portENTER_CRITICAL(&lock);
    active = recovery_lease_active(&lease, now_ms());
    config = configured;
    snprintf(sid, sizeof(sid), "%s", reveal ? lease.session : "");
    snprintf(nonce, sizeof(nonce), "%s", reveal ? lease.nonce : "");
    snprintf(st, sizeof(st), "%s", stage);
    snprintf(err, sizeof(err), "%s", command_error ? command_error : error);
    snprintf(sha, sizeof(sha), "%s", actual_sha);
    snprintf(iid, sizeof(iid), "%s", image_id);
    c = cloud;
    i = image;
    p = panel;
    portEXIT_CRITICAL(&lock);
    cJSON *r = cJSON_CreateObject();
    if (!r)
        return;
    cJSON_AddNumberToObject(r, "protocol", 1);
    cJSON_AddStringToObject(r, "boot_id", boot_id);
    cJSON_AddStringToObject(r, "device_identity", device_id);
    cJSON_AddStringToObject(r, "firmware", esp_app_get_description()->version);
    cJSON_AddStringToObject(r, "board", "seeedstudio_reterminal_e1002");
    cJSON_AddNumberToObject(r, "reset_reason", esp_reset_reason());
    cJSON_AddStringToObject(r, "prior_stage", prior_stage);
    cJSON_AddStringToObject(r, "prior_error", prior_error);
    cJSON_AddStringToObject(r, "diagnostic_error", diagnostic_error);
    cJSON_AddStringToObject(r, "stage", st);
    cJSON_AddStringToObject(r, "error", err);
    cJSON_AddBoolToObject(r, "active", active);
    cJSON_AddStringToObject(r, "session_id", sid);
    cJSON_AddStringToObject(r, "nonce", nonce);
    const char *token = config ? config_manager_get_access_token() : NULL;
    const char *url = config ? config_manager_get_image_url() : NULL;
    cJSON_AddBoolToObject(r, "configuration_ready",
                          config && token && token[0] && url && url[0] && network_count > 0);
    cJSON_AddNumberToObject(r, "network_count", network_count);
    cJSON_AddNumberToObject(r, "config_revision",
                            config ? config_manager_get_config_last_updated() : 0);
    cJSON_AddBoolToObject(r, "storage_persistent",
                          persistence_ready && storage_has_persistent_storage());
    cJSON_AddBoolToObject(r, "wifi_connected", config && wifi_manager_is_connected());
    cJSON_AddStringToObject(r, "cloud", c);
    cJSON_AddStringToObject(r, "image", i);
    cJSON_AddStringToObject(r, "panel", p);
    cJSON_AddStringToObject(r, "test_pattern", test_pattern);
    cJSON_AddStringToObject(r, "image_sha256", sha);
    cJSON_AddStringToObject(r, "image_id", iid);
    char json[1024], framed[1072];
    if (cJSON_PrintPreallocated(r, json, sizeof(json), false)) {
        int n = recovery_frame_encode(framed, sizeof(framed), seq, json);
        if (n > 0)
            uart_write_bytes(UART_NUM_0, framed, n);
    }
    cJSON_Delete(r);
}
static void command(char *line)
{
    uint32_t seq;
    char *json;
    if (!recovery_frame_decode(line, &seq, &json))
        return;
    // Commands are flat objects. Bound parser recursion as well as wire size.
    bool quoted = false, escaped = false;
    unsigned depth = 0;
    for (const char *p = json; *p; p++) {
        if (quoted) {
            if (escaped)
                escaped = false;
            else if (*p == '\\')
                escaped = true;
            else if (*p == '"')
                quoted = false;
        } else if (*p == '"')
            quoted = true;
        else if (*p == '[' || (*p == '{' && ++depth > 1))
            return;
        else if (*p == '}' && depth)
            depth--;
    }
    cJSON *root = cJSON_Parse(json);
    if (!root)
        return;
    const char *cmd = string(root, "cmd"), *sid = string(root, "session_id"),
               *nonce = string(root, "nonce"), *b = string(root, "bundle_sha256");
    const char *err = NULL;
    bool reveal = false;
    ota_status_t ota = {0};
    if (cmd && !strcmp(cmd, "BEGIN_RECOVERY"))
        ota_get_status(&ota);
    bool ota_busy = ota.state == OTA_STATE_CHECKING || ota.state == OTA_STATE_DOWNLOADING ||
                    ota.state == OTA_STATE_INSTALLING;
    if (cmd && !strcmp(cmd, "HELLO")) { /* An unbound probe never discloses the nonce. */
    } else {
        portENTER_CRITICAL(&lock);
        if (cmd && !strcmp(cmd, "BEGIN_RECOVERY")) {
            bool same = recovery_lease_match(&lease, sid, nonce, now_ms());
            if (ota_busy)
                err = "operation_busy";
            else if (b && (strlen(b) != 64 || strspn(b, "0123456789abcdefABCDEF") != 64))
                err = "invalid_bundle";
            else if (fetch_busy && !same)
                err = "operation_busy";
            else if (!recovery_lease_begin(&lease, sid, nonce, now_ms()))
                err = "session_conflict";
            else {
                reveal = true;
                session_ever = true;
                if (!same) {
                    snprintf(bundle, sizeof(bundle), "%s", b ? b : "");
                    cloud = image = panel = "idle";
                    actual_sha[0] = image_id[0] = 0;
                    fetch_pending = true;
                    memset(&display_test, 0, sizeof(display_test));
                    test_pattern = "idle";
                }
            }
        } else if (!recovery_lease_match(&lease, sid, nonce, now_ms()))
            err = "session_mismatch";
        else if (cmd && !strcmp(cmd, "STATUS")) {
            lease.renewed = now_ms();
            reveal = true;
        } else if (cmd && !strcmp(cmd, "DISPLAY_TEST")) {
            if (!ready)
                err = "not_ready";
            else if (!recovery_display_request_test(&display_test))
                err = "test_already_used";
            else {
                test_pattern = "pending";
                reveal = true;
            }
        } else if (cmd && !strcmp(cmd, "RESTORE_PICTURE")) {
            if (!ready)
                err = "not_ready";
            else if (!recovery_display_request_restore(&display_test))
                err = "test_not_complete";
            else
                reveal = true;
        } else if (cmd && !strcmp(cmd, "END_RECOVERY")) {
            memset(&lease, 0, sizeof(lease));
            fetch_pending = false;
        } else
            err = "unknown_command";
        portEXIT_CRITICAL(&lock);
    }
    if (!err && cmd && !strcmp(cmd, "BEGIN_RECOVERY")) {
        // Close the race with an OTA claim made while BEGIN was being parsed.
        ota_get_status(&ota);
        if (ota.state == OTA_STATE_CHECKING || ota.state == OTA_STATE_DOWNLOADING ||
            ota.state == OTA_STATE_INSTALLING) {
            portENTER_CRITICAL(&lock);
            memset(&lease, 0, sizeof(lease));
            fetch_pending = false;
            portEXIT_CRITICAL(&lock);
            err = "operation_busy";
            reveal = false;
        }
    }
    if (!err && cmd && !strcmp(cmd, "BEGIN_RECOVERY") && !worker_running) {
        worker_running = true;
        if (xTaskCreate(worker, "recovery_fetch", 12288, NULL, 4, NULL) != pdPASS) {
            if (display_test.overlay && ready && utils_image_operation_begin(pdMS_TO_TICKS(1000))) {
                display_manager_restore_saved_artwork();
                utils_image_operation_end();
                display_test.overlay = false;
            }
            worker_running = false;
            usb_recovery_stage("recovery_worker", ESP_ERR_NO_MEM);
        }
    }
    respond(seq, err, reveal);
    cJSON_Delete(root);
}
static void reader(void *arg)
{
    char line[RECOVERY_JSON_MAX + 64];
    size_t used = 0;
    bool discard = false;
    unsigned char byte;
    uint64_t next_usb_check = 0;
    unsigned missing_usb = 0;
    for (;;) {
        int n = uart_read_bytes(UART_NUM_0, &byte, 1, pdMS_TO_TICKS(100));
        if (n == 1) {
            if (byte == '\n') {
                if (!discard && used) {
                    line[used] = 0;
                    command(line);
                }
                used = 0;
                discard = false;
            } else if (byte != '\r') {
                if (used < sizeof(line) - 1 && !discard)
                    line[used++] = (char)byte;
                else
                    discard = true;
            }
        }
        if (ready && usb_recovery_active() && now_ms() >= next_usb_check) {
            next_usb_check = now_ms() + 500;
            bool usb = board_hal_is_usb_connected();
            if (usb) {
                usb_seen = true;
                missing_usb = 0;
            } else if (usb_seen && ++missing_usb >= 3) {
                portENTER_CRITICAL(&lock);
                memset(&lease, 0, sizeof(lease));
                fetch_pending = false;
                portEXIT_CRITICAL(&lock);
            }
        }
        checkpoint_tick();
    }
}
static void worker(void *arg)
{
    while (usb_recovery_active()) {
        bool do_fetch = false, do_test = false, do_restore = false;
        portENTER_CRITICAL(&lock);
        if (recovery_lease_active(&lease, now_ms()) && fetch_pending && ready) {
            fetch_pending = false;
            fetch_busy = true;
            do_fetch = true;
        }
        if (display_test.test_pending && ready && !fetch_busy) {
            display_test.test_pending = false;
            do_test = true;
            fetch_busy = true;
        }
        if (display_test.restore_pending && ready && !fetch_busy) {
            display_test.restore_pending = false;
            do_restore = true;
            display_test.restoring = true;
            fetch_busy = true;
        }
        portEXIT_CRITICAL(&lock);
        if (do_restore) {
            esp_err_t result = ESP_ERR_TIMEOUT;
            if (utils_image_operation_begin(pdMS_TO_TICKS(1000))) {
                result = display_manager_restore_saved_artwork();
                utils_image_operation_end();
            }
            portENTER_CRITICAL(&lock);
            test_pattern = result == ESP_OK ? "restored" : "error";
            fetch_busy = false;
            display_test.restoring = false;
            if (result == ESP_OK) {
                display_test.overlay = false;
                display_test.restored = true;
            }
            portEXIT_CRITICAL(&lock);
            usb_recovery_stage("picture_restored", result);
        }
        if (do_test) {
            const char *lines[] = {"Emviary display test",
                                   "If you can read this, the panel refresh works.",
                                   "Your saved picture will return when recovery ends."};
            esp_err_t result = ESP_ERR_TIMEOUT;
            if (utils_image_operation_begin(pdMS_TO_TICKS(1000))) {
                display_test.overlay = true;
                result = display_manager_show_diagnostics(lines, 3);
                utils_image_operation_end();
            }
            portENTER_CRITICAL(&lock);
            test_pattern = result == ESP_OK ? "complete" : "error";
            fetch_busy = false;
            portEXIT_CRITICAL(&lock);
            usb_recovery_stage("display_test", result);
        }
        if (do_fetch) {
            uint64_t deadline = now_ms() + 60000;
            while (usb_recovery_active() && !wifi_manager_is_connected() && now_ms() < deadline)
                vTaskDelay(pdMS_TO_TICKS(250));
            if (usb_recovery_active()) {
                esp_err_t result =
                    wifi_manager_is_connected()
                        ? fetch_and_display_image_from_url(config_manager_get_image_url(), NULL)
                        : ESP_ERR_TIMEOUT;
                if (result != ESP_OK)
                    usb_recovery_fetch_failed(result);
            }
            portENTER_CRITICAL(&lock);
            fetch_busy = false;
            portEXIT_CRITICAL(&lock);
        }
        vTaskDelay(pdMS_TO_TICKS(250));
    }
    if (display_test.overlay && ready && utils_image_operation_begin(pdMS_TO_TICKS(1000))) {
        display_manager_restore_saved_artwork();
        utils_image_operation_end();
        display_test.overlay = false;
    }
    worker_running = false;
    vTaskDelete(NULL);
}
void usb_recovery_start(void)
{
#if CONFIG_BOARD_DRIVER_SEEEDSTUDIO_RETERMINAL_E1002
    if (started)
        return;
    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);
    snprintf(device_id, sizeof(device_id), "%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2],
             mac[3], mac[4], mac[5]);
    snprintf(boot_id, sizeof(boot_id), "%08lx%08lx", (unsigned long)esp_random(),
             (unsigned long)esp_random());
    /* E1002 USB bridge routes to ROM/console UART0. Do not remap pins or toggle DTR/RTS here. */
    if (!uart_is_driver_installed(UART_NUM_0) &&
        uart_driver_install(UART_NUM_0, 1024, 0, 0, NULL, 0) != ESP_OK)
        return;
    if (xTaskCreate(reader, "recovery_uart", 6144, NULL, 5, NULL) != pdPASS)
        return;
    started = true;

#endif
}
void usb_recovery_add_headers(esp_http_client_handle_t client)
{
    portENTER_CRITICAL(&lock);
    bool active = recovery_lease_active(&lease, now_ms());
    image_lease = lease;
    expected_sha[0] = actual_sha[0] = image_id[0] = 0;
    portEXIT_CRITICAL(&lock);
    if (!active)
        return;
    usb_recovery_stage("cloud_request", ESP_OK);
    esp_http_client_set_header(client, "X-Emviary-Recovery-Session", image_lease.session);
    esp_http_client_set_header(client, "X-Emviary-Recovery-Nonce", image_lease.nonce);
    esp_http_client_set_header(client, "X-Emviary-Recovery-Boot", boot_id);
    esp_http_client_set_header(client, "X-Emviary-Recovery-Device-Id", device_id);
    esp_http_client_set_header(client, "X-Emviary-Recovery-Bundle-Sha256", bundle);
    /* Require a full body and actual driver refresh, even if normal ETag matches. */
    esp_http_client_delete_header(client, "If-None-Match");
}
void usb_recovery_response_header(const char *key, const char *value)
{
    if (!usb_recovery_active())
        return;
    portENTER_CRITICAL(&lock);
    if (!strcasecmp(key, "X-Recovery-Image-Sha256") && strlen(value) == 64 &&
        strspn(value, "0123456789abcdef") == 64)
        snprintf(expected_sha, sizeof(expected_sha), "%s", value);
    if (!strcasecmp(key, "X-Recovery-Image-Id"))
        snprintf(image_id, sizeof(image_id), "%s", value);
    portEXIT_CRITICAL(&lock);
}
static bool image_session_current(void)
{
    portENTER_CRITICAL(&lock);
    bool match = recovery_lease_match(&lease, image_lease.session, image_lease.nonce, now_ms());
    portEXIT_CRITICAL(&lock);
    return match;
}
static void ack(const char *phase, esp_err_t result)
{
    if (!image_session_current())
        return;
    const char *url = config_manager_get_image_url(), *host = strstr(url, "://");
    if (!host)
        return;
    const char *path = strchr(host + 3, '/');
    if (!path || strncmp(url, "https://", 8))
        return;
    char endpoint[IMAGE_URL_MAX_LEN];
    size_t origin = path - url;
    if (origin + 18 >= sizeof(endpoint))
        return;
    snprintf(endpoint, sizeof(endpoint), "%.*s/v1/recovery/ack", (int)origin, url);
    cJSON *r = cJSON_CreateObject();
    if (!r)
        return;
    cJSON_AddStringToObject(r, "session_id", image_lease.session);
    cJSON_AddStringToObject(r, "nonce", image_lease.nonce);
    cJSON_AddStringToObject(r, "boot_id", boot_id);
    cJSON_AddStringToObject(r, "device_id", device_id);
    cJSON_AddStringToObject(r, "firmware", esp_app_get_description()->version);
    cJSON_AddStringToObject(r, "bundle_sha256", bundle);
    cJSON_AddStringToObject(r, "phase", phase);
    cJSON_AddStringToObject(r, "image_sha256", actual_sha);
    if (result != ESP_OK)
        cJSON_AddStringToObject(r, "error", esp_err_to_name(result));
    char *body = cJSON_PrintUnformatted(r);
    cJSON_Delete(r);
    if (!body)
        return;
    esp_http_client_config_t cfg = {.url = endpoint,
                                    .method = HTTP_METHOD_POST,
                                    .timeout_ms = 4000,
                                    .is_async = true,
                                    .disable_auto_redirect = true,
                                    .crt_bundle_attach = esp_crt_bundle_attach};
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (client) {
        char auth[1024];
        const char *token = config_manager_get_access_token();
        snprintf(auth, sizeof(auth), "Bearer %s", token ? token : "");
        esp_http_client_set_header(client, "Authorization", auth);
        esp_http_client_set_header(client, "Content-Type", "application/json");
        esp_http_client_set_post_field(client, body, strlen(body));
        esp_err_t sent;
        uint64_t deadline = now_ms() + 8000;
        do {
            sent = esp_http_client_perform(client);
            if (sent == ESP_ERR_HTTP_EAGAIN)
                vTaskDelay(pdMS_TO_TICKS(20));
        } while (sent == ESP_ERR_HTTP_EAGAIN && now_ms() < deadline && image_session_current());
        int status = esp_http_client_get_status_code(client);
        portENTER_CRITICAL(&lock);
        cloud = sent == ESP_OK && status >= 200 && status < 300 ? "authenticated" : "error";
        portEXIT_CRITICAL(&lock);
        esp_http_client_cleanup(client);
    }
    free(body);
}
esp_err_t usb_recovery_verify_image(const char *path)
{
    if (!image_session_current())
        return ESP_OK;
    FILE *f = fopen(path, "rb");
    if (!f)
        return ESP_FAIL;
    unsigned char chunk[1024], digest[32] = {0};
    mbedtls_sha256_context sha;
    mbedtls_sha256_init(&sha);
    int rc = mbedtls_sha256_starts(&sha, 0);
    size_t n;
    while (!rc && (n = fread(chunk, 1, sizeof(chunk), f)))
        rc = mbedtls_sha256_update(&sha, chunk, n);
    bool failed = ferror(f);
    fclose(f);
    if (!rc)
        rc = mbedtls_sha256_finish(&sha, digest);
    mbedtls_sha256_free(&sha);
    char hex[65];
    for (int i = 0; i < 32; i++)
        snprintf(hex + i * 2, 3, "%02x", digest[i]);
    portENTER_CRITICAL(&lock);
    bool match = !rc && !failed && expected_sha[0] && !strcmp(expected_sha, hex);
    if (match) {
        snprintf(actual_sha, sizeof(actual_sha), "%s", hex);
    }
    image = match ? "verified" : "error";
    portEXIT_CRITICAL(&lock);
    if (!match)
        return ESP_ERR_INVALID_CRC;
    usb_recovery_stage("image_verified", ESP_OK);
    ack("image_verified", ESP_OK);
    return ESP_OK;
}
void usb_recovery_panel_result(esp_err_t result)
{
    if (!image_session_current())
        return;
    portENTER_CRITICAL(&lock);
    panel = result == ESP_OK ? "complete" : "error";
    portEXIT_CRITICAL(&lock);
    usb_recovery_stage("panel", result);
    ack(result == ESP_OK ? "panel_refreshed" : "failed", result);
}
void usb_recovery_fetch_failed(esp_err_t result)
{
    if (!usb_recovery_active())
        return;
    portENTER_CRITICAL(&lock);
    snprintf(error, sizeof(error), "%s", esp_err_to_name(result));
    if (!strcmp(image, "idle"))
        image = "error";
    portEXIT_CRITICAL(&lock);
    ack("failed", result);
}

// These records contain only non-secret stage/error/session identity. Each session
// replaces one small file and commits at most sixteen NVS checkpoints, never logs.
static esp_err_t marker_store(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open("usb_recovery", NVS_READWRITE, &h);
    if (err != ESP_OK)
        return err;
    err = nvs_set_blob(h, "checkpoint", &checkpoint, sizeof(checkpoint));
    if (err == ESP_OK)
        err = nvs_commit(h);
    nvs_close(h);
    return err;
}
void usb_recovery_persistence_ready(void)
{
#if CONFIG_BOARD_DRIVER_SEEEDSTUDIO_RETERMINAL_E1002
    checkpoint_mutex = xSemaphoreCreateMutexStatic(&checkpoint_mutex_storage);
    nvs_handle_t h;
    size_t size = sizeof(checkpoint);
    if (nvs_open("usb_recovery", NVS_READONLY, &h) == ESP_OK) {
        esp_err_t err = nvs_get_blob(h, "checkpoint", &checkpoint, &size);
        nvs_close(h);
        if (err == ESP_OK && size == sizeof(checkpoint) && recovery_checkpoint_valid(&checkpoint)) {
            snprintf(prior_stage, sizeof(prior_stage), "%s", checkpoint.stage);
            snprintf(prior_error, sizeof(prior_error), "%s", checkpoint.error);
            bool scheduled = esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_UNDEFINED;
            marker_present = true;
            if (recovery_checkpoint_resume(&checkpoint, time(NULL), scheduled))
                marker_store();
            else {
                checkpoint.active = 0;
                usb_recovery_cleanup();
            }
        }
    }
    persistence_ready = true;
#endif
}
void usb_recovery_cleanup(void)
{
    if (!checkpoint_mutex)
        return;
    xSemaphoreTake(checkpoint_mutex, portMAX_DELAY);
    if (!marker_present) {
        xSemaphoreGive(checkpoint_mutex);
        return;
    }
    // Call before storage unmount as well as on END/lease expiry. Cleanup is
    // idempotent and does not enable ordinary debug capture or preserve a nonce.
    marker_present = false;
    nvs_handle_t h;
    if (nvs_open("usb_recovery", NVS_READWRITE, &h) == ESP_OK) {
        nvs_erase_key(h, "checkpoint");
        nvs_commit(h);
        nvs_close(h);
    }
    if (storage_has_persistent_storage())
        unlink(checkpoint_path);
    memset(&checkpoint, 0, sizeof(checkpoint));
    usb_seen = false;
    xSemaphoreGive(checkpoint_mutex);
}
static void checkpoint_tick(void)
{
    if (!persistence_ready)
        return;
    if (!usb_recovery_active()) {
        if (session_ever)
            usb_recovery_cleanup();
        return;
    }
    if (xSemaphoreTake(checkpoint_mutex, 0) != pdTRUE)
        return;
    char sid[65], st[32], err[48];
    portENTER_CRITICAL(&lock);
    snprintf(sid, sizeof(sid), "%s", lease.session);
    snprintf(st, sizeof(st), "%s", stage);
    snprintf(err, sizeof(err), "%s", error);
    portEXIT_CRITICAL(&lock);
    if (!marker_present || strcmp(checkpoint.session, sid)) {
        memset(&checkpoint, 0, sizeof(checkpoint));
        checkpoint.magic = RECOVERY_CHECKPOINT_MAGIC;
        checkpoint.active = 1;
        checkpoint.boots_left = RECOVERY_CHECKPOINT_BOOTS;
        time_t now = time(NULL);
        checkpoint.expires_at = now >= 1735689600 ? now + 600 : 0;
        snprintf(checkpoint.session, sizeof(checkpoint.session), "%s", sid);
        marker_present = true;
    } else if (!strcmp(checkpoint.stage, st) && !strcmp(checkpoint.error, err)) {
        xSemaphoreGive(checkpoint_mutex);
        return;
    }
    if (!recovery_checkpoint_write_allowed(&checkpoint)) {
        xSemaphoreGive(checkpoint_mutex);
        return;
    }
    snprintf(checkpoint.stage, sizeof(checkpoint.stage), "%s", st);
    snprintf(checkpoint.error, sizeof(checkpoint.error), "%s", err);
    checkpoint.writes++;
    if (marker_store() != ESP_OK) {
        snprintf(diagnostic_error, sizeof(diagnostic_error), "checkpoint_nvs");
        checkpoint.writes = RECOVERY_CHECKPOINT_WRITES;
        xSemaphoreGive(checkpoint_mutex);
        return;
    }
    if (!storage_has_persistent_storage()) {
        xSemaphoreGive(checkpoint_mutex);
        return;
    }
    FILE *f = fopen(checkpoint_path, "w");
    if (!f) {
        snprintf(diagnostic_error, sizeof(diagnostic_error), "checkpoint_storage");
        xSemaphoreGive(checkpoint_mutex);
        return;
    }
    int written =
        fprintf(f, "{\"boot_id\":\"%s\",\"stage\":\"%s\",\"error\":\"%s\",\"commit\":%u}\n",
                boot_id, st, err, checkpoint.writes);
    int close_result = fclose(f);
    if (written < 0 || close_result != 0)
        snprintf(diagnostic_error, sizeof(diagnostic_error), "checkpoint_storage");
    xSemaphoreGive(checkpoint_mutex);
}
