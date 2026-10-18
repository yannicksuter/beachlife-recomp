"""Keep long archive-name coverage independent of Windows long-path support."""
import tarfile
import tempfile
import unittest
from pathlib import Path, PureWindowsPath

import launcher_update_fixture as fixture


class LauncherUpdateFixtureTests(unittest.TestCase):
    def test_generated_license_uses_pax_and_retains_complete_name_and_content(self):
        license_name = next(name for name in fixture.NEW_FILES if name.endswith('/LICENSE'))
        self.assertGreater(len(license_name), 100)
        body, mode = fixture.NEW_FILES[license_name]
        for platform in ('linux', 'steamdeck'):
            with self.subTest(platform=platform), tempfile.TemporaryDirectory() as temp:
                stem = f'beachlife-{fixture.NEW_VERSION}-{platform}-x86_64'
                archive = Path(temp) / 'package.tar.gz'
                fixture.write_tar(archive, stem,
                                  *fixture.payload(fixture.NEW_VERSION, platform, fixture.NEW_FILES))
                with tarfile.open(archive) as package:
                    full_name = stem + '/' + license_name
                    member = package.getmember(full_name)
                    self.assertEqual(member.pax_headers.get('path'), full_name)
                    self.assertEqual(member.mode, mode)
                    with package.extractfile(member) as stream:
                        self.assertEqual(stream.read(), body)

    def test_staging_fits_legacy_windows_paths_at_hosted_checkout_depth(self):
        # Match the Windows runner's checkout and CMake test-output layout. The
        # updater does not yet promise extended-length Windows filesystem paths.
        root = PureWindowsPath(
            r'D:\a\beachlife-recomp\beachlife-recomp\build\launcher-update-tests\work')
        cases = [('unpack-linux', 'linux', ''), ('install', 'linux', '.update/staged-1'),
                 ('rollback', 'linux', '.update/staged-1'),
                 ('interrupted', 'linux', '.update/staged-1'),
                 ('mismatch', 'steamdeck', '.update/staged-1')]
        for directory, platform, staged in cases:
            stem = f'beachlife-{fixture.NEW_VERSION}-{platform}-x86_64'
            for name in fixture.NEW_FILES:
                path = root / directory / staged / stem / name
                with self.subTest(directory=directory, name=name):
                    self.assertLess(len(str(path)), 260)
                    self.assertLess(len(str(path.parent)), 248)


if __name__ == '__main__':
    unittest.main()
