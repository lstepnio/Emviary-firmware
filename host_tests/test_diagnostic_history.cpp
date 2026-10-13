#include <gtest/gtest.h>
#include <cstdlib>
#include "diagnostic_history.h"

TEST(DiagnosticHistory, FailureKeepsLastSuccessfulRefresh) {
    diagnostic_history_t history = {};
    diagnostic_history_refresh(&history, true, 1791896400);
    diagnostic_history_refresh(&history, false, 1791900000);
    EXPECT_EQ(history.last_success, 1791896400);
    EXPECT_TRUE(history.had_success);
}

TEST(DiagnosticHistory, UnsynchronizedClockDoesNotInventOrReplaceATime) {
    diagnostic_history_t history = {};
    diagnostic_history_refresh(&history, true, 42);
    EXPECT_EQ(history.last_success, 0);
    EXPECT_TRUE(history.had_success);
    diagnostic_history_refresh(&history, true, 1791896400);
    diagnostic_history_refresh(&history, true, 42);
    EXPECT_EQ(history.last_success, 1791896400);
    char output[48];
    diagnostic_local_time(output, sizeof(output), 0, "Time unavailable");
    EXPECT_STREQ(output, "Time unavailable");
}

TEST(DiagnosticHistory, LocalRefreshTimeUsesDenverAndDaylightSaving) {
    setenv("TZ", "MST7MDT,M3.2.0,M11.1.0", 1); tzset();
    char output[48];
    diagnostic_local_time(output, sizeof(output), 1791896400, "Unavailable");
    EXPECT_STREQ(output, "Oct 13, 7:00 AM MDT");
    diagnostic_local_time(output, sizeof(output), 1767283200, "Unavailable");
    EXPECT_STREQ(output, "Jan 1, 9:00 AM MST");
}

TEST(DiagnosticHistory, NetworkHistoryIsBoundedAndKeepsUsefulAddress) {
    diagnostic_history_t history = {};
    diagnostic_history_network(&history, "A very long SSID beyond the supported network name limit", "192.168.1.24", -63);
    EXPECT_EQ(strlen(history.ssid), 32);
    EXPECT_STREQ(history.ip, "192.168.1.24");
    diagnostic_history_network(&history, "Gift network", "0.0.0.0", -70);
    EXPECT_STREQ(history.ip, "");
    EXPECT_STREQ(history.ssid, "Gift network");
    EXPECT_TRUE(history.rssi_valid);
}
