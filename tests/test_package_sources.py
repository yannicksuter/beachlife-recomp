"""Exercise package provenance with generated Git pins and real prepared exports."""
import json
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import package_build as packages
from prepare_sources import DEPENDENCIES, prepare


class PackageSourceTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.repo, self.build = self.root / 'repo', self.root / 'build'
        self.output = self.root / 'packages'
        self.repo.mkdir()
        self.git(self.repo, 'init', '-q')
        self.git(self.repo, 'config', 'core.autocrlf', 'false')
        notices = {'sdl': ['LICENSE.txt'], 'imgui': ['LICENSE.txt'], 'stb': ['LICENSE'],
                   'freetype': ['docs/FTL.TXT', 'src/bdf/README', 'src/pcf/README'],
                   'dr-libs': ['LICENSE']}
        for name in DEPENDENCIES:
            checkout = self.repo / 'extern' / name
            checkout.mkdir(parents=True)
            self.git(checkout, 'init', '-q')
            self.git(checkout, 'config', 'core.autocrlf', 'false')
            for filename in ['fixture.txt', *notices.get(name, [])]:
                self.write(checkout / filename, b'generated dependency fixture\n')
            pin = self.commit(checkout)
            self.git(self.repo, 'update-index', '--add', '--cacheinfo',
                     '160000,' + pin + ',extern/' + name)
        for name in (*packages.DOCUMENTS, *packages.LAUNCHER_NOTICES,
                     *packages.GAME_NOTICES, 'assets/app/README.md'):
            self.write(self.repo / name, b'generated package documentation\n')
        self.write(self.repo / 'CMakeLists.txt',
                   b'project(beachlife_recomp VERSION 0.0.1 LANGUAGES C CXX)\n')
        self.write(self.repo / 'decomp/src/value.cpp', b'base\n')
        self.revision = self.commit(self.repo)
        self.patch_root = self.repo / 'patches/beachlife-decomp'
        for name, before, after in [('common.patch', 'base', 'reconstructed'),
                                    ('native.patch', 'reconstructed', 'native')]:
            patch = ('diff --git a/src/value.cpp b/src/value.cpp\n'
                     '--- a/src/value.cpp\n+++ b/src/value.cpp\n'
                     f'@@ -1 +1 @@\n-{before}\n+{after}\n')
            self.write(self.patch_root / name, patch.encode())
        self.write(self.patch_root / 'reconstruction-series', b'common.patch\n')
        self.write(self.patch_root / 'series', b'native.patch\n')
        self.write(self.patch_root / 'native-pre-series', b'# no early adaptations\n')
        self.pin_path = self.patch_root / 'source.json'
        self.write(self.pin_path, json.dumps({'revision': self.revision}).encode())
        self.commit(self.repo)
        self.prepared = self.build / 'prepared'
        self.identities = prepare(self.repo, self.prepared, decomp_revision=self.revision)
        self.stamp = self.prepared / 'beachlife-decomp.json'
        self.export = self.prepared / 'beachlife-decomp'
        for name in packages.LAUNCHER_RESOURCES:
            self.write(self.build / 'assets/launcher' / name, b'generated asset fixture')
        self.write(self.build / 'assets/app/icon.png', b'generated icon fixture')
        # Only the static header is packaged. This generated file is never executed.
        binary = bytearray(64)
        binary[:6] = b'\x7fELF\x02\x01'
        struct.pack_into('<H', binary, 18, 62)
        self.write(self.build / 'beachlife', binary + b'0.0.1+fixture')
        (self.build / 'beachlife').chmod(0o755)
        self.refresh_build_identity()

    @staticmethod
    def git(root, *args):
        return subprocess.check_output(['git', '-C', str(root), *args], text=True,
                                       stderr=subprocess.DEVNULL).strip()

    @staticmethod
    def write(path, body):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(body)

    def commit(self, root):
        self.git(root, 'add', '--all')
        self.git(root, '-c', 'user.name=Yannick Suter',
                 '-c', 'user.email=yannick.suter@gmail.com',
                 'commit', '-qm', 'test: update generated package fixture')
        return self.git(root, 'rev-parse', 'HEAD')

    def refresh_build_identity(self, profile='native'):
        self.info = {'project_version': '0.0.1', 'build_version': '0.0.1+fixture',
                     'commit': self.git(self.repo, 'rev-parse', 'HEAD'), 'commit_time': 1,
                     'dirty': bool(self.git(self.repo, 'status', '--porcelain=v1',
                                           '--untracked-files=normal')),
                     'channel': 'dev', 'platform': 'linux', 'system': 'linux',
                     'architecture': 'x86_64', 'capability': 'launcher',
                     'decomp_profile': profile}
        self.write(self.build / 'generated/build-info.json', json.dumps(self.info).encode())

    def assert_rejected(self, message):
        self.refresh_build_identity()
        with self.assertRaisesRegex(ValueError, message):
            packages.package(self.build, self.output, source=self.repo)
        self.assertFalse(self.output.exists(), 'A rejected export must not create a package')

    @unittest.skipIf(os.name == 'nt', 'Synthetic ELF packaging requires POSIX executable permissions')
    def test_manifest_includes_verified_native_export_and_both_patch_series(self):
        before = {p.relative_to(self.prepared).as_posix(): (p.read_bytes(), p.stat().st_mtime_ns)
                  for p in self.prepared.rglob('*') if p.is_file()}
        archive = packages.package(self.build, self.output, source=self.repo)
        manifest, payload = packages.inspect_package(archive)
        self.assertEqual(manifest['dependencies'], self.identities)
        self.assertEqual([row['name'] for row in manifest['dependencies']],
                         [*DEPENDENCIES, 'beachlife-decomp'])
        decomp = manifest['dependencies'][-1]
        self.assertEqual(decomp['commit'], self.revision)
        self.assertEqual(decomp['tree'], self.git(self.repo, 'rev-parse', self.revision + ':decomp'))
        self.assertEqual(decomp['subtree'], 'decomp')
        self.assertEqual(decomp['profile'], 'native')
        for field, filenames in [('series', ['reconstruction-series', 'native-pre-series', 'series']),
                                 ('patches', ['common.patch', 'native.patch'])]:
            self.assertEqual(decomp[field], [
                {'path': 'patches/beachlife-decomp/' + name,
                 'sha256': packages.digest((self.patch_root / name).read_bytes())}
                for name in filenames])
        self.assertEqual((self.export / 'src/value.cpp').read_bytes(), b'native\n')
        self.assertEqual(before, {
            p.relative_to(self.prepared).as_posix(): (p.read_bytes(), p.stat().st_mtime_ns)
            for p in self.prepared.rglob('*') if p.is_file()})
        self.assertEqual(set(payload), packages.payload_names(self.info) | {'SOURCE-MANIFEST.json'})
        self.assertFalse(any(name.startswith(('decomp/', 'patches/', 'orig/')) for name in payload))

    @unittest.skipIf(os.name == 'nt', 'Synthetic ELF packaging requires POSIX executable permissions')
    def test_experimental_package_preserves_selected_profile_and_patch_hashes(self):
        self.write(self.patch_root / 'experimental.patch',
                   b'diff --git a/src/experimental.cpp b/src/experimental.cpp\n'
                   b'new file mode 100644\n--- /dev/null\n+++ b/src/experimental.cpp\n'
                   b'@@ -0,0 +1 @@\n+generated experimental source\n')
        self.write(self.patch_root / 'experimental-series', b'experimental.patch\n')
        identities = prepare(self.repo, self.prepared, decomp_revision=self.revision,
                             decomp_profile='native-experimental')
        self.refresh_build_identity('native-experimental')
        archive = packages.package(self.build, self.output, source=self.repo)
        manifest, payload = packages.inspect_package(archive)
        self.assertEqual(manifest['build']['decomp_profile'], 'native-experimental')
        self.assertEqual(manifest['dependencies'], identities)
        self.assertEqual([Path(row['path']).name for row in identities[-1]['series']],
                         ['reconstruction-series', 'native-pre-series', 'experimental-series', 'series'])
        self.assertEqual((self.export / 'src/experimental.cpp').read_bytes(),
                         b'generated experimental source\n')
        self.assertFalse(any(name.startswith(('decomp/', 'patches/', 'orig/')) for name in payload))

    def test_package_cannot_substitute_native_and_experimental_exports(self):
        for prepared, requested in (('native', 'native-experimental'),
                                    ('native-experimental', 'native')):
            with self.subTest(prepared=prepared, requested=requested):
                prepare(self.repo, self.prepared, decomp_revision=self.revision,
                        decomp_profile=prepared)
                self.refresh_build_identity(requested)
                with self.assertRaisesRegex(ValueError, 'Prepared beachlife-decomp is stale'):
                    packages.package(self.build, self.output, source=self.repo)
                self.assertFalse(self.output.exists())

    def test_package_rejects_unknown_profile(self):
        self.refresh_build_identity('matching')
        self.info['channel'] = 'dev'
        self.write(self.build / 'generated/build-info.json', json.dumps(self.info).encode())
        with self.assertRaisesRegex(ValueError, 'Unknown native reconstructed source profile'):
            packages.package(self.build, self.output, source=self.repo)
        self.assertFalse(self.output.exists())

    @unittest.skipIf(os.name == 'nt', 'Synthetic ELF packaging requires POSIX executable permissions')
    def test_release_packages_the_complete_game_and_notices(self):
        prepare(self.repo, self.prepared, decomp_revision=self.revision,
                decomp_profile='native-game-tweaks')
        self.refresh_build_identity('native-game-tweaks')
        self.info['channel'] = 'release'
        self.write(self.build / 'generated/build-info.json', json.dumps(self.info).encode())
        complete = {'functions': [], 'host_services': [], 'unresolved': []}
        for kind in complete:
            self.write(self.build / 'boot/traps.json',
                       json.dumps({**complete, kind: [{'symbol': 'missing'}]}).encode())
            with self.subTest(kind=kind), self.assertRaisesRegex(ValueError, 'requires zero'):
                packages.package(self.build, self.output, source=self.repo)
            self.assertFalse(self.output.exists())
        self.write(self.build / 'boot/traps.json', json.dumps(complete).encode())
        archive = packages.package(self.build, self.output, source=self.repo)
        manifest, payload = packages.inspect_package(archive)
        self.assertEqual(manifest['game_traps'], {kind: 0 for kind in complete})
        self.assertEqual(manifest['dependencies'][-1]['profile'], 'native-game-tweaks')
        self.assertEqual(set(payload), packages.payload_names(self.info) | {'SOURCE-MANIFEST.json'})
        for notice in packages.GAME_NOTICES:
            self.assertIn(notice, payload)
        self.assertNotIn('boot/traps.json', payload)

    def test_missing_native_export_is_rejected_without_repair(self):
        shutil.rmtree(self.export)
        self.stamp.unlink()
        self.assert_rejected('Prepared beachlife-decomp is stale')
        self.assertFalse(self.export.exists())
        self.assertFalse(self.stamp.exists())

    def test_incomplete_native_export_is_rejected(self):
        self.stamp.unlink()
        self.assert_rejected('no complete directory and manifest')
        self.assertFalse(self.stamp.exists())

    def test_matching_profile_is_not_a_native_export(self):
        prepare(self.repo, self.prepared, decomp_revision=self.revision, decomp_profile='matching')
        self.assertEqual((self.export / 'src/value.cpp').read_bytes(), b'reconstructed\n')
        self.assert_rejected('Prepared beachlife-decomp is stale')

    def test_changed_pin_is_stale_even_when_source_tree_is_identical(self):
        newer = self.git(self.repo, 'rev-parse', 'HEAD')
        self.assertNotEqual(newer, self.revision)
        self.assertEqual(self.git(self.repo, 'rev-parse', newer + ':decomp'),
                         self.git(self.repo, 'rev-parse', self.revision + ':decomp'))
        self.pin_path.write_text(json.dumps({'revision': newer}))
        self.assert_rejected('Prepared beachlife-decomp is stale')

    def test_missing_pin_is_rejected(self):
        self.pin_path.unlink()
        self.assert_rejected('Missing or unsafe package input: patches/beachlife-decomp/source.json')

    def test_pin_schema_and_revision_are_strict(self):
        values = [None, [], self.revision, {}, {'revision': self.revision, 'profile': 'native'},
                  {'revision': None}, {'revision': False}, {'revision': 42}, {'revision': []},
                  {'revision': 'HEAD'}, {'revision': self.revision[:12]},
                  {'revision': self.revision.upper()}]
        for value in values:
            with self.subTest(value=value):
                self.pin_path.write_text(json.dumps(value))
                self.assert_rejected('reconstructed source pin')
        self.pin_path.write_text('{invalid')
        self.assert_rejected('Expecting property name')

    def test_symlinked_pin_is_rejected(self):
        target = self.root / 'pin.json'
        target.write_bytes(self.pin_path.read_bytes())
        self.pin_path.unlink()
        try:
            self.pin_path.symlink_to(target)
        except OSError:
            self.skipTest('Symlinks unavailable on this runner')
        self.assert_rejected('Missing or unsafe package input')

    def test_edited_export_is_rejected(self):
        (self.export / 'src/value.cpp').write_bytes(b'edited\n')
        self.assert_rejected('Prepared beachlife-decomp was edited')

    def test_forged_manifest_does_not_hide_edited_export(self):
        (self.export / 'src/value.cpp').write_bytes(b'edited\n')
        record = json.loads(self.stamp.read_text())
        record['files']['src/value.cpp'] = packages.digest(b'edited\n')
        self.stamp.write_text(json.dumps(record))
        self.assert_rejected('Prepared beachlife-decomp manifest or files were edited')

    def test_modified_manifest_identity_is_rejected(self):
        record = json.loads(self.stamp.read_text())
        record['identity']['profile'] = 'matching'
        self.stamp.write_text(json.dumps(record))
        self.assert_rejected('Prepared beachlife-decomp is stale')

    def test_changed_native_patch_is_stale(self):
        path = self.patch_root / 'native.patch'
        path.write_bytes(path.read_bytes().replace(b'+native\n', b'+changed\n'))
        self.assert_rejected('Prepared beachlife-decomp is stale')

    def test_changed_common_series_is_stale(self):
        with (self.patch_root / 'reconstruction-series').open('ab') as handle:
            handle.write(b'# changed series identity\n')
        self.assert_rejected('Prepared beachlife-decomp is stale')

    def test_external_dependency_verification_is_preserved(self):
        (self.prepared / 'sdl/fixture.txt').write_bytes(b'edited dependency\n')
        self.assert_rejected('Prepared sdl was edited')


if __name__ == '__main__':
    unittest.main()
