#include "gift_mode.h"

#include "https_origin.h"
#ifdef GIFT_HOST_TEST
#include "gift_test_platform.h"
#else
#include "GUI_EPDGZfile.h"
#include "config.h"
#include "config_manager.h"
#include "display_manager.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "https_origin.h"
#include "navigation_state.h"
#include "nvs.h"
#include "recovery_image_digest.h"
#include "storage.h"
#include "utils.h"
#endif
#include <dirent.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define GIFT_DIR FS_MOUNT_POINT "/gift"
typedef struct {
    uint32_t version;
    char id[37];
    uint32_t state;
    uint32_t overlay;
    int64_t hold_until;
} gift_record_t;
enum { GIFT_NONE, GIFT_PENDING, GIFT_READY, GIFT_OPENED, GIFT_CANCELLED };
static gift_record_t record;
static bool loaded;
static atomic_bool staging;
static _Atomic(TaskHandle_t) publishing_task;
static char error[81];
static char error_id[37];
/* Suppress taps made during the long activation/reveal refresh. */
static atomic_uint navigation_after_ms;
static void load(void)
{
    if (loaded)
        return;
    loaded = true;
    nvs_handle_t nvs;
    if (nvs_open("gift", NVS_READONLY, &nvs) == ESP_OK) {
        size_t n = sizeof(record);
        esp_err_t read_result = nvs_get_blob(nvs, "record", &record, &n);
        record.id[sizeof(record.id) - 1] = 0;
        if (read_result != ESP_OK || n != sizeof(record) || record.version != 1 ||
            !gift_id_valid(record.id) || record.state > GIFT_CANCELLED)
            memset(&record, 0, sizeof(record));
        nvs_close(nvs);
    }
}
static esp_err_t save(gift_record_t next)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open("gift", NVS_READWRITE, &nvs);
    if (err != ESP_OK)
        return err;
    next.version = 1;
    err = nvs_set_blob(nvs, "record", &next, sizeof(next));
    if (err == ESP_OK)
        err = nvs_commit(nvs);
    nvs_close(nvs);
    if (err == ESP_OK)
        record = next;
    return err;
}
static void path(char *out, size_t n, const char *id, const char *asset)
{
    snprintf(out, n, GIFT_DIR "/%s-%s.epdgz", id, asset);
}
bool gift_mode_active(void)
{
    load();
    return record.state == GIFT_READY || record.state == GIFT_PENDING;
}
bool gift_mode_hold(void)
{
    load();
    return record.state == GIFT_OPENED && gift_hold_active(time(NULL), record.hold_until);
}
int64_t gift_mode_hold_until(void)
{
    return gift_mode_hold() ? record.hold_until : 0;
}
bool gift_mode_display_allowed(const char *p)
{
    if (!gift_mode_active())
        return true;
    if (!p || publishing_task != xTaskGetCurrentTaskHandle())
        return false;
    char welcome[160], first[160];
    path(welcome, sizeof(welcome), record.id, "welcome");
    path(first, sizeof(first), record.id, "first");
    return !strcmp(p, welcome) || !strcmp(p, first);
}
bool gift_mode_navigation_blocked(void)
{
    uint32_t after = navigation_after_ms;
    uint32_t now = (uint32_t) (esp_timer_get_time() / 1000);
    return staging || publishing_task || (after && (int32_t) (now - after) < 0);
}
void gift_mode_add_metrics(cJSON *json)
{
    load();
    cJSON_AddNumberToObject(json, "gift_version", 1);
    if (!record.id[0] && !error_id[0])
        return;
    cJSON_AddStringToObject(json, "gift_id", error[0] && error_id[0] ? error_id : record.id);
    const char *s = record.state == GIFT_OPENED      ? "opened"
                    : record.state == GIFT_CANCELLED ? "cancelled"
                    : record.state == GIFT_READY     ? "ready"
                                                     : "failed";
    cJSON_AddStringToObject(json, "gift_state", error[0] ? "failed" : s);
    if (error[0])
        cJSON_AddStringToObject(json, "gift_error", error);
}
char *gift_mode_bound_metrics(char *plain)
{
    if (!plain || gift_metrics_header_fits(strlen(plain)))
        return plain;
    free(plain);
    cJSON *minimal = cJSON_CreateObject();
    if (!minimal)
        return NULL;
    gift_mode_add_metrics(minimal);
    cJSON_AddNumberToObject(minimal, "uptime_ms", esp_timer_get_time() / 1000);
    cJSON_AddBoolToObject(minimal, "metrics_trimmed", true);
    plain = cJSON_PrintUnformatted(minimal);
    cJSON_Delete(minimal);
    if (plain && !gift_metrics_header_fits(strlen(plain))) {
        free(plain);
        return NULL;
    }
    return plain;
}
static esp_err_t paint(const char *asset)
{
    char file[160];
    path(file, sizeof(file), record.id, asset);
    publishing_task = xTaskGetCurrentTaskHandle();
    esp_err_t err = display_manager_show_image(file);
    publishing_task = NULL;
    navigation_after_ms = (uint32_t) (esp_timer_get_time() / 1000) + 1000;
    emviary_navigation_discard_pending();
    return err;
}
esp_err_t gift_mode_restore(void)
{
    if (!gift_mode_active())
        return display_manager_restore_artwork();
    esp_err_t err = paint("welcome");
    if (err == ESP_OK && (record.overlay || record.state == GIFT_PENDING)) {
        gift_record_t next = record;
        next.overlay = 0;
        next.state = GIFT_READY;
        err = save(next);
    }
    return err;
}
esp_err_t gift_mode_note_diagnostics(void)
{
    if (!gift_mode_active() || record.overlay)
        return ESP_OK;
    gift_record_t next = record;
    next.overlay = 1;
    return save(next);
}
esp_err_t gift_mode_resume(void)
{
    load();
    if (gift_mode_active() && (record.overlay || record.state == GIFT_PENDING))
        return gift_mode_restore();
    return ESP_OK;
}
static esp_err_t mark_completed(const char *id);
esp_err_t gift_mode_reveal(void)
{
    if (!gift_mode_active())
        return ESP_ERR_INVALID_STATE;
    if (gift_mode_navigation_blocked())
        return ESP_OK;
    esp_err_t err = paint("first");
    if (err == ESP_OK) {
        gift_record_t next = record;
        next.state = GIFT_OPENED;
        next.overlay = 0;
        next.hold_until = time(NULL) + GIFT_HOLD_SECONDS;
        err = mark_completed(record.id);
        if (err == ESP_OK)
            err = save(next);
        if (err == ESP_OK) {
            config_manager_set_image_etag("");
            error[0] = 0;
        }
    }
    if (err != ESP_OK) {
        snprintf(error, sizeof(error), "First artwork refresh or durable opened state failed");
        paint("welcome");
    }
    return err;
}
static const char *str(cJSON *o, const char *key)
{
    cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    return cJSON_IsString(v) ? v->valuestring : NULL;
}
static bool completed(const char *id)
{
    char file[160];
    path(file, sizeof(file), id, "done");
    return access(file, F_OK) == 0;
}
static esp_err_t mark_completed(const char *id)
{
    char file[160];
    path(file, sizeof(file), id, "done");
    FILE *f = fopen(file, "wb");
    if (!f)
        return ESP_FAIL;
    bool ok = fputc('1', f) != EOF && fflush(f) == 0 && fsync(fileno(f)) == 0;
    if (fclose(f) != 0)
        ok = false;
    return ok ? ESP_OK : ESP_FAIL;
}
/* Bound durable image storage to the active package plus one staging package.
 * Completed-id receipts are tiny and survive cleanup to make old IDs one-shot. */
