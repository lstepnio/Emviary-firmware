# Persistent gift welcome (protocol 1)

Gift mode is off by default. An explicit server package offers a welcome and
weather-neutral first artwork through `X-Emviary-Gift` on the ordinary image
response. This control is handled before the normal 304/no-body return.

The manifest is at most 3072 bytes. IDs are exactly 32 lowercase hexadecimal
characters. Each asset is at most 256 KiB with an exact byte count and lowercase
SHA256 of its original EPDGZ body. HTTPS URLs must match the configured image URL's
origin; bearer and custom credentials are retained only on that origin, redirects
are disabled, and downloads use the existing absolute and idle deadlines.
Both files undergo complete gzip, panel-size and palette validation before any
welcome pixels are sent. They use dedicated immutable `/gift/` filenames,
not the moving cloud or offline cache.

An atomic NVS record is committed as pending before checked welcome publication,
then ready afterward. A reset during that window retries the welcome; an ordinary
ready boot leaves retained pixels alone. A durable diagnostics marker permits
cold recovery after diagnostics without repainting normal ready boots. Green
returns from diagnostics to the welcome. Timers, local HTTP display/calibration,
USB automatic fetch, ordinary saved-art restoration and left navigation cannot
replace an armed welcome. Publication permission belongs to the current task and
exact gift asset path, including while HTTP requests run concurrently.

Right white reveals the dedicated first file locally. Opened is committed only
after checked publication and durable one-shot receipt. Failure preserves/repaints
the welcome and permits retry. Activation/reveal clears pending navigation taps;
new taps during gift staging or publication and the next second are consumed.
Normal navigation's existing last-press-wins behavior is preserved.

The first image holds for 1800 seconds on the existing wall clock. Ordinary cron
boundaries inside the interval are skipped, without adding a hold-expiration wake.
Manual previous/next ends the hold. A backward clock correction beyond the hold
length invalidates the hold rather than causing an unbounded delay.

Already-online armed wakes use authenticated `/v1/gift` for control and metrics;
there is no new timer or poll. Initial package processing sends one status report
within the existing wake. An offline reveal reports opened on a later normal
contact. Matching cancellation clears the cloud ETag and fetches ordinary art in
the same existing online sync. The server must stop offering cancellation control
after receiving matching cancelled telemetry. Completed ID receipts prevent an
old opened package from rearming after a newer package.

Telemetry uses normal `X-Frame-Metrics` with `gift_version:1`, `gift_id`,
`gift_state` (`ready`, `opened`, `cancelled`, `failed`) and optional error at most
80 characters. Encoded metrics stay within the server's 2048-byte header cap;
oversized optional inventory/history falls back to gift fields plus uptime and
`metrics_trimmed:true`. The normal firmware, battery voltage/percent, charging and USB
headers accompany gift contact. Checked display success is a device report,
not independent physical panel sensing.

## Verification

`host_tests/gift_mode_test` compiles the real lifecycle with fault-injected
transport, integrity, NVS and checked panel calls. `epdgz_test` validates actual
gzip bodies and confirms preflight changes no pixels. `navigation_state_test`
keeps ordinary queued-tap behavior and covers gift tap discard.

Local ESP-IDF build used E1002/ESP32-S3 in `/tmp/emviary-gift-firmware-build`.
No flashing, firmware release or physical claim is implied by this build.

Before release, physically qualify switched-off packing, offline cold right-button
reveal, held activation and double taps, USB, green/diagnostics, scheduled wakes,
OTA/restart, interrupted staging/refresh, cancellation and next saved daily wake.
