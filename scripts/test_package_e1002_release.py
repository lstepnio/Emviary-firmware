"""Recovery packaging must preserve credential/storage gaps and use build bytes."""
import hashlib
import json
import tempfile
import unittest
from pathlib import Path

from package_e1002_release import package


class RecoveryPackaging(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.build = self.root / 'build'
        self.output = self.root / 'release'
        self.build.mkdir()
        self.config = self.root / 'sdkconfig'
        self.config.write_text('\n'.join((
            'CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y', 'CONFIG_LOG_DEFAULT_LEVEL_NONE=y',
            'CONFIG_BOOTLOADER_LOG_LEVEL_NONE=y', 'CONFIG_ESP_COREDUMP_ENABLE_TO_NONE=y')))
        self.files = {'0x0': 'bootloader/bootloader.bin',
                      '0x8000': 'partition_table/partition-table.bin',
                      '0xf000': 'ota_data_initial.bin', '0x20000': 'esp32-photoframe.bin'}
        for source in self.files.values():
            p = self.build / source
            p.parent.mkdir(exist_ok=True)
            p.write_bytes(b'\xff' * 0x2000 if 'ota_data' in source else b'firmware' * 128)
        self.args = {'flash_files': self.files, 'extra_esptool_args': {'chip': 'esp32s3'},
                     'flash_settings': {'flash_size': '32MB', 'flash_mode': 'qio', 'flash_freq': '80m'}}
        self.write_args()

    def write_args(self):
        (self.build / 'flasher_args.json').write_text(json.dumps(self.args))

    def run_package(self):
        return package(self.build, self.output, 'v0.8.2', 'abc123', self.config)

    def test_separate_files_have_exact_build_bytes_addresses_and_hashes(self):
        manifest = self.run_package()
        entries = manifest['recovery']['files']
        self.assertEqual([e['address'] for e in entries], [0, 0x8000, 0xf000, 0x20000])
        for source, entry in zip(self.files.values(), entries):
            data = (self.output / entry['asset']).read_bytes()
            self.assertEqual(data, (self.build / source).read_bytes())
            self.assertEqual(entry['sha256'], hashlib.sha256(data).hexdigest())
            self.assertEqual(entry['size'], len(data))
            self.assertFalse(entry['address'] < 0xf000 and entry['address'] + len(data) > 0x9000)
            self.assertLessEqual(entry['address'] + len(data), 0x720000)
        self.assertEqual(manifest['sha256'], entries[-1]['sha256'])
        self.assertTrue(manifest['recovery']['preserves_nvs'])
        self.assertTrue(manifest['recovery']['preserves_littlefs'])

    def test_rejects_extra_nvs_write(self):
        self.files['0x9000'] = 'nvs.bin'
        self.write_args()
        with self.assertRaises(ValueError):
            self.run_package()

    def test_rejects_nonblank_ota_data(self):
        (self.build / 'ota_data_initial.bin').write_bytes(b'\0' * 0x2000)
        with self.assertRaises(ValueError):
            self.run_package()

    def test_rejects_wrong_chip_and_flash(self):
        for key, value in (('chip', 'esp32'), ('flash_size', '16MB')):
            section = 'extra_esptool_args' if key == 'chip' else 'flash_settings'
            original = self.args[section][key]
            self.args[section][key] = value
            self.write_args()
            with self.assertRaises(ValueError):
                self.run_package()
            self.args[section][key] = original

    def test_rejects_oversized_bootloader_that_could_touch_partition_table(self):
        (self.build / 'bootloader/bootloader.bin').write_bytes(b'\0' * 0x8001)
        with self.assertRaises(ValueError):
            self.run_package()

    def test_rejects_nonquiet_config(self):
        self.config.write_text('CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y\n')
        with self.assertRaises(ValueError):
            self.run_package()


if __name__ == '__main__':
    unittest.main()
