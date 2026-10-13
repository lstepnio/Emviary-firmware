#pragma once
#include <stdbool.h>
#include <stdint.h>
#define RECOVERY_CHECKPOINT_MAGIC 0x45524331u
#define RECOVERY_CHECKPOINT_WRITES 16
#define RECOVERY_CHECKPOINT_BOOTS 5
typedef struct {
    uint32_t magic;
    uint8_t boots_left, writes, active, reserved;
    int64_t expires_at;
    char session[65], stage[32], error[48];
} recovery_checkpoint_t;
bool recovery_checkpoint_valid(const recovery_checkpoint_t *record);
bool recovery_checkpoint_resume(recovery_checkpoint_t *record, int64_t wall_seconds,
                                bool scheduled_wake);
bool recovery_checkpoint_write_allowed(const recovery_checkpoint_t *record);
