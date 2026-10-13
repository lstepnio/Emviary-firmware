#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifndef OFFLINE_CACHE_MAX_ENTRIES
#define OFFLINE_CACHE_MAX_ENTRIES 256u
#endif
#ifndef OFFLINE_CACHE_BUDGET
#define OFFLINE_CACHE_BUDGET (16u * 1024u * 1024u)
#endif
#define OFFLINE_STAGING_MARGIN (256u * 1024u)
static inline size_t offline_reserve(size_t total)
{
    size_t q = total / 4;
    return q > 4u * 1024u * 1024u ? q : 4u * 1024u * 1024u;
}
static inline bool offline_can_stage(size_t total, size_t used, size_t addition)
{
    size_t reserve = offline_reserve(total);
    return used <= total && addition <= total - used && reserve <= total - used - addition &&
           OFFLINE_STAGING_MARGIN <= total - used - addition - reserve;
}
static inline bool offline_patch_bounds(unsigned x, unsigned y, unsigned w, unsigned h,
                                        unsigned width, unsigned height, size_t length)
{
    return w && h && !(w & 1) && x <= width && y <= height && w <= width - x && h <= height - y &&
           ((size_t) w / 2) * h == length && length <= 64u * 1024u;
}
static inline unsigned offline_next_index(unsigned current, unsigned count, bool previous)
{
    return count ? (current + (previous ? count - 1 : 1)) % count : 0;
}
/* Northern hemisphere meteorological seasons. Zero means an untrusted clock. */
static inline unsigned offline_month_season(unsigned month)
{
    if (month < 1 || month > 12)
        return 0;
    return month >= 3 && month <= 5    ? 1
           : month >= 6 && month <= 8  ? 2
           : month >= 9 && month <= 11 ? 3
                                       : 4;
}
static inline bool offline_season_allowed(unsigned entry, unsigned current)
{
    return entry == 0 || (current != 0 && entry == current);
}
