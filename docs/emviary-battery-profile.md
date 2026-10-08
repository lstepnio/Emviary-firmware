# Emviary E1002 battery profile

The E1002 board header selects `EMVIARY_CLOUD_ONLY`. Other board drivers keep
upstream behavior. This profile removes unused wake work without changing the
panel startup, refresh, or deep-sleep sequence.

## Removed wake work

- The microSD rail stays off. The SD mount/probe and its 500 ms power-up wait
  are skipped. Cloud downloads, current artwork, previews, debug logs, and
  recovery uploads use the existing internal LittleFS fallback.
- The additional 500 ms main startup delay intended for AXP2101 power rails is
  skipped. E1002 has no AXP2101. Display-specific waits remain intact.
- Home Assistant notifications, rotation vetoes, and the HA-only 10-second
  configuration window are disabled. Saved HA settings remain in NVS.
- Interactive startup keeps the synchronous cloud firmware-policy check, but
  skips the redundant delayed GitHub check and its 10-second startup wait.

Local processing and album APIs stay initialized for recovery compatibility.

WiFi profiles, saved device configuration, cloud policy checks, image/navigation
headers, battery telemetry, remote configuration response handling, server-requested
configuration windows, RTC restoration, and configured wake schedules are retained.
Green-button and cold-boot recovery still provide the web UI, local image import,
configuration, and mDNS. The profile intentionally disables SD albums and SD
`wifi.txt` provisioning. Existing files on an SD card are not migrated or erased;
they are simply not mounted. Import an image through the recovery web UI if needed.
Stored storage-rotation settings remain functional against internal flash, but
SD-only albums are unavailable in this profile.

## Storage budget

The existing 32 MiB partition table reserves `0x018e0000` bytes (24.875 MiB) for
LittleFS and two independent `0x380000`-byte (3.5 MiB) OTA slots. OTA writes to
the inactive application slot, not to LittleFS. The 800 x 480 four-bit framebuffer
is 192,000 bytes before EPDGZ compression. Current artwork plus a staging copy
requires roughly 384,000 bytes at that raw payload size, plus compression headers,
previews, filesystem overhead, and any optional saved uploads/logs. Existing
persistent storage remains mounted normally and is not reformatted by this
profile. Available space still depends on accumulated imports, logs, and saved
downloads; leave album saving disabled for the normal daily cloud-art workflow.

## Validation and remaining measurements

A build can verify compilation, and host pipeline tests can check decoding and
publication behavior. Neither measures battery endurance. Record timer, left/right
white-button, green-button, network-failure, unchanged-image, and OTA wakes on real
hardware. Compare `Awake for ... ms this wake` logs and measure supply current
across the complete cycle, including panel refresh and deep sleep. Check SD rail
voltage with a card inserted, displayed image integrity, current-image preview,
WiFi reconnect, remote config, and green-button recovery before release.

The known fixed waits removed total 1 second per ordinary boot (SD plus unrelated
rail stabilization), before any SD-probe overhead. The duplicate check's 10-second
wait affects interactive startup. Further savings from skipped HA requests depend
on old HA settings and reachability. WiFi/TLS/downloads and panel refresh remain
necessary wake costs. No battery-life estimate is justified without physical
current and duty-cycle measurements.

## Battery sample headers

Every image fetch, including requests that receive 304, reports
`X-Battery-Percentage` (0 to 100, omitted when unknown), `X-Battery-Voltage`
(integer millivolts, omitted when nonpositive), `X-Battery-Charging`
(`true` or `false` from the board charging signal), and `X-USB-Connected`
(`true` or `false` from the board USB detection). USB present and charging
are separate states: a full battery may report USB true and charging false.
On SY6974B boards these flags use charger status and power-good. On older
ETA6003 boards charging reports false, and USB falls back to USB serial/JTAG
connection detection, which can miss a data-less USB charger.
The boolean board API has no unknown sentinel; these are observed firmware
flags, not independently measured charger status or a calibrated fuel gauge.
