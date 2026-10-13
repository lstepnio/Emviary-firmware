#include "offline_cache.h"

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "config.h"
#include "display_manager.h"
#include "esp_heap_caps.h"
#include "esp_littlefs.h"
#include "mbedtls/base64.h"
#include "offline_policy.h"
#include "recovery_image_digest.h"
#include "storage.h"
#include "utils.h"
#include "zlib.h"
#define CACHE_DIR FS_MOUNT_POINT "/offline"
#define CURSOR_PATH CACHE_DIR "/cursor"
typedef struct {
    uint32_t seq;
    size_t bytes;
} entry_t;
typedef struct {
    uint32_t magic, seq, rotations, offline;
    char id[65];
    uint32_t crc;
} cursor_t;
typedef struct {
    char id[65], sha[65];
    unsigned season;
    offline_patch_t patch;
} bundle_t;
static entry_t entries[OFFLINE_CACHE_MAX_ENTRIES];
static size_t count, bytes;
static uint32_t highest_sequence;
static cursor_t cursor;
static bool loaded, last_rotation;
static uint32_t cursor_crc(const cursor_t *c)
{
    return (uint32_t) crc32(0, (const Bytef *) c, offsetof(cursor_t, crc));
}
static void name(char *out, size_t cap, uint32_t seq, const char *ext)
{
    snprintf(out, cap, CACHE_DIR "/%010lu.%s", (unsigned long) seq, ext);
}
static int compare(const void *a, const void *b)
{
    const entry_t *x = a, *y = b;
    return x->seq < y->seq ? -1 : x->seq > y->seq;
}
static void load(void)
{
    if (loaded || storage_get_type() != STORAGE_TYPE_LITTLEFS)
        return;
    loaded = true;
    memset(&cursor, 0, sizeof(cursor));
    FILE *f = fopen(CURSOR_PATH, "rb");
    if (f) {
        bool ok = fread(&cursor, 1, sizeof(cursor), f) == sizeof(cursor) && fgetc(f) == EOF;
        fclose(f);
        if (!ok || cursor.magic != 0x454f4331 || cursor.crc != cursor_crc(&cursor))
            memset(&cursor, 0, sizeof(cursor));
    }
    DIR *d = opendir(CACHE_DIR);
    if (!d)
        return;
    struct dirent *de;
    unsigned scanned = 0;
    while ((de = readdir(d)) && scanned++ < 1024) {
        unsigned long seq = 0;
        char extension[16];
        if (sscanf(de->d_name, "%lu.%15s", &seq, extension) != 2 || strcmp(extension, "meta") ||
            seq == 0 || seq > UINT32_MAX)
            continue;
        char p[160];
        struct stat st, meta;
        name(p, sizeof(p), seq, "epdgz");
        if (stat(p, &st) != 0 || st.st_size <= 0 || st.st_size > 256 * 1024)
            continue;
        name(p, sizeof(p), seq, "meta");
        if (stat(p, &meta) != 0 || meta.st_size <= 0 || meta.st_size > OFFLINE_HEADER_MAX)
            continue;
        if (seq > highest_sequence)
            highest_sequence = seq;
        bool duplicate = false;
        for (size_t i = 0; i < count; i++)
            if (entries[i].seq == seq)
                duplicate = true;
        if (duplicate)
            continue;
        if (count == OFFLINE_CACHE_MAX_ENTRIES) {
            qsort(entries, count, sizeof(entries[0]), compare);
            if (seq <= entries[0].seq)
                continue;
            bytes -= entries[0].bytes;
            count--;
            memmove(entries, entries + 1, count * sizeof(entries[0]));
        }
        entries[count++] = (entry_t){seq, (size_t) st.st_size + (size_t) meta.st_size};
        bytes += st.st_size + meta.st_size;
        qsort(entries, count, sizeof(entries[0]), compare);
        while (count > 1 && bytes > OFFLINE_CACHE_BUDGET) {
            bytes -= entries[0].bytes;
            count--;
            memmove(entries, entries + 1, count * sizeof(entries[0]));
        }
    }
    closedir(d);
    qsort(entries, count, sizeof(entries[0]), compare);
}
static bool save_cursor(void)
{
    cursor.magic = 0x454f4331;
    cursor.crc = cursor_crc(&cursor);
    FILE *f = fopen(CURSOR_PATH ".tmp", "wb");
    if (!f)
        return false;
    bool ok = fwrite(&cursor, 1, sizeof(cursor), f) == sizeof(cursor);
    if (fclose(f) != 0)
        ok = false;
    if (ok)
        ok = rename(CURSOR_PATH ".tmp", CURSOR_PATH) == 0;
    if (!ok)
        unlink(CURSOR_PATH ".tmp");
    return ok;
}
static unsigned season_code(const char *s)
{
    if (!s)
        return UINT_MAX;
    if (!strcmp(s, "evergreen"))
        return 0;
    if (!strcmp(s, "spring"))
        return 1;
    if (!strcmp(s, "summer"))
        return 2;
    if (!strcmp(s, "autumn"))
        return 3;
    if (!strcmp(s, "winter"))
        return 4;
    return UINT_MAX;
}
static bool integer(cJSON *o, const char *key, unsigned *out)
{
    cJSON *n = cJSON_GetObjectItemCaseSensitive(o, key);
    if (!cJSON_IsNumber(n) || n->valuedouble < 0 || n->valuedouble > 65535 ||
        n->valuedouble != (unsigned) n->valuedouble)
        return false;
    *out = (unsigned) n->valuedouble;
    return true;
}
static void release(bundle_t *b)
{
    free(b->patch.pixels);
    b->patch.pixels = NULL;
}
static bool parse(const char *header, bundle_t *b)
{
    memset(b, 0, sizeof(*b));
    if (!header || !*header || strlen(header) > OFFLINE_HEADER_MAX)
        return false;
    if (!strncmp(header, "b64:", 4))
        header += 4;
    unsigned char *decoded = heap_caps_malloc(3073, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!decoded)
        return false;
    size_t len = 0;
    if (mbedtls_base64_decode(decoded, 3072, &len, (const unsigned char *) header,
                              strlen(header)) != 0 ||
        !len) {
        free(decoded);
        return false;
    }
    decoded[len] = 0;
    if (memchr(decoded, 0, len)) {
        free(decoded);
        return false;
    }
    // Bound JSON nesting before cJSON's recursive parser touches task stack.
    unsigned depth = 0;
    bool quoted = false, escaped = false, shallow = true;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = decoded[i];
        if (quoted) {
            if (escaped)
                escaped = false;
            else if (c == '\\')
                escaped = true;
            else if (c == '"')
                quoted = false;
            continue;
        }
        if (c == '"')
            quoted = true;
        else if (c == '{' || c == '[') {
            if (++depth > 4) {
                shallow = false;
                break;
            }
        } else if (c == '}' || c == ']') {
            if (!depth) {
                shallow = false;
                break;
            }
            depth--;
        }
    }
    if (!shallow || depth || quoted) {
        free(decoded);
        return false;
    }
    cJSON *j = cJSON_ParseWithOpts((char *) decoded, NULL, true);
    free(decoded);
    if (!j)
        return false;
    cJSON *v = cJSON_GetObjectItemCaseSensitive(j, "v"),
          *id = cJSON_GetObjectItemCaseSensitive(j, "id"),
          *sha = cJSON_GetObjectItemCaseSensitive(j, "sha"),
          *eligible = cJSON_GetObjectItemCaseSensitive(j, "eligible"),
          *season = cJSON_GetObjectItemCaseSensitive(j, "season"),
          *p = cJSON_GetObjectItemCaseSensitive(j, "patch");
    bool ok = cJSON_IsNumber(v) && v->valuedouble == 1 && cJSON_IsTrue(eligible) &&
              cJSON_IsString(id) && strlen(id->valuestring) > 0 && strlen(id->valuestring) <= 64 &&
              cJSON_IsString(sha) && strlen(sha->valuestring) == 64 && cJSON_IsString(season) &&
              cJSON_IsObject(p);
    if (ok) {
        for (const char *c = id->valuestring; *c; c++)
            if (*c < '0' || *c > '9')
                ok = false;
        if (strlen(id->valuestring) > 10 || strtoul(id->valuestring, NULL, 10) == 0 ||
            strtoul(id->valuestring, NULL, 10) > INT_MAX)
            ok = false;
    }
    if (ok) {
        for (unsigned i = 0; i < 64; i++)
            if (!((sha->valuestring[i] >= '0' && sha->valuestring[i] <= '9') ||
                  (sha->valuestring[i] >= 'a' && sha->valuestring[i] <= 'f')))
                ok = false;
    }
    cJSON *z = cJSON_GetObjectItemCaseSensitive(p, "z");
    if (ok) {
        b->season = season_code(season->valuestring);
        ok = b->season != UINT_MAX && integer(p, "x", &b->patch.x) &&
             integer(p, "y", &b->patch.y) && integer(p, "w", &b->patch.w) &&
             integer(p, "h", &b->patch.h) && cJSON_IsString(z) && strlen(z->valuestring) <= 2732;
    }
    if (ok) {
        b->patch.length = ((size_t) b->patch.w / 2) * b->patch.h;
        ok = offline_patch_bounds(b->patch.x, b->patch.y, b->patch.w, b->patch.h, 800, 480,
                                  b->patch.length);
    }
    unsigned char *compressed =
        ok ? heap_caps_malloc(2048, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) : NULL;
    if (!compressed)
        ok = false;
    size_t clen = 0;
    if (ok)
        ok = mbedtls_base64_decode(compressed, 2048, &clen, (unsigned char *) z->valuestring,
                                   strlen(z->valuestring)) == 0 &&
             clen > 0;
    if (ok) {
        b->patch.pixels = heap_caps_malloc(b->patch.length, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        ok = b->patch.pixels != NULL;
    }
    if (ok) {
        z_stream stream = {0};
        stream.next_in = compressed;
        stream.avail_in = clen;
        stream.next_out = b->patch.pixels;
        stream.avail_out = b->patch.length;
        bool init = inflateInit(&stream) == Z_OK;
        ok = init && inflate(&stream, Z_FINISH) == Z_STREAM_END &&
             stream.total_out == b->patch.length && stream.avail_in == 0;
        if (init)
            inflateEnd(&stream);
    }
    /* Spectra6 valid nibble codes: black0,white1,yellow2,red3,blue5,green6. */
    if (ok)
        for (size_t i = 0; i < b->patch.length; i++) {
            unsigned a = b->patch.pixels[i] >> 4, c = b->patch.pixels[i] & 15;
            if (a > 6 || a == 4 || c > 6 || c == 4) {
                ok = false;
                break;
            }
        }
    if (ok) {
        snprintf(b->id, sizeof(b->id), "%s", id->valuestring);
        snprintf(b->sha, sizeof(b->sha), "%s", sha->valuestring);
    }
    free(compressed);
    cJSON_Delete(j);
    if (!ok)
        release(b);
    return ok;
}
static bool read_bundle(uint32_t seq, bundle_t *b)
{
    char p[160];
    char *header = heap_caps_malloc(OFFLINE_HEADER_MAX + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!header)
        return false;
    name(p, sizeof(p), seq, "meta");
    FILE *f = fopen(p, "rb");
    if (!f) {
        free(header);
        return false;
    }
    size_t n = fread(header, 1, OFFLINE_HEADER_MAX, f);
    bool ok = !ferror(f) && fgetc(f) == EOF;
    fclose(f);
    header[n] = 0;
    ok = ok && parse(header, b);
    free(header);
    return ok;
}
static bool copy_file(const char *src, const char *dst)
{
    FILE *in = fopen(src, "rb"), *out = in ? fopen(dst, "wb") : NULL;
    if (!out) {
        if (in)
            fclose(in);
        return false;
    }
    unsigned char chunk[2048];
    size_t n;
    bool ok = true;
    while ((n = fread(chunk, 1, sizeof(chunk), in)))
        if (fwrite(chunk, 1, n, out) != n) {
            ok = false;
            break;
        }
    ok = ok && !ferror(in);
    fclose(in);
    if (fclose(out) != 0)
        ok = false;
    return ok;
}
static void cleanup_incomplete(void)
{
    unlink(CACHE_DIR "/stage.epdgz");
    unlink(CACHE_DIR "/stage.meta");
    DIR *d = opendir(CACHE_DIR);
    if (!d)
        return;
    struct dirent *de;
    unsigned scanned = 0;
    while ((de = readdir(d)) && scanned++ < 1024) {
        unsigned long seq;
        char extension[16];
        if (sscanf(de->d_name, "%lu.%15s", &seq, extension) != 2 || strcmp(extension, "epdgz") ||
            !seq || seq > UINT32_MAX)
            continue;
        char meta[160], image[160];
        name(meta, sizeof(meta), seq, "meta");
        bool retained = false;
        for (size_t i = 0; i < count; i++)
            if (entries[i].seq == seq)
                retained = true;
        struct stat metadata;
        int metadata_result = stat(meta, &metadata);
        if (metadata_result != 0 && errno != ENOENT)
            continue;
        // access() is unsupported by ESP LittleFS. Only a known absent
        // metadata file is an orphan; IO/unsupported errors preserve data.
        if (metadata_result != 0 || !retained) {
            unlink(meta);
            name(image, sizeof(image), seq, "epdgz");
            unlink(image);
        }
    }
    closedir(d);
}
esp_err_t offline_cache_admit(const char *path, const char *header)
{
    load();
    if (!loaded)
        return ESP_ERR_NOT_SUPPORTED;
    bundle_t b;
    if (!parse(header, &b))
        return ESP_ERR_INVALID_ARG;
    char sha[65];
    struct stat st;
    bool verified = stat(path, &st) == 0 && st.st_size > 0 && st.st_size <= 256 * 1024 &&
                    recovery_image_sha256(path, sha) && !strcmp(sha, b.sha);
    if (!verified) {
        release(&b);
        return ESP_FAIL;
    }
    /* Same digest or ID is a repeated visit, not new FIFO content. */
    for (size_t i = 0; i < count; i++) {
        bundle_t old;
        if (!read_bundle(entries[i].seq, &old))
            continue;
        bool same = !strcmp(old.sha, b.sha) || !strcmp(old.id, b.id);
        if (same) {
            char cached[160], digest[65];
            name(cached, sizeof(cached), entries[i].seq, "epdgz");
            same = recovery_image_sha256(cached, digest) && !strcmp(digest, old.sha);
        }
        release(&old);
        if (same) {
            cursor.seq = entries[i].seq;
            snprintf(cursor.id, sizeof(cursor.id), "%s", b.id);
            cursor.offline = 0;
            cursor.rotations = 0;
            save_cursor();
            release(&b);
            return ESP_OK;
        }
    }
    mkdir(CACHE_DIR, 0755);
    cleanup_incomplete();
    size_t total = 0, used = 0, added = st.st_size + strlen(header);
    if (esp_littlefs_info(LITTLEFS_PARTITION_LABEL, &total, &used) != ESP_OK ||
        !offline_can_stage(total, used, added)) {
        release(&b);
        return ESP_ERR_NO_MEM;
    }
    uint32_t seq = highest_sequence + 1;
    if (!seq) {
        release(&b);
        return ESP_ERR_INVALID_STATE;
    }
    char image[160], meta[160];
    name(image, sizeof(image), seq, "epdgz");
    name(meta, sizeof(meta), seq, "meta");
    bool ok = copy_file(path, CACHE_DIR "/stage.epdgz");
    FILE *f = ok ? fopen(CACHE_DIR "/stage.meta", "wb") : NULL;
    if (f) {
        ok = fwrite(header, 1, strlen(header), f) == strlen(header);
        if (fclose(f) != 0)
            ok = false;
    } else
        ok = false;
    if (ok)
        ok = rename(CACHE_DIR "/stage.epdgz", image) == 0;
    if (ok)
        ok = rename(CACHE_DIR "/stage.meta", meta) == 0;
    if (!ok) {
        unlink(CACHE_DIR "/stage.epdgz");
        unlink(CACHE_DIR "/stage.meta");
        unlink(image);
        release(&b);
        return ESP_FAIL;
    }
    /* Publish first. Eviction can never remove the only known-good fallback. */
    while (count && (bytes + added > OFFLINE_CACHE_BUDGET || count >= OFFLINE_CACHE_MAX_ENTRIES)) {
        char p[160];
        name(p, sizeof(p), entries[0].seq, "meta");
        unlink(p);
        name(p, sizeof(p), entries[0].seq, "epdgz");
        unlink(p);
        bytes -= entries[0].bytes;
        count--;
        memmove(entries, entries + 1, count * sizeof(entries[0]));
    }
    highest_sequence = seq;
    entries[count++] = (entry_t){seq, added};
    bytes += added;
    cursor.seq = seq;
    cursor.offline = 0;
    cursor.rotations = 0;
    snprintf(cursor.id, sizeof(cursor.id), "%s", b.id);
    save_cursor();
    release(&b);
    return ESP_OK;
}
esp_err_t offline_cache_rotate(bool previous)
{
    last_rotation = false;
    if (!utils_image_operation_begin(pdMS_TO_TICKS(60000)))
        return ESP_ERR_TIMEOUT;
    load();
    if (!count) {
        utils_image_operation_end();
        return ESP_ERR_NOT_FOUND;
    }
    esp_err_t result = ESP_ERR_NOT_FOUND;
    unsigned current = previous ? 0 : count ? count - 1 : 0;
    for (unsigned i = 0; i < count; i++)
        if (entries[i].seq == cursor.seq) {
            current = i;
            break;
        }
    time_t now = time(NULL);
    struct tm local;
    unsigned season =
        now >= 1577836800 && localtime_r(&now, &local) ? offline_month_season(local.tm_mon + 1) : 0;
    for (unsigned n = 0; n < count + 1; n++) {
        unsigned index = offline_next_index(current, count, previous);
        current = index;
        bool hold_current = n == count;
        if (hold_current) {
            bool found = false;
            for (unsigned i = 0; i < count; i++)
                if (entries[i].seq == cursor.seq) {
                    index = i;
                    found = true;
                    break;
                }
            if (!found)
                break;
        }
        if (entries[index].seq == cursor.seq && count > 1 && !hold_current)
            continue;
        bundle_t b;
        if (!read_bundle(entries[index].seq, &b))
            continue;
        if (!offline_season_allowed(b.season, season) && !hold_current) {
            release(&b);
            continue;
        }
        if (entries[index].seq == cursor.seq && cursor.offline) {
            release(&b);
            result = ESP_OK;
            last_rotation = true;
            break;
        }
        char p[160], sha[65];
        name(p, sizeof(p), entries[index].seq, "epdgz");
        bool valid = recovery_image_sha256(p, sha) && !strcmp(sha, b.sha);
        if (valid)
            result = display_manager_show_offline(p, &b.patch);
        if (valid && result == ESP_OK) {
            cursor.seq = entries[index].seq;
            cursor.offline = 1;
            cursor.rotations++;
            snprintf(cursor.id, sizeof(cursor.id), "%s", b.id);
            save_cursor();
            last_rotation = true;
            release(&b);
            break;
        }
        release(&b);
    }
    utils_image_operation_end();
    return result;
}
bool offline_cache_available(void)
{
    load();
    return count > 0;
}
bool offline_cache_is_offline(void)
{
    load();
    return cursor.offline != 0;
}
bool offline_cache_last_rotation_ok(void)
{
    return last_rotation;
}
void offline_cache_mark_cloud_success(void)
{
    load();
    if (cursor.offline) {
        cursor.offline = 0;
        cursor.rotations = 0;
        save_cursor();
    }
}
void offline_cache_add_metrics(cJSON *j)
{
    load();
    cJSON_AddBoolToObject(j, "offline_mode", cursor.offline != 0);
    if (*cursor.id)
        cJSON_AddNumberToObject(j, "offline_image_id", strtol(cursor.id, NULL, 10));
    cJSON_AddNumberToObject(j, "offline_cache_count", count);
    cJSON_AddNumberToObject(j, "offline_cache_bytes", bytes);
    cJSON_AddNumberToObject(j, "offline_rotations", cursor.rotations);
}
void offline_cache_status(char *out, size_t cap)
{
    load();
    snprintf(out, cap, cursor.offline ? "Offline art (%u cached)" : "%u cached compositions",
             (unsigned) count);
}

#ifdef OFFLINE_CACHE_HOST_TEST
void offline_cache_test_reset(void)
{
    loaded = false;
    count = bytes = 0;
    highest_sequence = 0;
    last_rotation = false;
    memset(&cursor, 0, sizeof(cursor));
}
#endif
