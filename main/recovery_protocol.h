#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define RECOVERY_JSON_MAX 384
#define RECOVERY_ID_MAX 64
#define RECOVERY_LEASE_MS 90000ULL
#define RECOVERY_ABSOLUTE_MS 600000ULL
typedef struct {
    char session[65], nonce[65];
    uint64_t started, renewed;
    bool active;
} recovery_lease_t;
uint32_t recovery_crc32(const void *data, size_t length);
bool recovery_frame_decode(char *line, uint32_t *sequence, char **json);
int recovery_frame_encode(char *out, size_t capacity, uint32_t sequence, const char *json);
bool recovery_identifier_valid(const char *value);
bool recovery_lease_active(recovery_lease_t *lease, uint64_t now);
bool recovery_lease_begin(recovery_lease_t *lease, const char *session, const char *nonce,
                          uint64_t now);
bool recovery_lease_match(recovery_lease_t *lease, const char *session, const char *nonce,
                          uint64_t now);
typedef struct {
    bool used, test_pending, restore_pending, overlay, restoring, restored;
} recovery_display_state_t;
bool recovery_display_request_test(recovery_display_state_t *state);
bool recovery_display_request_restore(recovery_display_state_t *state);
bool recovery_fetch_should_retry(bool same_session, bool busy, bool pending, bool failed,
                                 bool complete);
#define RECOVERY_USB_WINDOW_MS 90000ULL
typedef struct {
    uint64_t window_expires;
    bool usb_verified, held;
} recovery_awake_guard_t;
typedef enum {
    RECOVERY_GUARD_UNCHANGED,
    RECOVERY_GUARD_ACQUIRE,
    RECOVERY_GUARD_RELEASE
} recovery_guard_action_t;
void recovery_awake_guard_verify_usb(recovery_awake_guard_t *guard, uint64_t now,
                                     bool active_lease);
void recovery_awake_guard_end_window(recovery_awake_guard_t *guard);
void recovery_awake_guard_usb_removed(recovery_awake_guard_t *guard);
recovery_guard_action_t recovery_awake_guard_update(recovery_awake_guard_t *guard, uint64_t now,
                                                    bool active_lease);
