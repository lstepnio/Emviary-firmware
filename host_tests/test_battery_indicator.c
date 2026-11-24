#include "battery_indicator.h"
#include <assert.h>
#include <stdio.h>
int main(void)
{
    assert(!battery_warning_next(false, 26, false));
    assert(battery_warning_next(false, 25, false));
    for (int p = 0; p <= 25; ++p) assert(battery_warning_next(false, p, false));
    assert(battery_warning_next(true, 27, false));
    assert(!battery_warning_next(true, 28, false));
    assert(!battery_warning_next(true, 10, true));
    assert(!battery_warning_next(true, -1, false));
    assert(!battery_warning_next(true, 101, false));
    assert(!battery_warning_next(false, 27, false));
    int count = 0;
    for (int y = -1; y <= 12; ++y) for (int x = -1; x <= 27; ++x) {
        bool ink = battery_icon_pixel(x, y);
        if (x < 0 || y < 0 || x >= 27 || y >= 12) assert(!ink);
        if (ink) ++count;
    }
    assert(count == 152); /* outline, terminal and small low fill */
    assert(!battery_icon_pixel(12, 6)); /* hollow, no background box */
    assert(battery_icon_pixel(25, 5));
    assert(BATTERY_ICON_LEFT > 8); /* clear the composition border */
    assert(480 - BATTERY_ICON_BOTTOM - BATTERY_ICON_HEIGHT == 444);
    puts("battery indicator thresholds and transparent geometry passed");
}
