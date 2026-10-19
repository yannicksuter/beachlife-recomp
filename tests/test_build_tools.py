import io
import json
import struct
import subprocess
import sys
import tarfile
import tempfile
import unittest
import zipfile
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import build_version
import bump_version
import format_sources
import package_build as packages
import release
from project_version import read_version


class VersionTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        (self.root / 'CMakeLists.txt').write_text('project(beachlife_recomp VERSION 0.1.0 LANGUAGES C CXX)\n# retain me\n')

    def test_bumps_and_rejections(self):
        for request, expected in [('patch', '0.1.1'), ('minor', '0.2.0'), ('major', '1.0.0'), ('2.3.4', '2.3.4')]:
            bump_version.bump(self.root, request)
            self.assertEqual(read_version(self.root), expected)
        for request in ('2.3.4', '1.0.0', '2.03.5', '3.0.0-rc.1', 'nonsense'):
            with self.assertRaises(ValueError):
                bump_version.bump(self.root, request)
        self.assertIn('# retain me', (self.root / 'CMakeLists.txt').read_text())

    def test_build_identity_refreshes_after_commit_and_edits(self):
        def git(*args):
            return subprocess.check_output(['git', '-C', str(self.root), *args], text=True, stderr=subprocess.DEVNULL).strip()
        git('init', '-q')
        git('add', 'CMakeLists.txt')
        git('-c', 'user.name=Yannick Suter', '-c', 'user.email=yannick.suter@gmail.com', 'commit', '-qm', 'test: create fixture')
        config = {'project_version': '0.1.0', 'channel': 'dev', 'pull_request': '0', 'run_id': '0'}
        first = build_version.identity(self.root, config)
        self.assertFalse(first['dirty'])
        self.assertEqual(first['decomp_profile'], 'native')
        (self.root / 'note.txt').write_text('edit')
        self.assertTrue(build_version.identity(self.root, config)['dirty'])
        git('add', 'note.txt')
        git('-c', 'user.name=Yannick Suter', '-c', 'user.email=yannick.suter@gmail.com', 'commit', '-qm', 'test: update fixture')
        second = build_version.identity(self.root, config)
        self.assertNotEqual(first['build_version'], second['build_version'])
        self.assertFalse(second['dirty'])
        with self.assertRaises(ValueError):
            build_version.identity(self.root, {**config, 'channel': 'test'})

    def test_experimental_identity_cannot_be_a_release(self):
        config = {'project_version': '0.1.0', 'channel': 'dev', 'pull_request': '0',
                  'run_id': '0', 'decomp_profile': 'native-experimental'}
        with patch.object(build_version, 'git_output', side_effect=['a' * 40, '', '1']):
            self.assertEqual(build_version.identity(self.root, config)['decomp_profile'],
                             'native-experimental')
        with self.assertRaisesRegex(ValueError, 'requires a dev or test build'):
            build_version.identity(self.root, {**config, 'channel': 'release'})
        for profile in ('matching', 'unknown', None):
            with self.subTest(profile=profile), self.assertRaisesRegex(ValueError, 'Unknown native'):
                build_version.identity(self.root, {**config, 'decomp_profile': profile})


def fixture_archive(root, target, sha='a' * 40, channel='test', extra=None, corrupt=False,
                    profile='native'):
    platform, architecture = target.rsplit('-', 1)
    system = 'linux' if platform == 'steamdeck' else platform
    version = '0.1.0+g' + sha[:12]
    info = {'project_version': '0.1.0', 'build_version': version, 'commit': sha, 'dirty': False,
            'channel': channel, 'run_id': 7, 'platform': platform, 'system': system, 'architecture': architecture,
            'capability': 'launcher', 'decomp_profile': profile}
    data = {name: b'fixture' for name in packages.payload_names(info)}
    manifest = {'format': 1, 'build': info, 'files': {
        name: {'sha256': packages.digest(body), 'bytes': len(body), 'mode': 0o644} for name, body in data.items()}}
    data['SOURCE-MANIFEST.json'] = json.dumps(manifest).encode()
    if extra:
        data[extra] = b'unexpected'
    if corrupt:
        data['README.md'] = b'changed'
    stem = f'beachlife-{version}-{target}'
    path = root / (stem + ('.zip' if system == 'windows' else '.tar.gz'))
    if system == 'windows':
        with zipfile.ZipFile(path, 'w') as archive:
            for name, body in data.items():
                archive.writestr(name, body)
    else:
        with tarfile.open(path, 'w:gz') as archive:
            for name, body in data.items():
                entry = tarfile.TarInfo(stem + '/' + name)
                entry.size, entry.mode = len(body), 0o644
                archive.addfile(entry, io.BytesIO(body))
    return path


class PackageTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def test_complete_matrix_and_wrong_provenance(self):
        for target in packages.TARGETS:
            fixture_archive(self.root, target)
        self.assertEqual(len(packages.verify_set(self.root, '0.1.0', 'a' * 40, 'test', 7)), 6)
        for version, sha, channel, run in [('0.2.0', 'a' * 40, 'test', 7), ('0.1.0', 'b' * 40, 'test', 7),
                                          ('0.1.0', 'a' * 40, 'release', 7), ('0.1.0', 'a' * 40, 'test', 8)]:
            with self.assertRaises(ValueError):
                packages.verify_set(self.root, version, sha, channel, run)
        next(self.root.glob('*.zip')).unlink()
        with self.assertRaises(ValueError):
            packages.verify_set(self.root, '0.1.0', 'a' * 40, 'test', 7)

    def test_archive_rejects_extra_corrupt_and_traversal_members(self):
        for target in ('windows-x86_64', 'linux-x86_64'):
            for extra, corrupt in [('unrelated.dat', False), ('../escape', False), (None, True)]:
                path = fixture_archive(self.root, target, extra=extra, corrupt=corrupt)
                with self.assertRaises(ValueError):
                    packages.inspect_package(path)

    def test_archive_rejects_experimental_release_and_mixed_profiles(self):
        for target in packages.TARGETS:
            fixture_archive(self.root, target)
        fixture_archive(self.root, 'linux-x86_64', profile='native-experimental')
        with self.assertRaisesRegex(ValueError, 'source profiles differ'):
            packages.verify_set(self.root, '0.1.0', 'a' * 40, 'test', 7)
        archive = fixture_archive(self.root, 'linux-x86_64', channel='release',
                                  profile='native-experimental')
        with self.assertRaisesRegex(ValueError, 'requires a dev or test build'):
            packages.inspect_package(archive)

    def test_symlink_input_is_rejected(self):
        (self.root / 'real').write_text('data')
        try:
            (self.root / 'link').symlink_to(self.root / 'real')
        except OSError:
            self.skipTest('Symlinks unavailable on this runner')
        with self.assertRaises(ValueError):
            packages.regular_file(self.root, 'link')

    def test_native_architecture_validation(self):
        elf = bytearray(64)
        elf[:6] = b'\x7fELF\x02\x01'
        struct.pack_into('<H', elf, 18, 62)
        packages.check_binary(elf, 'linux', 'x86_64')
        struct.pack_into('<H', elf, 18, 183)
        with self.assertRaises(ValueError):
            packages.check_binary(elf, 'linux', 'x86_64')
        with self.assertRaises(ValueError):
            packages.check_binary(b'truncated', 'windows', 'x86_64')

    def test_steam_runtime_limits(self):
        report = '(NEEDED) Shared library: [libc.so.6]\nName: GLIBC_2.39\n'
        packages.check_steam_abi(report)
        for changed in (report.replace('2.39', '2.41'), report.replace('libc.so.6', 'unknown.so')):
            with self.assertRaises(ValueError):
                packages.check_steam_abi(changed)


