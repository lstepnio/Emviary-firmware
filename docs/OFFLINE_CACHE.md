# Offline composition cache

E1002 cloud frames cache ordinary bird and Colorado wildlife compositions after a verified download successfully refreshes the panel. Special-day content never enters the cache. Legacy or invalid metadata is ignored while online delivery remains usable.

## Contract

Request `X-Emviary-Offline-Version: 1`. Response `X-Emviary-Offline` is bounded ASCII/base64 JSON containing version 1, positive numeric-string composition ID, SHA256 of the exact downloaded gzip body, ordinary-art eligibility, season, and an exact neutral background tile. The tile is zlib-compressed row-packed Spectra6 nibbles, high nibble first. Limits: header 4096 bytes, compressed tile 2048 bytes, inflated tile 64 KiB, JSON nesting four levels. Hash, palette codes, dimensions, lengths and season are checked before admission. Current backend layout is 800x480 landscape; other layouts are not admitted until their corresponding contract exists.

Offline playback copies the backend-generated neutral pixels into RAM to remove weather text, temperatures and icons. The ESP32 does not render or dither weather locally.

## Storage and safety

A bounded directory scan recovers at most the newest 256 complete pairs and 16 MiB of cache data. Admission preserves `max(4 MiB, 25% filesystem capacity)` free space plus 256 KiB staging margin, using actual LittleFS usage. If staging is unsafe, admission is skipped without sacrificing fallback art.

Staged files and atomic rename publish the body followed by metadata; metadata marks a complete pair. Publication precedes FIFO eviction. Repeated valid compositions are deduplicated, and corrupt old bodies can be replaced on verified re-download. Incomplete stages, orphan bodies and excluded older complete pairs are cleaned only during admission. Cache reads and diagnostics never clean or format the filesystem.

Offline display uses two alternating neutral snapshot slots and an atomically replaced current-image link. Save the neutral recovery target and invalidate cloud ETag before panel refresh. A preparation failure preserves the prior slot. Cursor and offline identity are persisted after panel driver acknowledgement. Filesystem transactions and physical panel changes cannot be jointly atomic; interruption can retain the prior cursor. Offline identity never claims an online ETag or cloud recovery proof.

## Playback and battery

FIFO describes eviction; playback circles forward for RIGHT and backward for LEFT. Skip the current image when valid seasonal alternatives exist; skip corrupt entries. One already-neutral image holds without another refresh. Seasons use local meteorological month windows, selecting evergreen alternatives if clock is untrusted. If alternatives are unavailable, neutralize the current ordinary cached art and hold rather than keep stale weather indefinitely.

Nightly local rotation is independent of cloud retry cooldown. Repeated failures limit cloud probes to once per 48 hours. Green/power-on permits manual reconnect; white navigation during cooldown does not scan Wi-Fi. Cached-only wakes skip cloud, SNTP, OTA checks and debug logging. Scheduled rotation holds at critical battery (10%, battery power only). Normal cloud-only fetch errors use RTC memory rather than NVS writes. Actual battery energy remains unmeasured.

Diagnostics and normal check-ins expose offline mode, composition ID, cache count/bytes and rotations since contact. Cloud previews are last cloud-confirmed until contact resumes.

## Tests

Host tests exercise actual parser/cache with real cJSON/zlib and a digest-provider stub: reserve refusal, allocation failure, metadata write/publication failure, orphan stages, corruption, re-download repair, FIFO eviction, cursor restart, interruption after publication before eviction, seasonal filtering and neutral hold. Pure policy and wake tests cover bounds and battery behavior. Software tests do not constitute physical panel or battery measurement. Hardware acceptance belongs to release validation.
