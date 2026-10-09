#ifndef DOWNLOAD_POLICY_H
#define DOWNLOAD_POLICY_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define IMAGE_DOWNLOAD_MAX_BYTES (5u * 1024u * 1024u)
#define IMAGE_DOWNLOAD_MAX_US INT64_C(60000000)
#define THUMBNAIL_DOWNLOAD_MAX_BYTES (256u * 1024u)
#define THUMBNAIL_DOWNLOAD_MAX_US INT64_C(30000000)

typedef struct { size_t max_bytes; int64_t deadline_us; } download_policy_t;
static inline bool download_policy_within_deadline(const download_policy_t *p, int64_t now_us)
{
    return now_us < p->deadline_us;
}
static inline bool download_policy_accept_bytes(const download_policy_t *p, size_t received,
                                                size_t incoming)
{
    return received <= p->max_bytes && incoming <= p->max_bytes - received;
}
static inline bool download_policy_complete(const download_policy_t *p, size_t received,
                                            int64_t declared, bool transport_complete)
{
    return transport_complete && received <= p->max_bytes &&
           (declared < 0 || (uint64_t) declared == received);
}
// Count only bytes actually persisted; the caller also checks buffered close.
static inline bool download_policy_write(const download_policy_t *p, FILE *file,
                                         size_t *received, const void *data, size_t size,
                                         int64_t now_us)
{
    if (!file || !received || !download_policy_within_deadline(p, now_us) ||
        !download_policy_accept_bytes(p, *received, size)) return false;
    if (fwrite(data, 1, size, file) != size) return false;
    *received += size;
    return true;
}
#endif
