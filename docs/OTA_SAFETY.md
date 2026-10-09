# Emviary OTA safety boundary

The authenticated cloud policy remains the authority for updates, including pauses,
pins and newer compatible prereleases. The frame selects only its board's named
GitHub asset and validates the advertised integral size, full SHA-256, application
project and version before selecting the new boot slot. TLS uses the ESP-IDF trust
bundle. Metadata redirects are refused so its bearer credential stays on the
configured cloud endpoint. GitHub binary redirects remain necessary and carry no
cloud bearer credential.

Metadata is limited to 64 KiB, a complete response and one complete JSON object,
with nesting limited to 16 levels before parsing, a 20 second deadline and
5 second I/O timeout. Firmware work has a 180 second
deadline, checked between 10 second I/O calls, and is limited by the inactive
application partition. One OTA operation may run at a time, serialized with image
work and sleep teardown. Failed validation preserves the selected boot slot.

The board binding is the cloud's board-specific asset selection plus its digest.
The application descriptor additionally checks project and version; the shared
project name does not independently encode the board. ESP-IDF validates the chip
image format. This is not a secure-boot or signed-firmware trust boundary.

## Boot rollback is not enabled on the installed bootloader

The current build has bootloader application rollback disabled. Application-only
OTA cannot replace that bootloader. Integrity validation prevents a mismatched
binary from being selected, but it cannot guarantee that a correctly downloaded
new application will work after reboot. No battery-life, physical panel, network
recovery or post-install health result follows from the SHA-256 check alone.

A future separately approved USB migration should preserve configuration and
partition layout, enable bootloader rollback, and move pending-image confirmation
to an explicit local initialization self-test. The test must not require cloud or
Wi-Fi availability: an offline frame must remain bootable and recoverable. Validate
successful confirmation, a reset before confirmation and a deliberately failing
self-test with a known-good alternate slot before calling rollback supported.
Do not burn eFuses or enable anti-rollback as part of that migration without a
separate explicit decision and recovery plan.

## Local authentication read errors

Local HTTP authentication remains optional and disabled by default. The existing
configuration loader also disables it if reading the saved HTTP password fails
(for example, a corrupt or oversized NVS value), and logs that state. This release
preserves that existing recovery policy; it does not establish a fail-closed local
authentication guarantee. Avoid treating the optional LAN password as the trust
boundary for cloud credentials or firmware authenticity. A future change needs an
explicit recovery design so a corrupt password cannot permanently lock out its owner.
