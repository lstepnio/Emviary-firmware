#include <gtest/gtest.h>
#include <cstring>
extern "C" {
#include "ota_recovery.h"
}
static ota_recovery_record_t attempt() {
    ota_recovery_record_t record{};
    record.format = OTA_RECOVERY_FORMAT; record.phase = OTA_RECOVERY_ACTIVE;
    strcpy(record.source, "v0.7.1"); memset(record.digest, 'a', 64);
    return record;
}
TEST(OtaRecovery, PanicHoldsSameAutomaticCandidateAcrossBoots) {
    auto record = attempt();
    EXPECT_TRUE(ota_recovery_hold_after_reset(&record, "v0.7.1", true));
    EXPECT_TRUE(ota_recovery_should_hold(&record, "v0.7.1", record.digest, true));
    EXPECT_FALSE(ota_recovery_hold_after_reset(&record, "v0.7.1", false));
    EXPECT_TRUE(ota_recovery_should_hold(&record, "v0.7.1", record.digest, true));
}
TEST(OtaRecovery, ManualRetryOrChangedCandidateOrSourceCanProceed) {
    auto record = attempt(); record.phase = OTA_RECOVERY_HELD;
    EXPECT_FALSE(ota_recovery_should_hold(&record, "v0.7.1", record.digest, false));
    EXPECT_FALSE(ota_recovery_should_hold(&record, "v0.7.3", record.digest, true));
    std::string other(64, 'b');
    EXPECT_FALSE(ota_recovery_should_hold(&record, "v0.7.1", other.c_str(), true));
}
TEST(OtaRecovery, PowerLossAndOtherSourceDoNotCreatePanicHold) {
    auto record = attempt();
    EXPECT_FALSE(ota_recovery_hold_after_reset(&record, "v0.7.1", false));
    EXPECT_FALSE(ota_recovery_hold_after_reset(&record, "v0.7.2", true));
    EXPECT_EQ(record.phase, OTA_RECOVERY_ACTIVE);
}
TEST(OtaRecovery, CorruptPersistedRecordCannotBecomeHold) {
    auto record = attempt(); memset(record.source, 'x', sizeof(record.source));
    EXPECT_FALSE(ota_recovery_record_valid(&record));
    record = attempt(); record.digest[64] = 'x';
    EXPECT_FALSE(ota_recovery_record_valid(&record));
    record = attempt(); record.format = 0;
    EXPECT_FALSE(ota_recovery_hold_after_reset(&record, "v0.7.1", true));
}
