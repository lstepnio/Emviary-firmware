#!/usr/bin/env python3
"""Package separate E1002 recovery writes from the exact OTA build."""
import argparse
import hashlib
import json
import shutil
from pathlib import Path


def package(build, output, version, commit, sdkconfig):
    config = sdkconfig.read_text()
    required = ('CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y',
                'CONFIG_LOG_DEFAULT_LEVEL_NONE=y',
                'CONFIG_BOOTLOADER_LOG_LEVEL_NONE=y',
                'CONFIG_ESP_COREDUMP_ENABLE_TO_NONE=y')
    if any(line not in config.splitlines() for line in required):
        raise ValueError('E1002 release requires rollback and quiet logging configuration')
    args = json.loads((build / 'flasher_args.json').read_text())
    if args['extra_esptool_args']['chip'] != 'esp32s3' or args['flash_settings']['flash_size'] != '32MB':
        raise ValueError('Recovery requires ESP32-S3 with 32MB flash')
    files = (
        ('0x0', 'bootloader/bootloader.bin', 'emviary-e1002-rollback-bootloader.bin', 0x8000),
        ('0x8000', 'partition_table/partition-table.bin', 'emviary-e1002-partition-table.bin', 0x1000),
        ('0xf000', 'ota_data_initial.bin', 'emviary-e1002-ota-data-initial.bin', 0x2000),
        ('0x20000', 'esp32-photoframe.bin', 'emviary-seeedstudio_reterminal_e1002.bin', 0x380000),
    )
    actual_files = {int(k, 0): v for k, v in args['flash_files'].items()}
    if actual_files != {int(offset, 0): source for offset, source, _, _ in files}:
        raise ValueError('Unexpected recovery flash files or addresses')
    output.mkdir(parents=True, exist_ok=True)
    entries = []
    for offset, source, asset, limit in files:
        data = (build / source).read_bytes()
        if not data or len(data) > limit:
            raise ValueError(f'Invalid size for {source}')
        if source == 'ota_data_initial.bin' and (len(data) != 0x2000 or any(b != 0xff for b in data)):
            raise ValueError('Initial OTA data must be the exact blank 8KiB build artifact')
        shutil.copy2(build / source, output / asset)
        entries.append({'address': int(offset, 0), 'offset': offset, 'asset': asset,
                        'sha256': hashlib.sha256(data).hexdigest(), 'size': len(data)})
    boot, _, _, app = entries
    manifest = {'version': version, 'board': 'seeedstudio_reterminal_e1002',
                'commit': commit, 'asset': app['asset'], 'sha256': app['sha256'], 'size': app['size'],
                'rollback_required': True, 'bootloader_asset': boot['asset'],
                'bootloader_sha256': boot['sha256'], 'bootloader_size': boot['size'],
                'recovery': {'chip': 'esp32s3', 'flash_size': '32MB',
                             'flash_settings': args['flash_settings'], 'files': entries,
                             'preserves_nvs': True, 'preserves_littlefs': True}}
    (output / 'firmware-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    (output / 'SHA256SUMS').write_text(''.join(f"{e['sha256']}  {e['asset']}\n" for e in entries))
    (output / 'requires-rollback-bootloader.txt').write_text(
        'Install the matching rollback bootloader by USB before OTA.\n'
        'USB recovery writes four separate files at the manifest addresses.\n'
        'Do not erase all flash or merge images: retain the NVS and LittleFS gaps.\n')
    return manifest


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, default=Path('build'))
    parser.add_argument('--output', type=Path, default=Path('release'))
    parser.add_argument('--sdkconfig', type=Path, default=Path('sdkconfig'))
    parser.add_argument('--version', required=True)
    parser.add_argument('--commit', required=True)
    options = parser.parse_args()
    package(options.build, options.output, options.version, options.commit, options.sdkconfig)