static void cleanup_assets(const char *staging_id)
{
    DIR *dir = opendir(GIFT_DIR);
    if (!dir)
        return;
    struct dirent *entry;
    while ((entry = readdir(dir))) {
        const char *n = entry->d_name;
        if (strlen(n) > 80 || strlen(n) < 39 || n[32] != '-' || strstr(n, "-done.epdgz"))
            continue;
        char id[33];
        memcpy(id, n, 32);
        id[32] = 0;
        if (!gift_id_valid(id) || !strcmp(id, record.id) || !strcmp(id, staging_id))
            continue;
        char file[256];
        snprintf(file, sizeof(file), GIFT_DIR "/%.80s", n);
        unlink(file);
    }
    closedir(dir);
}
static bool asset(cJSON *root, const char *key, const char *id, char file[160])
{
    cJSON *a = cJSON_GetObjectItemCaseSensitive(root, key),
          *n = cJSON_GetObjectItemCaseSensitive(a, "size");
    const char *url = str(a, "url"), *hash = str(a, "sha256");
    if (!url || strlen(url) > 768 || !https_origin_same(url, config_manager_get_image_url()) ||
        !gift_hash_valid(hash) || !cJSON_IsNumber(n) || n->valuedouble < 1 ||
        n->valuedouble > GIFT_ASSET_MAX || n->valuedouble != (size_t) n->valuedouble)
        return false;
    char temp[170];
    path(file, 160, id, key);
    snprintf(temp, sizeof(temp), "%s.tmp", file);
    if (utils_gift_download(url, temp, (size_t) n->valuedouble) != ESP_OK)
        return false;
    struct stat st;
    char digest[65];
    bool ok = stat(temp, &st) == 0 && st.st_size == (off_t) n->valuedouble &&
              recovery_image_sha256(temp, digest) && !strcmp(digest, hash) &&
              GUI_ValidateEPDGZ(temp) == 0;
    if (ok)
        ok = rename(temp, file) == 0;
    if (!ok)
        unlink(temp);
    return ok;
}
esp_err_t gift_mode_apply(const char *manifest)
{
    load();
    if (!manifest || !*manifest)
        return ESP_OK;
    if (strlen(manifest) > GIFT_MANIFEST_MAX)
        return ESP_ERR_INVALID_SIZE;
    cJSON *root = cJSON_Parse(manifest);
    cJSON *version = cJSON_GetObjectItemCaseSensitive(root, "version");
    const char *state = str(root, "state"), *id = str(root, "id");
    esp_err_t err = ESP_ERR_INVALID_ARG;
    if (!cJSON_IsNumber(version) || version->valuedouble != 1 || !state)
        goto done;
    if (!strcmp(state, "none")) {
        err = ESP_OK;
        goto done;
    }
    if (!gift_id_valid(id))
        goto done;
    snprintf(error_id, sizeof(error_id), "%s", id);
    if (!strcmp(state, "cancelled")) {
        if ((!strcmp(id, record.id) && gift_mode_active()) || !gift_mode_active()) {
            gift_record_t next = record;
            snprintf(next.id, sizeof(next.id), "%s", id);
            next.state = GIFT_CANCELLED;
            next.hold_until = 0;
            err = save(next);
            if (err == ESP_OK) {
                error[0] = 0;
                config_manager_set_image_etag("");
            }
        } else
            err = ESP_OK;
        goto done;
    }
    if (strcmp(state, "armed"))
        goto done;
    if (completed(id) && strcmp(id, record.id)) {
        err = ESP_OK;
        goto done;
    }
    if (!strcmp(id, record.id) && (record.state == GIFT_READY || record.state == GIFT_OPENED ||
                                   record.state == GIFT_CANCELLED)) {
        err = ESP_OK;
        goto done;
    }
    if (!storage_has_persistent_storage()) {
        err = ESP_ERR_INVALID_STATE;
        goto done;
    }
    mkdir(GIFT_DIR, 0755);
    staging = true;
    cleanup_assets(id);
    char welcome[160], first[160];
    /* Unique immutable filenames prevent staging from replacing the active gift. */
    if (!asset(root, "first", id, first) || !asset(root, "welcome", id, welcome)) {
        err = ESP_FAIL;
        goto done;
    }
    gift_record_t next = {.version = 1, .state = GIFT_PENDING};
    snprintf(next.id, sizeof(next.id), "%s", id);
    err = save(next);
    if (err != ESP_OK)
        goto done;
    err = paint("welcome");
    if (err == ESP_OK) {
        next.state = GIFT_READY;
        err = save(next);
    }
    if (err == ESP_OK)
        error[0] = 0;
done:
    staging = false;
    if (err != ESP_OK)
        snprintf(error, sizeof(error), "Gift package staging or checked welcome refresh failed");
    cJSON_Delete(root);
    return err;
}
static esp_err_t contact(bool apply)
{
    bool was_active = gift_mode_active();
    const char *image = config_manager_get_image_url();
    https_origin_t origin;
    if (!https_origin_parse(image, &origin))
        return ESP_ERR_INVALID_ARG;
    const char *end = strchr(image + 8, '/');
    size_t n = end ? (size_t) (end - image) : strlen(image);
    char url[1024];
    if (n + 10 >= sizeof(url))
        return ESP_ERR_INVALID_SIZE;
    memcpy(url, image, n);
    strcpy(url + n, "/v1/gift");
    if (!utils_image_operation_begin(pdMS_TO_TICKS(60000)))
        return ESP_ERR_TIMEOUT;
    mkdir(GIFT_DIR, 0755);
    esp_err_t err = utils_gift_download(url, GIFT_DIR "/manifest.tmp", GIFT_MANIFEST_MAX);
    if (err == ESP_OK) {
        FILE *f = fopen(GIFT_DIR "/manifest.tmp", "rb");
        char *body = calloc(1, GIFT_MANIFEST_MAX + 1);
        if (!f || !body)
            err = ESP_ERR_NO_MEM;
        else {
            size_t bytes = fread(body, 1, GIFT_MANIFEST_MAX, f);
            if (ferror(f) || !bytes)
                err = ESP_FAIL;
            else
                err = apply ? gift_mode_apply(body) : ESP_OK;
        }
        if (f)
            fclose(f);
        free(body);
        unlink(GIFT_DIR "/manifest.tmp");
    }
    if (apply && was_active && !gift_mode_active() && err == ESP_OK) {
        bool unchanged = false;
        err = fetch_and_display_image_from_url(config_manager_get_image_url(), &unchanged);
    }
    utils_image_operation_end();
    return err;
}
esp_err_t gift_mode_sync(void)
{
    return contact(true);
}
esp_err_t gift_mode_report(void)
{
    return contact(false);
}

void gift_mode_end_hold(void)
{
    load();
    if (record.state == GIFT_OPENED && record.hold_until) {
        gift_record_t next = record;
        next.hold_until = 0;
        save(next);
    }
}
#ifdef GIFT_HOST_TEST
void gift_mode_test_reset(void)
{
    memset(&record, 0, sizeof(record));
    loaded = false;
    error[0] = 0;
    error_id[0] = 0;
    navigation_after_ms = 0;
}
#endif
