"""Keep generated runtime settings compatible with the binary file format."""
import subprocess
import unittest
from pathlib import Path
from unittest.mock import patch

import check_runtime


class RuntimeFixtureTests(unittest.TestCase):
    def test_settings_keep_lf_when_host_text_output_uses_crlf(self):
        write_text = Path.write_text
        configurations = []

        def windows_write_text(path, data, encoding=None, errors=None, newline=None):
            return write_text(path, data, encoding=encoding, errors=errors,
                              newline='\r\n' if newline is None else newline)

        def run(command, **kwargs):
            config = Path(command[command.index('--config') + 1]).read_bytes()
            self.assertNotIn(b'\r', config)
            version = b'2' if not configurations else b'4'
            self.assertTrue(config.startswith(b'beachlife-launcher ' + version + b'\n'))
            self.assertIn(b'source.path "', config)
            if configurations:
                self.assertIn(b'display.fps_overlay on\n', config)
                self.assertIn(b'display.fps_overlay_corner bottom-left\n', config)
            configurations.append(config)
            if '--preview-screen' in command:
                preview = command[command.index('--preview-screen') + 1]
                if preview.endswith('missing.tga'):
                    return subprocess.CompletedProcess(command, 1, '', 'Selected screen is missing.')
                return subprocess.CompletedProcess(command, 0,
                    'Game runtime: 2 frames\n'
                    'Screen preview: Data/Screens/FrontScreen.tga, 4 x 2 pixels read through VFS.\n', '')
            return subprocess.CompletedProcess(command, 0,
                'Game data: BeachLife.ini 27 bytes read through VFS.\nGame runtime: 30 frames\n', '')

        # Emulate Windows newline translation even on Linux/macOS. Only the
        # executable is doubled; check() writes both actual fixture files.
        with patch.object(Path, 'write_text', windows_write_text), \
             patch.object(check_runtime.subprocess, 'run', side_effect=run):
            self.assertEqual(check_runtime.check('generated-runtime-test'), 0)
        self.assertEqual(len(configurations), 4)
        self.assertEqual(configurations[1:], [configurations[1]] * 3)


if __name__ == '__main__':
    unittest.main()
