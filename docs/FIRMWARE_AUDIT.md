# E1002 firmware audit

This audit starts from Emviary v0.6.2, commit
`4c9fc939185d163c087a1b2f15d47674664cc21c`. The physical E1002 reported
v0.6.2, 800 x 480 Spectra 6, internal LittleFS and one saved Wi-Fi profile.
The configured nightly rule was `15 3 *`. Known-good application, ELF,
partition table, NVS, OTA selection and diagnostic logs were saved privately.
Secrets and configuration backups must not be published.

## Prioritized findings

| Priority | Finding and evidence | Resulting change | Acceptance evidence |
| --- | --- | --- | --- |
| P1 | EPDGZ accepted short inflate output and incomplete/trailing gzip data before painting. | Exact packed pixel count, gzip stream end, CRC/input consumption, size and six-color nibble checks before any pixel write. | Real reader host tests with corrupt/truncated/oversized streams and invalid ink codes. |
| P1 | Downloads and local uploads shared temporary paths; the display mutex began after transfer. | A recursive operation mutex covers transfer, validation, refresh, source publication and ETag. Cloud upload/calibration routes are removed. | Host lifecycle tests and physical navigation checks. |
| P1 | Replacing `.current.*` before decoding destroyed the last valid source on failure. Writes and close errors were ignored. | Pending files are decoded and refreshed before promotion. Bounded writes, exact HTTP completion and close checks reject bad transfers. | Real filesystem and write-failure tests. |
| P1 | GCA driver logged a completed update after controller BUSY timeout; SPI errors were ignored. | Checked refresh result aborts the update at its first failure and prevents successful metadata publication. | Real driver host fault harness; physical refresh must also be observed. |
| P1 | Sleep could race OTA, refresh, periodic RTC/SNTP work or a queued navigation request. | Image, navigation and periodic barriers protect teardown; scheduled wakes drain and retry pending requests. | Host periodic barrier tests, source review and device button/sleep regression. |
| P1 | LittleFS mount failures and partition mismatches could format automatically; NVS initialization and rejected Wi-Fi could erase saved configuration. | Automatic formatting and credential/NVS erasure are removed. NVS failure sleeps with a 15-minute retry and green wake. Green recovery after failed connection offers a portal while retaining profiles. | Real mount fault harness; physical recovery portal validation. |
| P1 | Provisioning body/field truncation, malformed escapes, scan JSON buffer overflow and loose configuration JSON boundaries. | Complete bounded form receive, full-length Wi-Fi validation, safe scan JSON, strict JSON depth/length/NUL checks. | Input validation tests and real HTTP rejection checks. |
| P1 | OTA operations raced; metadata could be incomplete, oversized or stalled, and authenticated redirects could leak a bearer token. | One claimed operation, immutable install inputs, checked task creation, bounded complete metadata, no authenticated redirects, integral size and full digest/project/version checks. | Host boundary tests, target build and physical OTA installation. |
| P2 | GET configuration echoed cloud/API credentials. | Secrets are write-only presence flags; local recovery saves only changed fields. | Real GET secret omission and persistence checks. |
| P2 | Idle battery wakes reserved a 16 KiB rotation task; green scanning duplicated an 8 KiB task. | Small power supervisor lazily starts active rotation; the cloud scanner handles all three buttons. | Source stack accounting and live heap measurements. |
| P2 | Full upstream gallery, local processing UI, EXIF assets and calibration sample were embedded despite backend rendering. | Self-contained connection/recovery and provisioning pages replace the SPA on E1002. Processing/palette headers and initializers are skipped. | Target linker size, local page checks and setup regression. |

## Work and recovery boundaries

The backend still owns art, bird selection, dithering, image history and schedule
configuration. White buttons coalesce one pending previous/next request while a
refresh runs. Green wakes local recovery. Both local hostnames are retained.

E1002 image bodies are capped at 256 KiB with a 60-second attempt deadline;
thumbnail bodies at 256 KiB with 30 seconds. Retry admission retains the existing
20-second budget and three-second delay, so a slow attempt is not repeated.
Only display-ready EPDGZ is accepted by the cloud image path. Thumbnail requests
are restricted to the image's HTTPS origin and do not follow redirects.

OTA metadata is capped at 64 KiB and 20 seconds. Firmware download is capped by
the advertised size, inactive partition and a 180-second transfer deadline.
Lock admission and controller initialization/refresh are separate bounded phases.
These limits do not form one hard deadline for a whole wake. Wi-Fi discovery,
time sync, manual navigation and panel phases add to wake duration. SPI polling
uses the SDK-required `portMAX_DELAY`; bus acquisition is bounded at five seconds
and each controller BUSY phase at 40 seconds. An ordinary controller failure
cannot be treated as a completed refresh, but a processor/driver fault still
requires watchdog or USB recovery.

No partition layout, bootloader, eFuse, secure-boot or anti-rollback settings were
changed. See [OTA_SAFETY.md](OTA_SAFETY.md): automatic post-boot rollback remains
unsupported until a separately approved bootloader migration. An inactive-slot
download failure leaves the selected application unchanged. A correctly hashed
application can still contain a software defect.

USB recovery must use the board-specific application binary and existing
partition map. Read the active OTA slot and save NVS/OTA metadata first. Never
use an erase-all or merged image as routine application recovery. Restore a
known-good application to the selected slot without modifying credentials,
bootloader or partitions. Only restore an NVS backup after an explicit decision
that the current configuration is lost or unsuitable.

## Evidence limits

Target build, linked size and physical results are recorded in the parent
project's firmware resource audit. Host fault tests execute real reader, driver,
periodic and storage sources with mocks; they do not reproduce actual scheduler,
radio, flash corruption or power-loss behavior. Interrupted physical OTA,
intentional brownout, corrupted physical NVS and boot rollback require separate
controlled recovery tests.

No current profiler or inline measurement device was available. Firmware bytes,
reserved stacks, heap and awake duration are not energy measurements. No battery
life or three-month charging interval is established. A battery-powered overnight
and multi-day retention test remains necessary before claiming the historical
white-screen symptom is resolved.

Public/frame mirroring follows backend delivered-image tracking. A fetched image
does not prove the physical panel finished its refresh; the new controller error
reporting makes that distinction visible locally, but a separate successful-panel
acknowledgment protocol would improve backend certainty.
