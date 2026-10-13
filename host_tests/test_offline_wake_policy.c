#include <assert.h>
#include "offline_wake_policy.h"

int main(void)
{
    time_t now = 1791900000;
    assert(offline_network_delay(0) == 0);
    assert(offline_network_delay(1) == 300);
    assert(offline_network_delay(2) == 300);
    assert(offline_network_delay(3) == 172800);
    assert(offline_network_delay(100) == 172800);
    assert(offline_network_due(now, now + 172800, 3, true));
    assert(!offline_network_due(now, now + 172800, 3, false));
    assert(!offline_network_due(now + 86400, now + 172800, 3, false));
    assert(offline_network_due(now + 172800, now + 172800, 3, false));
    assert(offline_network_due(now, now + 172801, 3, false));
    assert(offline_network_due(0, now + 172800, 3, false));
    assert(offline_low_battery_hold(10, false, true));
    assert(!offline_low_battery_hold(-1, false, true));
    assert(!offline_low_battery_hold(11, false, true));
    assert(!offline_low_battery_hold(1, true, true));
    assert(!offline_low_battery_hold(1, false, false));
    return 0;
}
