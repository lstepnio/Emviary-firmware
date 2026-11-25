/* Compile the actual lifecycle module with bounded fake transport/panel/NVS. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "gift_mode.h"
#include "gift_test_platform.h"
static unsigned char durable[256];
static size_t durable_size;
static bool fail_first, fail_display, fail_commit, fail_validate;
void gift_mode_test_reset(void);
static int64_t clock_us = 2000000;
static unsigned paints, downloads, waits;
static bool online;
static char last_report[20];
static const char *control = "{\"version\":1,\"state\":\"none\"}";
bool wifi_manager_is_connected(void) { return online; }
void vTaskDelay(unsigned ticks) { clock_us += (int64_t) ticks * 1000; waits++; }
static char last_path[200];

esp_err_t nvs_open(const char *a, int b, nvs_handle_t *c)
{
    (void) a;
    (void) b;
    *c = 1;
    return 0;
}
esp_err_t nvs_get_blob(nvs_handle_t a, const char *b, void *c, size_t *d)
{
    (void) a;
    (void) b;
    if (!durable_size)
        return -1;
    memcpy(c, durable, durable_size);
    *d = durable_size;
    return 0;
}
esp_err_t nvs_set_blob(nvs_handle_t a, const char *b, const void *c, size_t d)
{
    (void) a;
    (void) b;
    if (fail_commit)
        return -1;
    assert(d <= sizeof(durable));
    memcpy(durable, c, d);
    durable_size = d;
    return 0;
}
esp_err_t nvs_commit(nvs_handle_t a)
{
    (void) a;
    return fail_commit ? -1 : 0;
}
void nvs_close(nvs_handle_t a)
{
    (void) a;
}
bool storage_has_persistent_storage(void)
{
    return true;
}
const char *config_manager_get_image_url(void)
{
    return "https://frame.example/v1/image";
}
void config_manager_set_image_etag(const char *a)
{
    assert(!*a);
}
esp_err_t display_manager_show_image(const char *a)
{
    paints++;
    snprintf(last_path, sizeof(last_path), "%s", a);
    return fail_display ? -1 : 0;
}
esp_err_t display_manager_restore_artwork(void)
{
    return 0;
}
bool recovery_image_sha256(const char *a, char b[65])
{
    (void) a;
    memset(b, 'a', 64);
    b[64] = 0;
    return true;
}
int GUI_ValidateEPDGZ(const char *a)
{
    (void) a;
    return fail_validate ? -1 : 0;
}
int64_t esp_timer_get_time(void)
{
    return clock_us;
}
bool utils_image_operation_begin(int a)
{
    (void) a;
    return true;
}
void utils_image_operation_end(void) {}
esp_err_t utils_gift_download(const char *u, const char *p, size_t n)
{
    downloads++;
    if (fail_first && strstr(u, "first"))
        return -1;
    FILE *f = fopen(p, "wb");
    if (!f)
        return -1;
    if (strstr(u, "/v1/gift")) {
        cJSON *metrics = cJSON_CreateObject();
        gift_mode_add_metrics(metrics);
        cJSON *state = cJSON_GetObjectItem(metrics, "gift_state");
        snprintf(last_report, sizeof(last_report), "%s", cJSON_IsString(state) ? state->valuestring : "idle");
        cJSON_Delete(metrics);
        assert(strlen(control) < n);
        fputs(control, f);
    } else for (size_t i = 0; i < n; i++)
        fputc('x', f);
    fclose(f);
    return 0;
}
static char manifest[2200];
static const char *package(const char *id, const char *hash, const char *origin)
{
    snprintf(manifest, sizeof(manifest),
             "{\"version\":1,\"id\":\"%s\",\"state\":\"armed\",\"welcome\":{\"url\":\"%s/"
             "welcome\",\"sha256\":\"%s\",\"size\":100},\"first\":{\"url\":\"%s/"
             "first\",\"sha256\":\"%s\",\"size\":100},\"hold_seconds\":1800}",
             id, origin, hash, origin, hash);
    return manifest;
}
int main(void)
{
    mkdir(FS_MOUNT_POINT, 0700);
    remove(FS_MOUNT_POINT "/gift/0123456789abcdef0123456789abcdef-done.epdgz");
    remove(FS_MOUNT_POINT "/gift/1123456789abcdef0123456789abcdef-done.epdgz");
    char hash[65];
    memset(hash, 'a', 64);
    hash[64] = 0;
    const char *id = "0123456789abcdef0123456789abcdef";
    assert(!gift_mode_active());
    assert(!gift_mode_hold());
    assert(gift_mode_apply("{broken") != 0);
    assert(gift_mode_apply(package(id, hash, "https://evil.example")) != 0);
    assert(downloads == 0);
    fail_first = true;
    assert(gift_mode_apply(package(id, hash, "https://frame.example")) != 0);
    assert(!gift_mode_active());
    assert(paints == 0);
    fail_first = false;
    hash[0] = 'b';
    assert(gift_mode_apply(package(id, hash, "https://frame.example")) != 0);
    assert(!gift_mode_active());
    assert(paints == 0);
    hash[0] = 'a';
    fail_display = true;
    assert(gift_mode_apply(package(id, hash, "https://frame.example")) != 0);
    assert(gift_mode_active());
    fail_display = false;
    assert(gift_mode_apply(package(id, hash, "https://frame.example")) == 0);
    assert(gift_mode_active());
    assert(strstr(last_path, "welcome"));
    unsigned boot_paints = paints;
    gift_mode_test_reset();
    assert(gift_mode_active());
    assert(gift_mode_resume() == 0);
    assert(paints == boot_paints); /* retained welcome: no boot repaint */
    assert(gift_mode_note_diagnostics() == 0);
    gift_mode_test_reset();
    assert(gift_mode_resume() == 0);
    assert(paints == boot_paints + 1); /* interrupted diagnostic overlay recovery */
    unsigned before = paints;
    assert(gift_mode_apply(package(id, hash, "https://frame.example")) == 0);
    assert(paints == before);
    assert(gift_mode_reveal() == 0);
    assert(gift_mode_active());
    assert(paints == before); /* activation hold */
    clock_us += 2000000;
    fail_display = true;
    assert(gift_mode_reveal() != 0);
    assert(gift_mode_active());
    clock_us += 2000000;
    fail_display = false;
    fail_commit = true;
    assert(gift_mode_reveal() != 0);
    assert(gift_mode_active());
    clock_us += 2000000;
    fail_commit = false;
    assert(gift_mode_reveal() == 0);
    assert(!gift_mode_active());
    assert(gift_mode_hold());
    assert(strstr(last_path, "first"));
    before = paints;
    assert(gift_mode_apply(package(id, hash, "https://frame.example")) == 0);
    assert(!gift_mode_active());
    assert(paints == before); /* no rearm */
    gift_mode_end_hold();
    assert(!gift_mode_hold());
    const char *second = "1123456789abcdef0123456789abcdef";
    assert(gift_mode_apply(package(second, hash, "https://frame.example")) == 0);
    assert(gift_mode_active());
    assert(gift_mode_apply("{\"version\":1,\"state\":\"none\"}") == 0);
    assert(gift_mode_active());
    assert(gift_mode_apply("{\"version\":1,\"id\":\"0123456789abcdef0123456789abcdef\",\"state\":"
                           "\"cancelled\"}") == 0);
    assert(gift_mode_active());
    assert(gift_mode_apply("{\"version\":1,\"id\":\"1123456789abcdef0123456789abcdef\",\"state\":"
                           "\"cancelled\"}") == 0);
    assert(!gift_mode_active());
    before = paints;
    assert(gift_mode_apply(package(second, hash, "https://frame.example")) == 0);
    assert(!gift_mode_active());
    assert(paints == before);
    assert(gift_mode_apply(package(id, hash, "https://frame.example")) == 0);
    assert(!gift_mode_active()); /* completed old id cannot rearm after newer id */
    /* Ready does not reveal on power-on until the owner seals the gift. */
    const char *third = "2123456789abcdef0123456789abcdef";
    remove(FS_MOUNT_POINT "/gift/2123456789abcdef0123456789abcdef-done.epdgz");
    assert(gift_mode_apply(package(third, hash, "https://frame.example")) == 0);
    gift_mode_test_reset();
    before = paints;
    assert(gift_mode_on_boot(true) == 0);
    assert(gift_mode_active() && paints == before);
    const char *wrap = "{\"version\":1,\"id\":\"2123456789abcdef0123456789abcdef\",\"state\":\"wrapped\"}";
    fail_commit = true;
    assert(gift_mode_apply(wrap) != 0);
    fail_commit = false;
    gift_mode_test_reset();
    assert(gift_mode_on_boot(true) == 0 && paints == before);
    assert(gift_mode_apply(wrap) == 0);
    assert(gift_mode_apply(wrap) == 0); /* idempotent seal */
    gift_mode_test_reset();
    assert(gift_mode_on_boot(false) == 0 && paints == before); /* software/timer/green */
    assert(gift_mode_note_diagnostics() == 0);
    gift_mode_test_reset();
    assert(gift_mode_on_boot(false) == 0); /* overlay recovery retains sealed state */
    gift_mode_test_reset();
    unsigned offline_downloads = downloads;
    fail_display = true;
    assert(gift_mode_on_boot(true) != 0 && gift_mode_active());
    fail_display = false;
    gift_mode_test_reset();
    assert(gift_mode_on_boot(true) == 0);
    assert(!gift_mode_active() && gift_mode_hold());
    assert(strstr(last_path, "first") && downloads == offline_downloads);
    before = paints;
    gift_mode_test_reset();
    assert(gift_mode_on_boot(true) == 0 && paints == before); /* exactly once */
    const char *fourth = "3123456789abcdef0123456789abcdef";
    remove(FS_MOUNT_POINT "/gift/3123456789abcdef0123456789abcdef-done.epdgz");
    assert(gift_mode_apply(package(fourth, hash, "https://frame.example")) == 0);
    assert(gift_mode_apply("{\"version\":1,\"id\":\"3123456789abcdef0123456789abcdef\",\"state\":\"wrapped\"}") == 0);
    assert(gift_mode_apply("{\"version\":1,\"id\":\"3123456789abcdef0123456789abcdef\",\"state\":\"cancelled\"}") == 0);
    before = paints;
    gift_mode_test_reset();
    assert(gift_mode_on_boot(true) == 0 && !gift_mode_active() && paints == before);
    const char *fifth = "4123456789abcdef0123456789abcdef";
    remove(FS_MOUNT_POINT "/gift/4123456789abcdef0123456789abcdef-done.epdgz");
    assert(gift_mode_apply(package(fifth, hash, "https://frame.example")) == 0);
    unsigned before_downloads = downloads;
    gift_mode_setup_window();
    assert(downloads == before_downloads && waits == 0); /* never starts Wi-Fi */
    online = true;
    gift_mode_setup_window();
    assert(waits == 24 && !gift_mode_setup_busy()); /* bounded once-per-boot setup */
    before_downloads = downloads;
    gift_mode_setup_window();
    assert(downloads == before_downloads);
    gift_mode_test_reset();
    control = "{\"version\":1,\"id\":\"4123456789abcdef0123456789abcdef\",\"state\":\"wrapped\"}";
    gift_mode_setup_window();
    assert(waits == 25 && gift_mode_active() && !gift_mode_setup_busy());
    gift_mode_test_reset();
    assert(gift_mode_on_boot(true) == ESP_OK && !gift_mode_active());
    const char *sixth = "5123456789abcdef0123456789abcdef";
    remove(FS_MOUNT_POINT "/gift/5123456789abcdef0123456789abcdef-done.epdgz");
    assert(gift_mode_apply(package(sixth, hash, "https://frame.example")) == 0);
    gift_mode_test_reset();
    control = "{\"version\":1,\"id\":\"5123456789abcdef0123456789abcdef\",\"state\":\"wrapped\"}";
    assert(gift_mode_sync() == ESP_OK);
    assert(!strcmp(last_report, "wrapped")); /* standalone green/local sync reports its new state */
    gift_mode_test_reset();
    assert(gift_mode_on_boot(true) == ESP_OK && !gift_mode_active());
    assert(gift_metrics_header_fits(1533));
    assert(!gift_metrics_header_fits(1534));
    cJSON *large = cJSON_CreateObject();
    char escaped[33];
    memset(escaped, 1, 32);
    escaped[32] = 0;
    for (unsigned i = 0; i < 5; i++) {
        char key[16];
        snprintf(key, sizeof(key), "ssid%u", i);
        cJSON_AddStringToObject(large, key, escaped);
    }
    char optional[1000];
    memset(optional, 'x', 999);
    optional[999] = 0;
    cJSON_AddStringToObject(large, "optional_inventory", optional);
    char *plain = cJSON_PrintUnformatted(large);
    cJSON_Delete(large);
    assert(!gift_metrics_header_fits(strlen(plain)));
    plain = gift_mode_bound_metrics(plain);
    assert(plain && gift_metrics_header_fits(strlen(plain)));
    cJSON *minimal = cJSON_Parse(plain);
    free(plain);
    assert(minimal);
    assert(cJSON_GetObjectItem(minimal, "gift_version")->valueint == 2);
    assert(cJSON_IsString(cJSON_GetObjectItem(minimal, "gift_id")));
    assert(cJSON_IsString(cJSON_GetObjectItem(minimal, "gift_state")));
    assert(cJSON_IsTrue(cJSON_GetObjectItem(minimal, "metrics_trimmed")));
    cJSON_Delete(minimal);
    assert(gift_id_valid(id));
    assert(!gift_id_valid("../evil"));
    assert(!gift_hash_valid("hello"));
    assert(gift_hold_active(100, 1900));
    assert(!gift_hold_active(100, 1901));
    assert(!gift_hold_active(1900, 1900));
    puts(
        "gift lifecycle: staging, origin, integrity, checked display, retries, idempotency, "
        "cancellation, hold passed");
    return 0;
}

TaskHandle_t xTaskGetCurrentTaskHandle(void)
{
    return (void *) 1;
}
esp_err_t fetch_and_display_image_from_url(const char *u, bool *b)
{
    (void) u;
    *b = false;
    return 0;
}

void emviary_navigation_discard_pending(void) {}
