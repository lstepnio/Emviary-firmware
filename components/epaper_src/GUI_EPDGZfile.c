// Packed panel input must be validated completely before touching Paint.
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <zlib.h>

#include "GUI_Paint.h"

static const char *TAG = "GUI_EPDGZfile";

static voidpf epdgz_alloc(voidpf opaque, uInt items, uInt size)
{
    (void) opaque;
    if (size && items > SIZE_MAX / size) return NULL;
    return heap_caps_calloc(items, size, MALLOC_CAP_SPIRAM);
}

static void epdgz_free(voidpf opaque, voidpf address)
{
    (void) opaque;
    heap_caps_free(address);
}

int GUI_ReadEPDGZ(const char *path)
{
    if (!path || !Paint.Width || !Paint.Height) return 1;
    const size_t row_bytes = ((size_t) Paint.Width + 1) / 2;
    const size_t expected = row_bytes * Paint.Height;
    // Gzip incompressible overhead is tiny; this also bounds header padding.
    const size_t max_compressed = expected + expected / 1000 + 65536;
    if (expected > UINT_MAX || max_compressed > UINT_MAX) return 1;
    FILE *fp = fopen(path, "rb");
    if (!fp) return 1;
    uint8_t *compressed = NULL, *decoded = NULL;
    int result = 1;
    z_stream stream = {0};
    bool initialized = false;
    if (fseek(fp, 0, SEEK_END) != 0) goto done;
    long length = ftell(fp);
    if (length <= 0 || (size_t) length > max_compressed || fseek(fp, 0, SEEK_SET) != 0)
        goto done;
    compressed = heap_caps_malloc((size_t) length, MALLOC_CAP_SPIRAM);
    // One extra byte distinguishes an exact-size frame from an overflowing one.
    decoded = heap_caps_malloc(expected + 1, MALLOC_CAP_SPIRAM);
    if (!compressed || !decoded) goto done;
    if (fread(compressed, 1, (size_t) length, fp) != (size_t) length || ferror(fp)) goto done;
    int close_result = fclose(fp);
    fp = NULL;
    if (close_result != 0) goto done;
    stream.zalloc = epdgz_alloc;
    stream.zfree = epdgz_free;
    stream.next_in = compressed;
    stream.avail_in = (uInt) length;
    stream.next_out = decoded;
    stream.avail_out = (uInt) expected + 1;
    if (inflateInit2(&stream, 16 + MAX_WBITS) != Z_OK) goto done;
    initialized = true;
    int status = inflate(&stream, Z_FINISH);
    if (status != Z_STREAM_END || stream.total_out != expected || stream.avail_in != 0)
        goto done;
    // Spectra 6 uses sparse ink codes. Reject invalid codes before any pixels
    // are written. Gray16 accepts every nibble; scale 7 accepts 0..6.
    for (size_t y = 0; y < Paint.Height; y++) {
        for (size_t x = 0; x < Paint.Width; x++) {
            uint8_t b = decoded[y * row_bytes + x / 2];
            uint8_t color = (x & 1) ? b & 15 : b >> 4;
            if ((Paint.Scale == 6 && !(color <= 3 || color == 5 || color == 6)) ||
                (Paint.Scale == 7 && color > 6)) goto done;
        }
    }
    for (size_t y = 0; y < Paint.Height; y++) {
        for (size_t x = 0; x < Paint.Width; x++) {
            uint8_t b = decoded[y * row_bytes + x / 2];
            Paint_SetPixel(x, y, (x & 1) ? b & 15 : b >> 4);
        }
    }
    result = 0;
done:
    if (initialized) inflateEnd(&stream);
    if (fp && fclose(fp) != 0) result = 1;
    heap_caps_free(compressed);
    heap_caps_free(decoded);
    if (result) ESP_LOGE(TAG, "Invalid, incomplete or oversized EPDGZ input");
    return result;
}
