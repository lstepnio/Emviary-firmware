#ifndef EMVIARY_DIAGNOSTIC_HISTORY_H
#define EMVIARY_DIAGNOSTIC_HISTORY_H

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

// Retained in RTC RAM, never persisted or updated by opening the status screen.
typedef struct {
    time_t last_success;
    bool had_success;
    char ssid[33];
    char ip[16];
    int rssi_dbm;
    bool rssi_valid;
} diagnostic_history_t;

static inline bool diagnostic_time_valid(time_t timestamp)
{
    return timestamp >= (time_t)1577836800; // 2020-01-01, excludes unsynchronized clocks.
}

static inline void diagnostic_history_refresh(diagnostic_history_t *history,
                                               bool success, time_t timestamp)
{
    if (!success) return;
    history->had_success = true;
    if (diagnostic_time_valid(timestamp)) history->last_success = timestamp;
}

static inline void diagnostic_history_network(diagnostic_history_t *history,
                                               const char *ssid, const char *ip,
                                               int rssi_dbm)
{
    if (ssid && *ssid) {
        if (strcmp(history->ssid, ssid) && (!ip || !*ip || !strcmp(ip, "0.0.0.0")))
            history->ip[0] = 0;
        snprintf(history->ssid, sizeof(history->ssid), "%s", ssid);
    }
    if (ip && *ip && strcmp(ip, "0.0.0.0"))
        snprintf(history->ip, sizeof(history->ip), "%s", ip);
    history->rssi_dbm = rssi_dbm;
    history->rssi_valid = true;
}

static inline void diagnostic_local_time(char *output, size_t size, time_t timestamp,
                                        const char *fallback)
{
    struct tm local;
    if (!diagnostic_time_valid(timestamp) || !localtime_r(&timestamp, &local)) {
        snprintf(output, size, "%s", fallback);
        return;
    }
    char month[8], zone[12];
    strftime(month, sizeof(month), "%b", &local);
    strftime(zone, sizeof(zone), "%Z", &local);
    snprintf(output, size, "%s %d, %d:%02d %s%s%s", month, local.tm_mday,
             local.tm_hour % 12 ? local.tm_hour % 12 : 12, local.tm_min,
             local.tm_hour < 12 ? "AM" : "PM", *zone ? " " : "", zone);
}

#endif
