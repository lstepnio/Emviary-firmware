#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "offline_cache.h"
#include "storage.h"
#include "utils.h"
#include "zlib.h"
#define ROOT "/tmp/emviary-offline-cache-tests"
void offline_cache_test_reset(void);
time_t time(time_t *out)
{
    time_t fixed = 1791892800;
    if (out)
        *out = fixed;
    return fixed;
}
int offline_test_alloc_fail;
static unsigned displays, locks;
static bool display_fail, rename_fail;
static bool unsupported_access, metadata_stat_io;
static size_t used;
static char shown[160];
int rename(const char *from, const char *to)
{
    if (rename_fail && strstr(to, ".meta"))
        return -1;
    return renameat(AT_FDCWD, from, AT_FDCWD, to);
}
int access(const char *path, int mode)
{
    if (unsupported_access) {
        errno = ENOSYS;
        return -1;
    }
    return faccessat(AT_FDCWD, path, mode, 0);
}
int stat(const char *path, struct stat *st)
{
    if (metadata_stat_io && strstr(path, "/offline/") && strstr(path, ".meta")) {
        errno = EIO;
        return -1;
    }
    return fstatat(AT_FDCWD, path, st, 0);
}
storage_type_t storage_get_type(void)
{
    return STORAGE_TYPE_LITTLEFS;
}
int esp_littlefs_info(const char *label, size_t *total, size_t *out)
{
    (void) label;
    *total = 24u * 1024u * 1024u;
    *out = used;
    return 0;
}
bool utils_image_operation_begin(TickType_t ticks)
{
    (void) ticks;
    locks++;
    return true;
}
void utils_image_operation_end(void)
{
    assert(locks);
    locks--;
}
esp_err_t display_manager_show_offline(const char *path, const offline_patch_t *p)
{
    assert(p->length == 2);
    assert(p->pixels[0] == 0x11 && p->pixels[1] == 0x01);
    snprintf(shown, sizeof(shown), "%s", path);
    if (display_fail)
        return ESP_FAIL;
    displays++;
    return ESP_OK;
}
bool recovery_image_sha256(const char *path, char out[65])
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return false;
    int v = fgetc(f);
    fclose(f);
    if (v < 0)
        return false;
    for (int i = 0; i < 32; i++)
        sprintf(out + 2 * i, "%02x", v);
    return true;
}
static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static void b64(const unsigned char *in, size_t n, char *out)
{
    size_t p = 0;
    for (size_t i = 0; i < n; i += 3) {
        unsigned v = in[i] << 16;
        if (i + 1 < n)
            v |= in[i + 1] << 8;
        if (i + 2 < n)
            v |= in[i + 2];
        out[p++] = alphabet[v >> 18];
        out[p++] = alphabet[(v >> 12) & 63];
        out[p++] = i + 1 < n ? alphabet[(v >> 6) & 63] : '=';
        out[p++] = i + 2 < n ? alphabet[v & 63] : '=';
    }
    out[p] = 0;
}
int mbedtls_base64_decode(unsigned char *out, size_t cap, size_t *written, const unsigned char *in,
                          size_t n)
{
    if (n % 4)
        return -1;
    size_t p = 0;
    for (size_t i = 0; i < n; i += 4) {
        unsigned v = 0;
        for (unsigned j = 0; j < 4; j++) {
            const char *c = strchr(alphabet, in[i + j]);
            if (in[i + j] == '=') {
                if (i + 4 != n || j < 2)
                    return -1;
                v <<= 6;
            } else if (!c)
                return -1;
            else
                v = (v << 6) | (unsigned) (c - alphabet);
        }
        if (p >= cap)
            return -1;
        out[p++] = v >> 16;
        if (in[i + 2] != '=') {
            if (p >= cap)
                return -1;
            out[p++] = v >> 8;
        }
        if (in[i + 3] != '=') {
            if (p >= cap)
                return -1;
            out[p++] = v;
        }
    }
    *written = p;
    return 0;
}
static void header(unsigned id, const char *season, bool eligible, char out[4097])
{
    unsigned char tile[] = {0x11, 0x01}, compressed[100];
    uLongf len = sizeof(compressed);
    assert(compress(compressed, &len, tile, 2) == Z_OK);
    char z[200], sha[65], json[1000];
    b64(compressed, len, z);
    for (unsigned i = 0; i < 32; i++)
        sprintf(sha + 2 * i, "%02x", id);
    snprintf(json, sizeof(json),
             "{\"v\":1,\"id\":\"%u\",\"sha\":\"%s\",\"eligible\":%s,\"season\":\"%s\",\"patch\":{"
             "\"x\":200,\"y\":10,\"w\":4,\"h\":1,\"z\":\"%s\"}}",
             id, sha, eligible ? "true" : "false", season, z);
    b64((unsigned char *) json, strlen(json), out);
}
static void source(unsigned id)
{
    FILE *f = fopen(ROOT "/source.epdgz", "wb");
    assert(f);
    for (unsigned n = 0; n < 128; n++)
        fputc(id, f);
    assert(fclose(f) == 0);
}
static unsigned metric(const char *key)
{
    cJSON *j = cJSON_CreateObject();
    offline_cache_add_metrics(j);
    cJSON *value = cJSON_GetObjectItem(j, key);
    unsigned n = cJSON_IsBool(value) ? cJSON_IsTrue(value) : value->valueint;
    cJSON_Delete(j);
    return n;
}
static void reset(void)
{
    assert(system("rm -rf " ROOT) == 0);
    assert(mkdir(ROOT, 0755) == 0);
    offline_cache_test_reset();
    used = 0;
    displays = locks = 0;
    display_fail = rename_fail = unsupported_access = metadata_stat_io = false;
}
static void admit(unsigned id, const char *season)
{
    char h[4097];
    source(id);
    header(id, season, true, h);
    assert(offline_cache_admit(ROOT "/source.epdgz", h) == ESP_OK);
}
int main(void)
{
    setenv("TZ", "UTC0", 1);
    tzset();
    char h[4097];
    reset();
    assert(!offline_cache_available());
    assert(offline_cache_rotate(false) == ESP_ERR_NOT_FOUND);
    source(1);
    header(1, "evergreen", false, h);
    assert(offline_cache_admit(ROOT "/source.epdgz", h) != ESP_OK);
    assert(!offline_cache_available());
    assert(offline_cache_admit(ROOT "/source.epdgz", "notbase64") != ESP_OK);
    header(2, "evergreen", true, h);
    assert(offline_cache_admit(ROOT "/source.epdgz", h) != ESP_OK);  // body digest mismatch
    used = 23u * 1024u * 1024u;
    header(1, "evergreen", true, h);
    assert(offline_cache_admit(ROOT "/source.epdgz", h) == ESP_ERR_NO_MEM);
    used = 0;
    admit(1, "evergreen");
    assert(metric("offline_cache_count") == 1);
    assert(offline_cache_rotate(false) == ESP_OK);
    assert(displays == 1);
    assert(metric("offline_mode"));
    assert(offline_cache_rotate(false) == ESP_OK);
    assert(displays == 1);  // single neutral hold
    unsigned before_alloc = displays;
    offline_test_alloc_fail = -1;
    assert(offline_cache_rotate(false) != ESP_OK);
    assert(displays == before_alloc && metric("offline_cache_count") == 1);
    offline_test_alloc_fail = 0;
    admit(2, "evergreen");
    admit(3, "evergreen");
    assert(offline_cache_rotate(false) == ESP_OK);
    assert(metric("offline_image_id") == 1);
    assert(offline_cache_rotate(true) == ESP_OK);
    assert(metric("offline_image_id") == 3);
    offline_cache_test_reset();
    assert(metric("offline_mode"));
    assert(metric("offline_cache_count") == 3);  // durable restart
    admit(4, "evergreen");
    assert(metric("offline_cache_count") == 3);
    assert(access(ROOT "/offline/0000000001.meta", F_OK) != 0);  // FIFO capacity eviction
    // Corrupt oldest candidate, decoder not called for it.
    FILE *f = fopen(ROOT "/offline/0000000002.epdgz", "wb");
    assert(f);
    fputc(99, f);
    fclose(f);
    assert(offline_cache_rotate(false) == ESP_OK);
    assert(metric("offline_image_id") == 3);
    unsigned old = metric("offline_image_id");
    display_fail = true;
    assert(offline_cache_rotate(false) ==
           ESP_OK);  // All alternatives failed, keep the existing neutral picture.
    assert(metric("offline_image_id") == old);
    display_fail = false;
    // Atomic publication failure preserves all known-good pairs.
    rename_fail = true;
    source(5);
    header(5, "evergreen", true, h);
    assert(offline_cache_admit(ROOT "/source.epdgz", h) != ESP_OK);
    rename_fail = false;
    assert(metric("offline_cache_count") == 3);
    assert(access(ROOT "/offline/0000000005.epdgz", F_OK) != 0);
    // Retry repairs a corrupt stored copy rather than treating it as a good duplicate.
    source(2);
    header(2, "evergreen", true, h);
    assert(offline_cache_admit(ROOT "/source.epdgz", h) == ESP_OK);
    assert(offline_cache_rotate(true) == ESP_OK);
    assert(metric("offline_image_id") != 0);
    // Orphan/staging leftovers from power loss are removed only on admission.
    f = fopen(ROOT "/offline/0000000999.epdgz", "wb");
    assert(f);
    fputc(99, f);
    fclose(f);
    f = fopen(ROOT "/offline/stage.epdgz", "wb");
    assert(f);
    fputc(88, f);
    fclose(f);
    admit(6, "evergreen");
    assert(access(ROOT "/offline/0000000999.epdgz", F_OK) != 0);
    assert(access(ROOT "/offline/stage.epdgz", F_OK) != 0);
    // Metadata open failure preserves the current cache and known-good entries.
    assert(mkdir(ROOT "/offline/stage.meta", 0755) == 0);
    source(7);
    header(7, "evergreen", true, h);
    assert(offline_cache_admit(ROOT "/source.epdgz", h) != ESP_OK);
    rmdir(ROOT "/offline/stage.meta");
    reset();
    admit(1, "autumn");
    admit(2, "winter");  // Current winter is unsuitable in October, autumn selected.
    assert(offline_cache_rotate(false) == ESP_OK);
    assert(metric("offline_image_id") == 1);
    offline_cache_mark_cloud_success();
    source(1);
    header(1, "autumn", true, h);
    assert(offline_cache_admit(ROOT "/source.epdgz", h) == ESP_OK);
    assert(offline_cache_rotate(false) == ESP_OK);
    assert(metric("offline_image_id") == 1);  // unsuitable alternative: neutralize current
    reset();
    admit(1, "evergreen");
    admit(2, "evergreen");
    admit(3, "evergreen");
    // Simulate power loss after publishing a fourth pair but before old-entry eviction.
    header(4, "evergreen", true, h);
    f = fopen(ROOT "/offline/0000000004.meta", "wb");
    assert(f);
    assert(fwrite(h, 1, strlen(h), f) == strlen(h));
    fclose(f);
    f = fopen(ROOT "/offline/0000000004.epdgz", "wb");
    assert(f);
    for (unsigned i = 0; i < 128; i++)
        fputc(4, f);
    fclose(f);
    offline_cache_test_reset();
    assert(metric("offline_cache_count") == 3);
    admit(5, "evergreen");
    assert(metric("offline_cache_count") == 3);
    assert(access(ROOT "/offline/0000000001.meta", F_OK) != 0);
    assert(access(ROOT "/offline/0000000002.meta", F_OK) != 0);
    // ESP LittleFS supplies stat(), but not access(). ENOSYS is not absence.
    reset();
    unsupported_access = true;
    admit(1, "evergreen");
    admit(2, "evergreen");
    admit(3, "evergreen");
    offline_cache_test_reset();
    assert(metric("offline_cache_count") == 3);
    assert(offline_cache_rotate(false) == ESP_OK);
    assert(metric("offline_image_id") == 1);
    assert(offline_cache_rotate(false) == ESP_OK);
    assert(metric("offline_image_id") == 2);
    assert(offline_cache_rotate(false) == ESP_OK);
    assert(metric("offline_image_id") == 3);
    unsupported_access = false;
    // A transient metadata IO error must preserve existing pairs, not delete them.
    reset();
    admit(1, "evergreen");
    metadata_stat_io = true;
    admit(2, "evergreen");
    metadata_stat_io = false;
    offline_cache_test_reset();
    assert(metric("offline_cache_count") == 2);
    assert(!locks);
    puts(
        "Offline cache validation, reserve, FIFO, cursor, corruption, publication and neutral hold "
        "passed");
    reset();
    rmdir(ROOT);
    return 0;
}
