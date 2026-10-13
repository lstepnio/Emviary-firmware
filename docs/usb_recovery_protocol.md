# USB recovery protocol version 1

The E1002 connected adapter identifies as USB Serial, VID:PID `1a86:7523`.
It bridges the application and ROM UART0. The responder inherits the default
115200 baud console UART pins. No firmware DTR/RTS toggling is performed.
Host inventory establishes the bridge mapping, not electrical reset qualification.

Each request and response is an ASCII line:

```
!EMV1:<unsigned decimal sequence>:<JSON byte length>:<compact JSON>:<8 hex CRC32>\n
```

CRC32 is IEEE, reflected polynomial `0xedb88320`, initial/final XOR `0xffffffff`,
over the UTF-8 JSON bytes only. Requests contain at most 384 JSON bytes;
responses at most 1023. Malformed frames are silently discarded. Receive buffer
is fixed at 448 bytes, UART RX buffer 1024 bytes. Oversized lines are discarded
until newline. Requests are flat JSON objects; nested objects/arrays are refused
before cJSON parsing to bound parser recursion. Sequence IDs correlate responses.

Requests:

- `{"cmd":"HELLO"}`: unbound probe, does not disclose the active session nonce.
  Reports `operation_busy` during OTA checking, download, install or pending
  reboot, so the host waits before considering reset/ROM even on a version mismatch.
  An idle update offer does not block recovery. BEGIN uses the same busy predicate.
- `{"cmd":"BEGIN_RECOVERY","session_id":"...","nonce":"...","bundle_sha256":"..."}`:
  activates a RAM-only lease and queues one full picture request when normal
  initialization and Wi-Fi permit it. Same-session BEGIN is idempotent while pending, in flight or complete. After
  a failed fetch, another valid BEGIN clears failed pipeline evidence and queues
  one retry; further BEGIN requests do not duplicate that pending retry. A second
  session or an active OTA operation returns `operation_busy`/`session_conflict`.
- `{"cmd":"STATUS","session_id":"...","nonce":"..."}`: renews and reports the
  matching session. Identifiers contain 1 to 64 ASCII letters, digits, `_` or `-`.
  The optional bundle digest is exactly 64 hexadecimal characters.
- `{"cmd":"DISPLAY_TEST","session_id":"...","nonce":"..."}`: one explicit
  diagnostic overlay attempt per session after display initialization. Does not
  count as downloaded-picture or recovery success. Current artwork is preserved.
- `{"cmd":"RESTORE_PICTURE","session_id":"...","nonce":"..."}`: restores
  saved artwork after a display test without ending the lease or fetching again.
  `test_pattern` becomes `restored` or `error`; ETag remains unchanged.
- `{"cmd":"END_RECOVERY","session_id":"...","nonce":"..."}`: clears the lease.
  Completed overlay tests are restored from saved artwork when available by the
  worker. Existing diagnostic restoration state survives interrupted refresh.

Every response has `protocol`, `boot_id` (RAM random 16 hex), `device_identity`
(factory MAC 12 hex), `firmware`, `board` (`seeedstudio_reterminal_e1002`),
`reset_reason`, `stage`, `error`, `active`, `session_id`, `nonce`,
`configuration_ready` (loaded config, nonempty binding/URL and valid saved networks),
`network_count`, `config_revision`, `storage_persistent`, `wifi_connected`, `cloud`, `image`, `panel`,
`image_sha256`, `image_id`, `test_pattern`, `prior_stage`, `prior_error`, and
`diagnostic_error`. Normal `error` is empty. State values:

- cloud: `idle`, `authenticated` (successful correlated ACK), `error`
- image: `idle`, `verified` (full-body SHA256 matches server digest), `error`
- panel: `idle`, `complete` (existing checked driver finished), `error`
- test_pattern: `idle`, `pending`, `complete`, `restored`, `error`

The lease expires 90 seconds after the last valid renewal and at 600 seconds
from its first BEGIN. Renewal cannot extend the absolute limit. Physical USB
removal clears it after three negative power observations, 500 ms apart, only
if USB power was positively observed during the session. Hardware where the
power detector never reports USB still accepts a UART challenge and uses the
lease timeout. Driver removal detection and reset timings require hardware tests.

Startup exposes the responder before HAL/storage/NVS on a cold boot, and after
HAL on USB-powered deep-sleep wakes. Scheduled battery wakes do not start a
listener. The responder remains available if main initialization blocks, subject
to SDK flash/cache/interrupt scheduling. Returned HAL/storage/config/display
failures have a ten-second challenge window then sleep on a 15-minute timer;
a valid recovery lease extends the window. NVS failure preserves settings.
Normal debug logging is not enabled by this protocol.

The UART task uses a 6144-byte stack. The 12288-byte picture/test worker is
allocated only on explicit BEGIN and deleted on session exit. Runtime heap,
stack high-water marks, energy use and physical sleep/OTA/button behavior are
not established by a successful build or host tests.

## Cloud evidence

Full image requests carry `X-Emviary-Recovery-Session`, `-Nonce`, `-Boot`,
`-Device-Id` and `-Bundle-Sha256`, plus existing bearer authentication and
firmware headers. The existing ETag header is removed for recovery. A body is
accepted only after its SHA256 matches `X-Recovery-Image-Sha256`; image identity
comes from `X-Recovery-Image-Id`.

POST `<image origin>/v1/recovery/ack` uses the existing bearer and JSON
`session_id`, `nonce`, `boot_id`, `device_id`, `firmware`, `bundle_sha256`,
`phase`, `image_sha256`, with `error` on failure. Firmware emits `image_verified`,
`panel_refreshed` or `failed`. It does not claim a successful physical picture.
The asynchronous POST has an eight-second overall deadline and four-second
socket operations; redirects are disabled. Failure leaves the downloaded image
and driver evidence separate from cloud acknowledgement.

## Recovery-only checkpoints

Healthy NVS holds one small non-secret checkpoint with session identity,
stage/error, remaining five-boot budget, expiry when wall time is valid, and
write count. No nonce, token, Wi-Fi name/password or raw NVS enters a report.
At most sixteen stage checkpoints are committed per session. Up to five
additional boot-budget commits and one cleanup commit are separate bounded
operations. Healthy persistent storage keeps one small `.usb_recovery.json`
latest-stage report, replaced only with a stage checkpoint. No full trace ring,
continuous serial writes or new partition is introduced.

Crash/cold restart reports prior stage/error but does not recreate a lease or
restore a nonce. Browser must establish a new handshake. A scheduled wake
clears a stale marker without enabling tracing. END, lease expiry, observed USB
removal and pre-sleep cleanup delete the marker and report. Failed checkpoint
writes report `diagnostic_error`; the UART responder does not depend on them.
These host-tested budget rules are not power-loss hardware qualification.
