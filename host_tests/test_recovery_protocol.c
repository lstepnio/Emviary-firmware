#include "ota_manager.h"
#include "recovery_checkpoint.h"
#include "recovery_protocol.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void)
{
    assert(ota_state_blocks_recovery(OTA_STATE_CHECKING));
    assert(ota_state_blocks_recovery(OTA_STATE_DOWNLOADING));
    assert(ota_state_blocks_recovery(OTA_STATE_INSTALLING));
    assert(
        ota_state_blocks_recovery(OTA_STATE_SUCCESS)); // Three-second pending reboot must finish.
    assert(!ota_state_blocks_recovery(OTA_STATE_IDLE));
    assert(!ota_state_blocks_recovery(
        OTA_STATE_UPDATE_AVAILABLE)); // An idle offer must not stall recovery.
    assert(!ota_state_blocks_recovery(OTA_STATE_ERROR));
    assert(recovery_crc32("123456789", 9) == 0xcbf43926u);
    char frame[512];
    uint32_t seq;
    char *json;
    const char *hello = "{\"cmd\":\"HELLO\"}";
    assert(recovery_frame_encode(frame, sizeof(frame), 42, hello) > 0);
    frame[strlen(frame) - 1] = 0;
    assert(recovery_frame_decode(frame, &seq, &json) && seq == 42 && !strcmp(json, hello));
    assert(!recovery_frame_decode("ROM boot text", &seq, &json));
    char bad[] = "!EMV1:1:3:{}x:00000000";
    assert(!recovery_frame_decode(bad, &seq, &json));
    char longline[] = "!EMV1:1:999999:{}:00000000";
    assert(!recovery_frame_decode(longline, &seq, &json));
    char overflow[] = "!EMV1:4294967296:2:{}:a3a6bf43";
    assert(!recovery_frame_decode(overflow, &seq, &json));
    assert(!recovery_identifier_valid("bad\nvalue"));
    assert(!recovery_identifier_valid(""));
    char identifier[66];
    memset(identifier, 'a', 65);
    identifier[65] = 0;
    assert(!recovery_identifier_valid(identifier));
    recovery_lease_t l = {0};
    assert(recovery_lease_begin(&l, "session", "nonce", 0));
    assert(!recovery_lease_begin(&l, "other", "nonce", 1));
    assert(recovery_lease_begin(&l, "session", "nonce", 80000));
    assert(l.started == 0);
    assert(recovery_lease_active(&l, 169999));
    assert(!recovery_lease_active(&l, 170000));
    assert(!l.nonce[0]);
    assert(recovery_lease_begin(&l, "session", "nonce", 0));
    for (unsigned t = 15000; t < 600000; t += 15000)
        assert(recovery_lease_begin(&l, "session", "nonce", t));
    assert(!recovery_lease_active(&l, 600000));
    assert(!l.session[0]);
    // A reset loses the lease; a captured command cannot inherit an earlier boot.
    memset(&l, 0, sizeof(l));
    assert(!recovery_lease_match(&l, "session", "nonce", 1));
    assert(recovery_frame_encode(frame, 5, 42, hello) < 0);
    // Reply encoding is not limited by the 384-byte request decoder ceiling.
    char status[900], reply[1024];
    memset(status, 'a', sizeof(status) - 1);
    status[899] = 0;
    int status_length = recovery_frame_encode(reply, sizeof(reply), 42, status);
    assert(status_length > 900 && strstr(reply, "!EMV1:42:899:") == reply);
    assert(recovery_frame_encode(reply, 900, 42, status) < 0);
    assert(recovery_fetch_should_retry(true, false, false, true,
                                       false)); // Same failed session resumes once.
    assert(!recovery_fetch_should_retry(true, false, true, true,
                                        false)); // Already queued retry is idempotent.
    assert(!recovery_fetch_should_retry(true, true, false, true,
                                        false)); // Active transfer is preserved.
    assert(!recovery_fetch_should_retry(true, false, false, true,
                                        true)); // Completed picture is preserved.
    assert(!recovery_fetch_should_retry(true, false, false, false,
                                        false)); // Normal renewals do not fetch.
    assert(!recovery_fetch_should_retry(false, false, false, true,
                                        false)); // New sessions use the initial fetch.
    recovery_awake_guard_t guard = {0};
    // Battery wakes and unverified UART challenges do not acquire a PM guard.
    assert(recovery_awake_guard_update(&guard, 0, false) == RECOVERY_GUARD_UNCHANGED);
    assert(recovery_awake_guard_update(&guard, 0, true) == RECOVERY_GUARD_UNCHANGED);
    recovery_awake_guard_verify_usb(&guard, 1000, false);
    assert(recovery_awake_guard_update(&guard, 1000, false) == RECOVERY_GUARD_ACQUIRE);
    assert(recovery_awake_guard_update(&guard, 90999, false) == RECOVERY_GUARD_UNCHANGED);
    recovery_awake_guard_verify_usb(&guard, 90999, false); // Positive poll cannot rearm the window.
    assert(guard.window_expires == 91000);
    assert(recovery_awake_guard_update(&guard, 91000, false) == RECOVERY_GUARD_RELEASE);
    assert(recovery_awake_guard_update(&guard, 92000, false) == RECOVERY_GUARD_UNCHANGED);
    // An explicit lease protects the UART beyond the initial window.
    recovery_awake_guard_end_window(&guard);
    assert(recovery_awake_guard_update(&guard, 92000, true) == RECOVERY_GUARD_ACQUIRE);
    assert(recovery_awake_guard_update(&guard, 600000, true) == RECOVERY_GUARD_UNCHANGED);
    assert(recovery_awake_guard_update(&guard, 600001, false) == RECOVERY_GUARD_RELEASE);
    // END keeps the held lock until its serialized reply and bounded TX drain finish.
    assert(recovery_awake_guard_update(&guard, 700000, true) == RECOVERY_GUARD_ACQUIRE);
    recovery_awake_guard_end_window(&guard);
    assert(recovery_awake_guard_update(&guard, 700001, true) ==
           RECOVERY_GUARD_UNCHANGED); // reply pending
    assert(recovery_awake_guard_update(&guard, 700002, false) == RECOVERY_GUARD_RELEASE);
    recovery_awake_guard_verify_usb(&guard, 700003, false);
    assert(recovery_awake_guard_update(&guard, 700003, false) ==
           RECOVERY_GUARD_UNCHANGED); // still plugged, no rearm
    // Only a new verified physical attachment gets a fresh bounded window.
    recovery_awake_guard_usb_removed(&guard);
    recovery_awake_guard_verify_usb(&guard, 800000, false);
    assert(recovery_awake_guard_update(&guard, 800000, false) == RECOVERY_GUARD_ACQUIRE);
    recovery_awake_guard_usb_removed(&guard);
    assert(recovery_awake_guard_update(&guard, 800001, true) == RECOVERY_GUARD_RELEASE);
    assert(recovery_awake_guard_update(&guard, 800002, false) == RECOVERY_GUARD_UNCHANGED);
    recovery_display_state_t display = {0};
    assert(!recovery_display_request_restore(&display));
    assert(recovery_display_request_test(&display));
    assert(!recovery_display_request_test(&display));
    assert(!recovery_display_request_restore(&display));
    display.test_pending = false;
    display.overlay = true;
    assert(recovery_display_request_restore(&display));
    assert(recovery_display_request_restore(
        &display)); // Repeated restore safely queues the same saved art.
    assert(!recovery_display_request_test(&display));
    display.restore_pending = false;
    display.restoring = true;
    assert(recovery_display_request_restore(&display));
    assert(!display.restore_pending);
    display.restoring = false;
    display.restored = true;
    assert(recovery_display_request_restore(&display));
    assert(!display.restore_pending);
    memset(&display, 0, sizeof(display));
    assert(recovery_display_request_test(&display));
    recovery_checkpoint_t checkpoint = {0};
    checkpoint.magic = RECOVERY_CHECKPOINT_MAGIC;
    checkpoint.active = 1;
    checkpoint.boots_left = 5;
    strcpy(checkpoint.session, "session");
    strcpy(checkpoint.stage, "display");
    assert(recovery_checkpoint_valid(&checkpoint));
    for (int i = 0; i < 5; i++)
        assert(recovery_checkpoint_resume(&checkpoint, 0, false));
    assert(!recovery_checkpoint_resume(&checkpoint, 0, false));
    assert(!checkpoint.active);
    checkpoint.magic = RECOVERY_CHECKPOINT_MAGIC;
    checkpoint.active = 1;
    checkpoint.boots_left = 5;
    strcpy(checkpoint.session, "session");
    assert(!recovery_checkpoint_resume(&checkpoint, 0, true));
    checkpoint.magic = RECOVERY_CHECKPOINT_MAGIC;
    checkpoint.active = 1;
    checkpoint.boots_left = 5;
    strcpy(checkpoint.session, "session");
    checkpoint.expires_at = 1735689601;
    assert(!recovery_checkpoint_resume(&checkpoint, 1735689601, false));
    checkpoint.magic = RECOVERY_CHECKPOINT_MAGIC;
    checkpoint.active = 1;
    checkpoint.boots_left = 5;
    strcpy(checkpoint.session, "session");
    for (int i = 0; i < 16; i++) {
        assert(recovery_checkpoint_write_allowed(&checkpoint));
        checkpoint.writes++;
    }
    assert(!recovery_checkpoint_write_allowed(&checkpoint));
    checkpoint.session[64] = 'x';
    memset(checkpoint.session, 'x', 65);
    assert(!recovery_checkpoint_valid(&checkpoint));
    puts("recovery protocol framing, corruption, identity, lease and cleanup tests passed");
}
