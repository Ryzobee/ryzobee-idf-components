#!/usr/bin/env python3
"""Exercise the actual CMake selector without configuring an ESP-IDF project."""
from pathlib import Path
import subprocess
import tempfile
import unittest

COMPONENT = Path(__file__).resolve().parents[2]
SELECTOR = COMPONENT / 'cmake/select_protocol.cmake'


class ProtocolSelection(unittest.TestCase):
    def select(self, major, minor=0, patch=0, target='esp32s3', component=COMPONENT):
        with tempfile.TemporaryDirectory(prefix='ci1302-selector-') as directory:
            script = Path(directory)/'select.cmake'
            script.write_text(f'''set(IDF_TARGET "{target}")
set(IDF_VERSION_MAJOR {major})
set(IDF_VERSION_MINOR {minor})
set(IDF_VERSION_PATCH {patch})
include("{SELECTOR}")
gmf_ci1302_select_protocol("{component}" selected built_with)
message("SELECTED=${{selected}};BUILT_WITH=${{built_with}}")
''')
            return subprocess.run(['cmake', '-P', str(script)], capture_output=True, text=True)

    def test_major_version_mapping(self):
        for version in [(5, 5, 1), (5, 5, 2), (6, 0, 2), (6, 2, 0), (6, 3, 0)]:
            with self.subTest(version=version):
                result = self.select(*version)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertIn(f'/idf{version[0]}/libci1302_protocol.a', result.stderr)
                self.assertIn('BUILT_WITH='+('5.5.1' if version[0] == 5 else '6.2.0'), result.stderr)

    def test_unsupported_major(self):
        for major in [4, 7]:
            result = self.select(major)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('major version 5 or 6', result.stderr)

    def test_unsupported_target(self):
        result = self.select(6, 2, target='esp32')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('ESP32-S3 only', result.stderr)

    def test_missing_variant_does_not_fall_back(self):
        with tempfile.TemporaryDirectory(prefix='ci1302-missing-') as directory:
            other = Path(directory)/'ci1302_protocol/lib/esp32s3/idf6/libci1302_protocol.a'
            other.parent.mkdir(parents=True)
            other.touch()
            result = self.select(5, 5, 1, component=Path(directory))
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('idf5 archive is missing', result.stderr)


if __name__ == '__main__':
    unittest.main()
