#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#define OTA_RECOVERY_FORMAT 1
#define OTA_RECOVERY_ACTIVE 1
#define OTA_RECOVERY_HELD 2
typedef struct {
    uint8_t format;
    uint8_t phase;
    char source[32];
    char digest[65];
} ota_recovery_record_t;
static inline bool ota_recovery_record_valid(const ota_recovery_record_t *record)
{
    return record && record->format == OTA_RECOVERY_FORMAT &&
        (record->phase == OTA_RECOVERY_ACTIVE || record->phase == OTA_RECOVERY_HELD) &&
        memchr(record->source, 0, sizeof(record->source)) && record->source[0] &&
        record->digest[64] == 0 && strspn(record->digest, "0123456789abcdef") == 64;
}
static inline bool ota_recovery_hold_after_reset(ota_recovery_record_t *record,
                                                const char *source, bool crashed)
{
    if (!crashed || !ota_recovery_record_valid(record) ||
        record->phase != OTA_RECOVERY_ACTIVE || strcmp(record->source, source)) return false;
    record->phase = OTA_RECOVERY_HELD;
    return true;
}
static inline bool ota_recovery_should_hold(const ota_recovery_record_t *record,
                                            const char *source, const char *digest,
                                            bool automatic)
{
    return automatic && ota_recovery_record_valid(record) && record->phase == OTA_RECOVERY_HELD &&
        !strcmp(record->source, source) && !strcmp(record->digest, digest);
}
