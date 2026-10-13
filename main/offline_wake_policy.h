#pragma once

#include <stdbool.h>
#include <time.h>

// Keep display cadence independent from network cadence. White navigation
// honors a failed-network cooldown; green/power-on is an explicit reconnect.
static inline int offline_network_delay(unsigned failures)
{
    return failures >= 3 ? 48 * 60 * 60 : (failures ? 5 * 60 : 0);
}

static inline bool offline_network_due(time_t now, time_t retry_after,
                                       unsigned failures, bool force)
{
    if (force || !failures || now < 1577836800)
        return true;
    // Clock corrections must not create an unbounded hold.
    return retry_after <= now || retry_after - now > offline_network_delay(failures);
}

static inline bool offline_low_battery_hold(int percent, bool usb, bool scheduled)
{
    // Unknown measurements do not trigger a hold. A manual navigation remains
    // available; only unattended cached rotation is stopped at critical charge.
    return scheduled && !usb && percent >= 0 && percent <= 10;
}
