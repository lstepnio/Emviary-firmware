#include <assert.h>

#include "offline_policy.h"
int main(void)
{
    const size_t mib = 1024u * 1024u;
    assert(offline_reserve(24 * mib) == 6 * mib);
    assert(offline_reserve(8 * mib) == 4 * mib);
    assert(offline_can_stage(24 * mib, 10 * mib, 256 * 1024));
    assert(!offline_can_stage(24 * mib, 18 * mib, 1));
    assert(!offline_can_stage(24 * mib, 25 * mib, 0));
    assert(!offline_can_stage(24 * mib, 0, SIZE_MAX));
    assert(offline_patch_bounds(200, 10, 580, 36, 800, 480, 10440));
    assert(!offline_patch_bounds(200, 10, 581, 36, 800, 480, 10440));
    assert(!offline_patch_bounds(200, 10, 580, 36, 800, 480, 10439));
    assert(!offline_patch_bounds(UINT32_MAX, 10, 580, 36, 800, 480, 10440));
    assert(!offline_patch_bounds(200, 470, 580, 36, 800, 480, 10440));
    assert(offline_next_index(2, 3, false) == 0);
    assert(offline_next_index(0, 3, true) == 2);
    assert(offline_next_index(0, 1, true) == 0);
    assert(offline_season_allowed(0, 0));
    assert(!offline_season_allowed(3, 0));
    assert(offline_month_season(10) == 3);
    assert(offline_month_season(12) == 4);
    assert(offline_season_allowed(3, 3));
    assert(!offline_season_allowed(1, 3));
    return 0;
}