class ReleaseTests(unittest.TestCase):
    def test_cleanup_only_selects_exact_pr_test_tags(self):
        tags = ['v0.1.0', 'test-pr1-aaaaaaaaaaaa', 'test-pr10-aaaaaaaaaaaa', 'test-pr1-bad', 'v0.2.0-rc.1']
        self.assertEqual(release.test_tags([{'tag_name': tag} for tag in tags], 1), ['test-pr1-aaaaaaaaaaaa'])

    @patch.object(release, 'command')
    @patch.object(release, 'verify_set', return_value=[])
    @patch.object(release, 'read_version', return_value='0.1.0')
    def test_existing_candidate_is_preserved(self, version, verify, command):
        pages = [[{'tag_name': 'v0.0.1'}], [{'tag_name': 'v0.1.0', 'draft': True}]]
        with patch.object(release, 'api', return_value=pages) as api:
            release.publish('owner/repo', 'stable', Path('.'), 'a' * 40, 7)
        api.assert_called_once_with('repos/owner/repo/releases?per_page=100', paginate=True)
        command.assert_not_called()

    @patch.object(release, 'command')
    @patch.object(release, 'verify_set', return_value=[])
    @patch.object(release, 'read_version', return_value='0.1.0')
    def test_existing_tag_without_release_is_preserved(self, version, verify, command):
        with patch.object(release, 'api', side_effect=[[], {'ref': 'refs/tags/v0.1.0'}]):
            release.publish('owner/repo', 'stable', Path('.'), 'a' * 40, 7)
        command.assert_not_called()

    @patch.object(release, 'command')
    @patch.object(release, 'verify_set', return_value=[])
    @patch.object(release, 'read_version', return_value='0.1.0')
    def test_closed_or_superseded_pr_never_publishes(self, version, verify, command):
        with patch.object(release, 'current_pr', return_value=False):
            release.publish('owner/repo', 'test', Path('.'), 'a' * 40, 7, 1)
        command.assert_not_called()

    @patch.object(release, 'verify_set', return_value=[])
    @patch.object(release, 'read_version', return_value='0.1.0')
    def test_candidate_is_draft_at_exact_commit(self, version, verify):
        with patch.object(release, 'api', side_effect=[[], None]), patch.object(release, 'command') as command:
            release.publish('owner/repo', 'stable', Path('.'), 'a' * 40, 7)
        args = command.call_args.args
        self.assertIn('--draft', args)
        self.assertEqual(args[args.index('--target') + 1], 'a' * 40)


class FormatTests(unittest.TestCase):
    def setUp(self):
        try:
            self.executable = format_sources.formatter(format_sources.default_formatter(), format_sources.pinned_version())
        except (OSError, ValueError) as error:
            self.skipTest(str(error))
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        subprocess.run(['git', 'init', '-q', str(self.root)], check=True)
        for config in ('.clang-format', 'decomp/.clang-format'):
            (self.root / config).parent.mkdir(exist_ok=True)
            (self.root / config).write_bytes((format_sources.ROOT / config).read_bytes())
        (self.root / '.gitignore').write_text('ignored.cpp\n')
        self.untidy = untidy = '#include <b.h>\n#include <a.h>\nstd::vector<std::vector<int> > table;\nint f(){return 1;}\n'
        for name in ('src/game.cpp', 'decomp/src/loader.cpp', 'extern/dep/lib.cpp', 'orig/dump.cpp', 'ignored.cpp'):
            (self.root / name).parent.mkdir(parents=True, exist_ok=True)
            (self.root / name).write_text(untidy)

    def test_formats_only_project_sources_with_their_own_style(self):
        names = format_sources.sources(self.root)
        self.assertEqual(names, ['decomp/src/loader.cpp', 'src/game.cpp'])
        with patch('sys.stdout', new=io.StringIO()) as diff:
            self.assertEqual(format_sources.run(self.executable, self.root, names, check=True), names)
            self.assertIn('+int f()\n', diff.getvalue())
            self.assertIn('int f(){', (self.root / 'src/game.cpp').read_text())
            format_sources.run(self.executable, self.root, names, check=False)
            self.assertEqual(format_sources.run(self.executable, self.root, names, check=True), [])
        native = (self.root / 'src/game.cpp').read_text()
        self.assertLess(native.index('<a.h>'), native.index('<b.h>'))
        self.assertIn('<std::vector<int>>', native)
        # The VC6 sources keep their include order and C++98 template closers.
        matching = (self.root / 'decomp/src/loader.cpp').read_text()
        self.assertLess(matching.index('<b.h>'), matching.index('<a.h>'))
        self.assertIn('<std::vector<int> >', matching)
        for name in ('extern/dep/lib.cpp', 'orig/dump.cpp', 'ignored.cpp'):
            self.assertEqual((self.root / name).read_text(), self.untidy)


if __name__ == '__main__':
    unittest.main()
