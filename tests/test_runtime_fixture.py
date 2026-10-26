"""Check generated runtime inputs and preserve explicit process-result gates."""
import hashlib
import struct
import subprocess
import tempfile
import unittest
import zlib
from pathlib import Path
from unittest.mock import patch

import check_runtime
import check_startup_configuration
from content_fixture import DESCRIPTOR, RECORDS, TABLE, content_files


class RuntimeFixtureTests(unittest.TestCase):
    def runtime_result(self, command, configurations, *, fixture_status=0):
        if '--write-configuration' in command:
            Path(command[-1]).write_bytes(b'generated fixture'.ljust(1242, b'\r'))
            return subprocess.CompletedProcess(command, 0, 'GENERATED_CONFIGURATION bytes=1242\n', '')
        config = Path(command[command.index('--config') + 1]).read_bytes()
        self.assertNotIn(b'\r', config)
        self.assertIn(b'source.path "', config)
        configurations.append((Path(command[0]).name, config, command))
        if '--preview-screen' in command:
            preview = command[command.index('--preview-screen') + 1]
            if preview.endswith('missing.tga'):
                return subprocess.CompletedProcess(command, 1, '', 'Selected screen is missing.')
            return subprocess.CompletedProcess(command, 0,
                'Game runtime: 2 frames\n'
                'Screen preview: Data/Screens/FrontScreen.tga, 4 x 2 pixels read through VFS.\n', '')
        if Path(command[0]).name == 'generated-runtime-test':
            return subprocess.CompletedProcess(command, fixture_status,
                'Game data: BeachLife.ini 27 bytes read through VFS.\n'
                'Game configuration: loaded through VFS.\nGame runtime: 30 frames\n' +
                check_runtime.CONFIGURATION_COMPLETED + '\n', '')
        return subprocess.CompletedProcess(command, 1,
            'Game data: BeachLife.ini 27 bytes read through VFS.\n',
            'The selected game configuration is not supported.')

    def test_package_settings_keep_lf_on_windows_and_cover_production_rejection(self):
        write_text = Path.write_text
        configurations = []

        def windows_write_text(path, data, encoding=None, errors=None, newline=None):
            return write_text(path, data, encoding=encoding, errors=errors,
                              newline='\r\n' if newline is None else newline)

        with patch.object(Path, 'write_text', windows_write_text), \
             patch.object(check_runtime.subprocess, 'run',
                          side_effect=lambda command, **kw: self.runtime_result(command, configurations)):
            self.assertEqual(check_runtime.check('production-test'), 0)
        self.assertEqual(len(configurations), 4)
        self.assertTrue(configurations[0][1].startswith(b'beachlife-launcher 2\n'))
        self.assertEqual([item[1] for item in configurations[1:]], [configurations[1][1]] * 3)
        self.assertIn(b'display.fps_overlay on\n', configurations[1][1])
        self.assertIn(b'display.fps_overlay_corner bottom-left\n', configurations[1][1])
        self.assertTrue(all(item[0] == 'production-test' for item in configurations))

    def test_generated_normal_play_uses_separate_executable_and_resources(self):
        configurations = []
        with patch.object(check_runtime.subprocess, 'run',
                          side_effect=lambda command, **kw: self.runtime_result(command, configurations)):
            self.assertEqual(check_runtime.check('production-test', 'generated-runtime-test', 'fixture-fonts'), 0)
        self.assertEqual(len(configurations), 6)
        self.assertTrue(all(item[0] == 'production-test' for item in configurations[:4]))
        for _, _, command in configurations[4:]:
            self.assertEqual(Path(command[0]).name, 'generated-runtime-test')
            self.assertNotIn('--preview-screen', command)
            self.assertEqual(command[command.index('--resources') + 1], str(Path('fixture-fonts').resolve()))
        self.assertTrue(configurations[4][1].startswith(b'beachlife-launcher 2\n'))
        self.assertIn(b'display.fps_overlay on\n', configurations[5][1])

    def test_runtime_completion_marker_cannot_mask_nonzero_exit(self):
        with patch.object(check_runtime.subprocess, 'run',
                          side_effect=lambda command, **kw: self.runtime_result(command, [], fixture_status=9)):
            with self.assertRaises(AssertionError):
                check_runtime.check('production-test', 'generated-runtime-test')

    def test_headless_completion_marker_cannot_mask_nonzero_exit(self):
        result = subprocess.CompletedProcess([], 9, 'STARTUP_CONFIGURATION_SUCCESS checks=238\n', '')
        with patch.object(check_startup_configuration.subprocess, 'run', return_value=result):
            with self.assertRaises(AssertionError):
                check_startup_configuration.check('injected-test', 'default-test')

    def test_headless_crash_does_not_count_as_clean_allocation_abort(self):
        def run(command, **kwargs):
            if command[-1] == 'generated':
                output = 'STARTUP_CONFIGURATION_SUCCESS checks=1\n'
                return subprocess.CompletedProcess(command, 0, output, '')
            if command[-2:] == ['fault', '0']:
                output = ('ALLOCATION 1 8 0 0\nALLOCATION 2 8 0 0\n'
                          'ALLOCATION 3 32 0 1\nALLOCATION 4 160 1 1\n'
                          'ALLOCATION_BASELINE count=4 owner_bytes=8 token_bytes=160\n')
                return subprocess.CompletedProcess(command, 0, output, '')
            if command[-2:] == ['fault', '1']:
                return subprocess.CompletedProcess(command, 0, 'RECOVERED 1\n', 'INJECTED allocation=1\n')
            return subprocess.CompletedProcess(command, -11, '', 'INJECTED allocation=2\n')
        with patch.object(check_startup_configuration.subprocess, 'run', side_effect=run):
            with self.assertRaises(AssertionError):
                check_startup_configuration.check('injected-test', 'default-test')

    def test_production_never_accepts_generated_normal_play(self):
        result = subprocess.CompletedProcess([], 0,
            'Game configuration: loaded through VFS.\nGame runtime: 1 frames\n', '')
        with patch.object(check_runtime.subprocess, 'run', return_value=result):
            with self.assertRaises(AssertionError):
                check_runtime.check('production-test')

    def test_boot_parser_rejection_requires_retail_reads_and_clean_early_exit(self):
        reads = check_runtime.BOOT_CONFIGURATION_READ * 3
        rejected = subprocess.CompletedProcess([], 0, '', reads)
        check_runtime.rejected_configuration(rejected, boot=True)
        with self.assertRaises(AssertionError):
            check_runtime.rejected_configuration(rejected, boot=False)
        for status, output, errors in [
            (0, '', ''),
            (1, '', reads),
            (-11, '', reads),
            (0, 'Game runtime: 1 frames\n', reads),
            (0, check_runtime.CONFIGURATION_COMPLETED, reads),
            (0, '', reads + 'Frame 0: opened beachlife.ini\n'),
            (0, '', reads + 'Beach Life: The selected executable is not a supported credits-data source.\n'),
        ]:
            with self.subTest(status=status, output=output, errors=errors), self.assertRaises(AssertionError):
                check_runtime.rejected_configuration(
                    subprocess.CompletedProcess([], status, output, errors), boot=True)

    def test_boot_production_input_and_profile_are_separate_from_host_fixture(self):
        host = b'host fixture'.ljust(1242, b'\r')
        self.assertEqual(check_runtime.production_configuration(host, False), host)
        retail = check_runtime.production_configuration(host, True)
        self.assertEqual(len(retail), 1242)
        self.assertTrue(retail.startswith(b"'bFixture' 123\r\n"))
        self.assertNotEqual(retail, host)
        with tempfile.TemporaryDirectory() as directory:
            profile = Path(directory)/'profile'
            environment = check_runtime.production_environment(True, profile)
            self.assertEqual(environment['BEACHLIFE_BOOT_USER_DIR'], str(profile))
            self.assertEqual(environment['BEACHLIFE_BOOT_TRACE_FILES'], '1')

    def test_unavailable_gpu_precedes_cache_creation(self):
        def run(command, **kwargs):
            return subprocess.CompletedProcess(command, 1, '', 'Cannot start the game: no GPU')
        with patch.object(check_runtime.subprocess, 'run', side_effect=run):
            self.assertEqual(check_runtime.check('production-test'), 77)

    def test_configuration_writer_requires_marker_and_exact_length(self):
        for marker, count in [(False, 1242), (True, 12)]:
            with self.subTest(marker=marker, count=count), tempfile.TemporaryDirectory() as directory:
                def run(command, **kwargs):
                    Path(command[-1]).write_bytes(b'x' * count)
                    return subprocess.CompletedProcess(command, 0,
                        'GENERATED_CONFIGURATION bytes=1242\n' if marker else '', '')
                with patch.object(check_runtime.subprocess, 'run', side_effect=run):
                    with self.assertRaises(AssertionError):
                        check_runtime.generated_configuration('generated-runtime-test', Path(directory))

    def test_generated_configuration_has_own_compressed_cabinet_entry(self):
        configuration = b'generated configuration'.ljust(1242, b'\r')
        files, expected = content_files(configuration=configuration)
        header, cabinet = files['data1.hdr'], files['data2.cab']
        self.assertEqual(expected['Data/Script/GameParameters.pis'], configuration)
        self.assertEqual(expected['BeachLife.exe'], b'test')
        self.assertEqual(files['BeachLife.exe'], b'test')
        self.assertNotIn('Setup.exe', expected)
        self.assertEqual(len(expected), 7)
        self.assertEqual(struct.unpack_from('<I', header, DESCRIPTOR + 0x28)[0], 8)
        # The compressed group includes the new entry; loose/support ranges move
        # without changing their ownership. Decode its independent raw-deflate payload.
        for group, bounds in [(1, (1, 4)), (2, (5, 6)), (3, (7, 7))]:
            offset = DESCRIPTOR + 0x280 + group * 0x40 + 12 + 0x16
            self.assertEqual(struct.unpack_from('<2I', header, offset), bounds)
        entry = RECORDS + 4 * 0x57
        flags, size, stored, offset = struct.unpack_from('<HQQQ', header, entry)
        self.assertEqual((flags, size), (4, 1242))
        count = struct.unpack_from('<H', cabinet, offset)[0]
        self.assertEqual(stored, count + 2)
        self.assertEqual(zlib.decompress(cabinet[offset+2:offset+2+count], wbits=-15), configuration)
        self.assertEqual(bytes(header[entry+26:entry+42]), hashlib.md5(configuration).digest())
        name = TABLE + struct.unpack_from('<I', header, entry + 0x3a)[0]
        self.assertEqual(bytes(header[name:header.index(0, name)]), b'GameParameters.pis')


if __name__ == '__main__':
    unittest.main()
