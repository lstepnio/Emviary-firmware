#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#define GIFT_MANIFEST_MAX 3072
#define GIFT_ASSET_MAX (256u * 1024u)
#define GIFT_HOLD_SECONDS 1800
static inline bool gift_id_valid(const char *id)
{
    if (!id || strlen(id) != 32)
        return false;
    for (unsigned i = 0; i < 32; i++) {
        if (!((id[i] >= '0' && id[i] <= '9') || (id[i] >= 'a' && id[i] <= 'f')))
            return false;
    }
    return true;
}
static inline bool gift_hash_valid(const char *hash)
{
    if (!hash || strlen(hash) != 64)
        return false;
    for (unsigned i = 0; i < 64; i++)
        if (!((hash[i] >= '0' && hash[i] <= '9') || (hash[i] >= 'a' && hash[i] <= 'f')))
            return false;
    return true;
}
static inline bool gift_hold_active(int64_t now, int64_t until)
{
    return until > now && until - now <= GIFT_HOLD_SECONDS;
}

/* b64: prefix plus padded Base64 must fit the server's total header cap. */
static inline bool gift_metrics_header_fits(size_t plain_bytes)
{
    return plain_bytes <= 3 * ((2048u - 4u) / 4u);
}
