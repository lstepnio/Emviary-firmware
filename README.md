# Emviary firmware

Firmware for an Emviary nature-art frame built around the **Seeed Studio
reTerminal E1002**, its 800 × 480 Spectra 6 display, and internal flash storage.
The frame receives prepared compositions from the Emviary service, keeps artwork
visible while asleep, and can browse suitable cached pictures without Wi-Fi.

- [Emviary service and project](https://github.com/lstepnio/Emviary)
- [Public user guide](https://emviary.majjix.com/guide)
- [Cloud management](https://emviary.majjix.com/manage)
- [Firmware releases](https://github.com/lstepnio/Emviary-firmware/releases)

This is an Emviary-specific fork of
[aitjcize/esp32-photoframe](https://github.com/aitjcize/esp32-photoframe).
The E1002 is this fork's release and validation target. Other board drivers and
upstream tools remain in the repository, but their presence does not establish
Emviary compatibility or hardware qualification.

## How the system fits together

The server chooses artwork, renders compositions and weather, manages special
days and schedules, and offers device configuration and firmware policy.
The E1002 downloads prepared EPDGZ pictures over authenticated HTTPS, validates
input, performs the panel refresh, and reports its status on normal contacts.
It does not run the server's renderer or regenerate weather locally.

The E1002 profile uses internal LittleFS for artwork, cache and recovery files.
It does not mount microSD or use the inherited Home Assistant wake workflow.
The small local recovery interface remains available while the frame is awake,
usually at `http://emviary.local`, with `photoframe.local` as a compatibility
fallback, or at the frame's LAN address.

Cloud management is the normal place to change artwork, schedules, special days,
Wi-Fi profiles and update policy. Use the local interface for network recovery,
configuration and diagnostics. The public guide is intended for the recipient
of an already configured frame.

## E1002 controls

| Control | Normal behavior |
| --- | --- |
| Left white, short press | Previous picture |
| Right white, short press | Next picture; reveal first artwork when gift welcome is armed |
| Left white, hold five seconds | Local diagnostic snapshot |
| Green | Wake for local/network access; return from diagnostics to artwork |
| Power switch | Switch the frame off or on; e-ink retains its picture without power |
| USB-C | Charge and power the frame |

On battery, green is a local/network wake, not a next-picture gesture. A normal
USB-powered interactive startup fetches cloud artwork once when Wi-Fi connects.
An armed gift welcome guards that automatic fetch. White-button navigation can
use the offline cache during a cloud retry cooldown.

A refresh takes time. Let it finish before deciding that a button did nothing.
The right white button does not clear the display to blank white in this fork.

## Offline artwork and power

Ordinary bird and wildlife compositions can enter a bounded local cache after a
successful checked cloud refresh. Special-day content is excluded. The service
supplies exact neutral background pixels so offline playback removes weather
instead of displaying an old forecast. Navigation and seasonal selection remain
local; the frame does not synthesize new pictures while disconnected.

Failed or stalled network work has bounded deadlines and retry backoff. Cached
rotation follows the saved schedule independently of cloud retry cooldown.
Green or a power-on permits a manual reconnect attempt. Without a usable cached
alternative, the frame can retain the current picture rather than invent content.
See [offline cache behavior](docs/OFFLINE_CACHE.md) for bounds and admission rules.

A small transparent battery icon appears at **25% or below** and remains until
**28% or above**, or external power is detected. It changes only during a normal
artwork refresh; it does not trigger a separate panel refresh or alarm. Unknown
battery readings do not invent a warning. Charging and USB presence are reported
separately, and hardware revision affects what USB detection can observe.

Deep sleep and reduced wake work are designed to save energy, but battery life
depends on Wi-Fi, schedule, refreshes, board revision and battery condition.
There is no measured endurance promise. See the
[E1002 battery profile](docs/emviary-battery-profile.md) for the implementation.

## Gift welcome

Gift mode is off by default and requires an explicit owner action in management.
The source implementation stages a dedicated welcome and first composition,
checks both assets' sizes, hashes and complete EPDGZ contents, and publishes the
welcome only after both are available locally.

Loading the welcome does not arm automatic opening. The owner checks the actual
screen, then chooses **Ready to wrap** in management. Firmware revalidates both
local pictures and durably acknowledges the wrapped state. During this explicit
setup wake it accepts the final command through a brief bounded online
window; this adds no sleeping wake or scan. If the setup window ends, green wakes
the frame to receive the command.

After the frame reports ready to wrap, switch it off. The next genuine power-on
reveals the preloaded first composition before Wi-Fi initialization, including
when offline. Timer wakes, green/deep-sleep wakes, software OTA restarts, watchdog
and brownout resets preserve the welcome. Right white remains a manual reveal
fallback. Diagnostics restore the sealed state; cancellation prevents opening.
Opened state is committed only after a checked refresh. A failed refresh keeps
the welcome recoverable. Activation/reveal guards consume taps that could skip
the first picture.

The first artwork holds for 30 minutes unless manually browsed. Saved schedule
boundaries inside that hold are skipped without adding a special wake. Owner
cancellation takes effect on a subsequent online contact and resumes ordinary
artwork. Downloaded, device-reported ready and visually confirmed on the real
panel are distinct states.

Power-on reveal requires
[**v0.8.16**](https://github.com/lstepnio/Emviary-firmware/releases/tag/v0.8.16)
and gift capability 2. Basic button-reveal packages remain compatible. Publication
does not establish installation or physical acceptance. See [gift protocol and acceptance](docs/gift-mode.md).

## Updates and USB recovery

The authenticated Emviary update policy can allow newer compatible releases,
pause updates or pin a version. Eligible prereleases can be offered by that
policy. Online wake checks and downloads have byte and time bounds. Before
selecting an inactive application slot, firmware verifies the offered full
SHA-256, integral size, application project and version.

These are authenticated HTTPS and hash checks, not a secure-boot or signed-image
trust boundary. Normal OTA replaces the application only. It does not install a
bootloader or repartition storage.

Automatic OTA requires a recognized rollback-capable bootloader. New applications
confirm a pending boot after local initialization and a bounded offline storage
and memory check, without requiring Wi-Fi or a panel repaint. Older installations
may need the matching USB recovery package before OTA is allowed.

Use the **four separate files from one E1002 release**, at the addresses in its
`firmware-manifest.json`:

| Address | Release asset |
| --- | --- |
| `0x0` | `emviary-e1002-rollback-bootloader.bin` |
| `0x8000` | `emviary-e1002-partition-table.bin` |
| `0xf000` | `emviary-e1002-ota-data-initial.bin` |
| `0x20000` | `emviary-seeedstudio_reterminal_e1002.bin` |

The supported recovery package preserves the NVS and LittleFS gaps, including
settings and stored artwork. Verify `SHA256SUMS` and the manifest before writing.
Do not substitute an upstream merged image, mix releases, or erase all flash as
a routine update. Package generation checks the exact build files and layout.
[USB recovery protocol](docs/usb_recovery_protocol.md) documents the separate
recovery transport and its checkpoints.

Logging is quiet by default, including the bootloader. `debug_log_enabled` opts
into runtime console and stored debug logs. Automatic flash core dumps are
disabled in the E1002 release profile.

## Development

Clone this fork's working branch:

```bash
git clone --branch emviary-poc https://github.com/lstepnio/Emviary-firmware.git
cd Emviary-firmware
```

The [E1002 workflow](.github/workflows/emviary-e1002.yml) is the release recipe.
It uses Node.js 20, Python, CMake, libpng and zlib for preparation/host checks,
and **ESP-IDF v6.0** for the ESP32-S3 firmware build. Host tests fetch pinned
GoogleTest and cJSON dependencies independently of the SDK's managed components.

```bash
cmake -S host_tests -B host-build
cmake --build host-build -j 2
ctest --test-dir host-build --output-on-failure
python3 scripts/test_package_e1002_release.py
```

The full host suite passed **263 tests** for source revision `338ab0e` in a clean
tracked-files checkout without `managed_components`. That is software evidence,
separate from installing firmware or checking the physical frame.

With ESP-IDF activated and splash prerequisites installed, use the same board
selection as CI:

```bash
python3 build.py --board seeedstudio_reterminal_e1002 --step splash
python3 build.py --board seeedstudio_reterminal_e1002 --step firmware
```

The splash step installs its `process-cli` dependencies as needed; Python `qrcode`
and native image/font libraries are also required by the CI preparation step.
`build.py` records the generated bootloader digest and rebuilds the application
when necessary. Packaging is a separate step in the workflow.

Branch pushes build artifacts. A semantic `v*` tag or a workflow dispatch with a
`release_tag` can publish after validation; existing releases are immutable.
A README-only commit after a build does not change that build's recorded source
revision or binary. Check the release manifest for its exact commit and hashes.

Keep Wi-Fi passwords, frame bearer tokens, cloud credentials and private device
configuration out of commits and bug reports. Up to five saved 2.4 GHz networks
are supported; cloud and local changes apply through the existing wake workflow.
For internal paths and inherited tooling, see [development notes](docs/DEV.md),
[HTTP API](docs/API.md), and [process-cli](process-cli/README.md).

## Attribution and license

This fork builds on Wei-Ning Huang's
[ESP32 PhotoFrame](https://github.com/aitjcize/esp32-photoframe), the Waveshare
PhotoPainter sample and e-paper drivers, and Espressif ESP-IDF. Upstream board
support and tools retain their own history and documentation; Emviary-specific
cloud, recovery, power and gift behavior belongs to this fork.

The repository's [MIT license](LICENSE) retains the upstream copyright notice.
