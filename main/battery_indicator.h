#pragma once
#include <stdbool.h>

/* Shared thresholds with management. Unknown readings never invent a warning. */
static inline bool battery_warning_next(bool previous, int percent, bool powered)
{
    if (powered || percent < 0 || percent > 100) return false;
    return percent <= 25 || (previous && percent < 28);
}

#define BATTERY_ICON_WIDTH 27
#define BATTERY_ICON_HEIGHT 12
#define BATTERY_ICON_LEFT 20
#define BATTERY_ICON_BOTTOM 24

/* Transparent 24x12 outline plus terminal. No white rectangle or text. */
static inline bool battery_icon_pixel(int x, int y)
{
    if (x < 0 || y < 0 || x >= BATTERY_ICON_WIDTH || y >= BATTERY_ICON_HEIGHT) return false;
    if (x < 24 && (y < 2 || y >= 10 || x < 2 || x >= 22)) return true;
    if (x >= 24 && y >= 4 && y < 8) return true;
    return x >= 4 && x < 7 && y >= 4 && y < 8;
}
