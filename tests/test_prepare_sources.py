import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from prepare_sources import prepare


class PreparedSourceTests(unittest.TestCase):
    def test_all_six_dependency_defaults_ignore_decomp_profiles(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            repo, output = root / 'repo', root / 'prepared'
            repo.mkdir()

            def git(path, *args):
                return subprocess.check_output(['git', '-C', str(path), *args], text=True,
                                               stderr=subprocess.DEVNULL).strip()

            git(repo, 'init', '-q')
            names = ('sdl', 'imgui', 'stb', 'unshield', 'zlib', 'freetype', 'dr-libs', 'pl-mpeg')
            for name in names:
                checkout = repo / 'extern' / name
                checkout.mkdir(parents=True)
                git(checkout, 'init', '-q')
                git(checkout, 'config', 'core.autocrlf', 'false')
                (checkout / 'value.txt').write_bytes((name + '\n').encode())
                git(checkout, 'add', 'value.txt')
                git(checkout, '-c', 'user.name=Yannick Suter',
                    '-c', 'user.email=yannick.suter@gmail.com',
                    'commit', '-qm', 'test: generated dependency pin')
                pin = git(checkout, 'rev-parse', 'HEAD')
                git(repo, 'update-index', '--add', '--cacheinfo',
                    '160000,' + pin + ',extern/' + name)
                ignored = repo / 'patches' / name / 'reconstruction-series'
                ignored.parent.mkdir(parents=True)
                ignored.write_bytes(b'missing.patch\n')
            identities = prepare(repo, output)
            self.assertEqual([identity['name'] for identity in identities], list(names))
            for name, identity in zip(names, identities):
                self.assertEqual(set(identity), {'name', 'commit', 'patches'})
                self.assertEqual(identity['patches'], [])
                self.assertEqual((output / name / 'value.txt').read_bytes(),
                                 (name + '\n').encode())
            records = {p.name: p.read_bytes() for p in output.glob('*.json')}
            for profile in ('native', 'matching'):
                self.assertEqual(prepare(repo, output, verify=True, decomp_profile=profile),
                                 identities)
                self.assertEqual(records, {p.name: p.read_bytes() for p in output.glob('*.json')})

    def test_pin_ordered_patches_and_tamper_detection(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            repo, output = root / 'repo', root / 'prepared'
            checkout = repo / 'extern' / 'fixture'
            checkout.mkdir(parents=True)

            def git(path, *args):
                return subprocess.check_output(['git', '-C', str(path), *args], text=True, stderr=subprocess.DEVNULL).strip()

            for path in (repo, checkout):
                git(path, 'init', '-q')
                git(path, 'config', 'core.autocrlf', 'false')
            (checkout / 'value.txt').write_bytes(b'first\n')
            git(checkout, 'add', 'value.txt')
            git(checkout, '-c', 'user.name=Yannick Suter', '-c', 'user.email=yannick.suter@gmail.com',
                'commit', '-qm', 'test: prepare source fixture')
            pin = git(checkout, 'rev-parse', 'HEAD')
            git(repo, 'update-index', '--add', '--cacheinfo', '160000,' + pin + ',extern/fixture')
            patches = repo / 'patches' / 'fixture'
            patches.mkdir(parents=True)
            for name, before, after in [('one.patch', 'first', 'second'), ('two.patch', 'second', 'third')]:
                patch_text = f'diff --git a/value.txt b/value.txt\n--- a/value.txt\n+++ b/value.txt\n@@ -1 +1 @@\n-{before}\n+{after}\n'
                (patches / name).write_bytes(patch_text.encode())
            (patches / 'series').write_bytes(b'one.patch\ntwo.patch\n')
            # Git for Windows commonly enables automatic CRLF conversion.
            with patch.dict(os.environ, {'GIT_CONFIG_COUNT': '1',
                                        'GIT_CONFIG_KEY_0': 'core.autocrlf',
                                        'GIT_CONFIG_VALUE_0': 'true'}):
                prepare(repo, output, ['fixture'])
            self.assertEqual((output / 'fixture/value.txt').read_bytes(), b'third\n')
            self.assertEqual((checkout / 'value.txt').read_bytes(), b'first\n')
            prepare(repo, output, ['fixture'], verify=True)
            (output / 'fixture/value.txt').write_bytes(b'tampered\n')
            with self.assertRaisesRegex(ValueError, 'was edited'):
                prepare(repo, output, ['fixture'], verify=True)
            (output / 'fixture/value.txt').write_bytes(b'third\n')
            (checkout / 'value.txt').write_bytes(b'local edit\n')
            with self.assertRaisesRegex(ValueError, 'local edits'):
                prepare(repo, output, ['fixture'])
            (checkout / 'value.txt').write_bytes(b'first\n')
            (patches / 'series').write_bytes(b'two.patch\none.patch\n')
            with self.assertRaisesRegex(ValueError, 'stale'):
                prepare(repo, output, ['fixture'], verify=True)


class LocalDecompTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.repo = self.root / 'repo'
        self.output = self.root / 'prepared'
        self.repo.mkdir()
        self.git('init', '-q')
        self.git('config', 'core.autocrlf', 'false')
        self.write('decomp/src/value.cpp', b'first\n')
        self.write('decomp/README.md', b'generated reconstructed source\n')
        self.write('host.txt', b'host\n')
        self.write('orig/not-an-input.txt', b'generated original-input sentinel\n')
        self.write('patches/beachlife-decomp/series', b'# faithful patches\n')
        self.first = self.commit()

    def git(self, *args, body=None):
        return subprocess.check_output(['git', '-C', str(self.repo), *args], input=body,
                                       text=True, stderr=subprocess.DEVNULL).strip()

    def write(self, relative, body):
        path = self.repo / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(body)
        return path

    def commit(self):
        self.git('add', '.')
        self.git('-c', 'user.name=Yannick Suter', '-c', 'user.email=yannick.suter@gmail.com',
                 'commit', '-qm', 'test: update generated source fixture')
        return self.git('rev-parse', 'HEAD')

    def prepare(self, revision=None, **kwargs):
        return prepare(self.repo, self.output, (), decomp_revision=revision or self.first, **kwargs)

    def record(self):
        import json
        return json.loads((self.output / 'beachlife-decomp.json').read_text())

    def patch(self, name, before, after):
        body = (f'diff --git a/src/value.cpp b/src/value.cpp\n'
                f'--- a/src/value.cpp\n+++ b/src/value.cpp\n@@ -1 +1 @@\n-{before}\n+{after}\n')
        return self.write('patches/beachlife-decomp/' + name, body.encode())

    def symlink(self, path, target):
        try:
            path.symlink_to(target, target_is_directory=target.is_dir())
        except OSError as error:
            self.skipTest(f'Symlinks unavailable: {error}')

    def test_explicit_older_revision_and_before_after_manifest(self):
        import hashlib
        self.write('decomp/src/value.cpp', b'newer committed source\n')
        second = self.commit()
        self.write('host.txt', b'uncommitted host changes are independent\n')
        self.write('orig/extra.txt', b'never exported\n')
        with patch.dict(os.environ, {'GIT_CONFIG_COUNT': '1',
                                    'GIT_CONFIG_KEY_0': 'core.autocrlf',
                                    'GIT_CONFIG_VALUE_0': 'true'}):
            identities = self.prepare()
        exported = self.output / 'beachlife-decomp'
        self.assertEqual((exported / 'src/value.cpp').read_bytes(), b'first\n')
        self.assertEqual(set(p.relative_to(exported).as_posix() for p in exported.rglob('*') if p.is_file()),
                         {'src/value.cpp', 'README.md'})
        self.assertEqual((self.repo / 'decomp/src/value.cpp').read_bytes(), b'newer committed source\n')
        record = self.record()
        self.assertEqual(identities, [record['identity']])
        self.assertEqual(record['identity']['commit'], self.first)
        self.assertEqual(record['identity']['subtree'], 'decomp')
        self.assertEqual(record['identity']['tree'], self.git('rev-parse', self.first + ':decomp'))
        self.assertEqual(record['source_files']['src/value.cpp'], hashlib.sha256(b'first\n').hexdigest())
        self.assertEqual(record['files'], record['source_files'])
        times = {p: p.stat().st_mtime_ns for p in exported.rglob('*') if p.is_file()}
        self.prepare(verify=True)
        self.assertEqual(times, {p: p.stat().st_mtime_ns for p in times})
        with self.assertRaisesRegex(ValueError, 'stale'):
            self.prepare(second, verify=True)
        self.prepare(second)
        self.assertEqual((exported / 'src/value.cpp').read_bytes(), b'newer committed source\n')

    def test_revision_requires_full_existing_commit_and_subtree(self):
        from prepare_sources import main
        with self.assertRaises(SystemExit) as error:
            main(['--output', str(self.output), '--decomp-only'])
        self.assertEqual(error.exception.code, 2)
        self.assertFalse(self.output.exists())
        for revision in ('', 'HEAD', self.first[:12], '--help'):
            with self.subTest(revision=revision), self.assertRaisesRegex(ValueError, 'full local commit'):
                prepare(self.repo, self.output, (), decomp_revision=revision)
        with self.assertRaises(subprocess.CalledProcessError):
            self.prepare('0' * 40)
        self.git('update-ref', 'refs/heads/' + '0' * 40, self.first)
        with self.assertRaises((ValueError, subprocess.CalledProcessError)):
            self.prepare('0' * 40)
        tree = self.git('rev-parse', self.first + ':decomp')
        with self.assertRaisesRegex(ValueError, 'identify a commit'):
            self.prepare(tree)
        self.git('rm', '-r', '-q', 'decomp')
        empty = self.commit()
        with self.assertRaises(subprocess.CalledProcessError):
            self.prepare(empty)
        self.assertFalse(self.output.exists())

    def test_commit_tree_and_blob_replacements_cannot_change_the_pin(self):
        import hashlib
        first_tree = self.git('rev-parse', self.first + ':decomp')
        first_blob = self.git('rev-parse', self.first + ':decomp/src/value.cpp')
        self.write('decomp/src/value.cpp', b'replacement\n')
        second = self.commit()
        second_tree = self.git('rev-parse', second + ':decomp')
        second_blob = self.git('rev-parse', second + ':decomp/src/value.cpp')
        for kind, original, replacement in (('commit', self.first, second),
                                             ('tree', first_tree, second_tree),
                                             ('blob', first_blob, second_blob)):
            with self.subTest(kind=kind):
                self.output = self.root / ('prepared-' + kind)
                self.git('replace', original, replacement)
                self.assertEqual(self.git('show', self.first + ':decomp/src/value.cpp'), 'replacement')
                self.prepare()
                self.prepare(verify=True)
                record = self.record()
                self.assertEqual((self.output / 'beachlife-decomp/src/value.cpp').read_bytes(), b'first\n')
                self.assertEqual(record['identity']['commit'], self.first)
                self.assertEqual(record['identity']['tree'], first_tree)
                self.assertEqual(record['source_files']['src/value.cpp'], hashlib.sha256(b'first\n').hexdigest())
                self.assertEqual(record['files'], record['source_files'])
                self.assertEqual(self.git('replace', '-l'), original)
                self.git('replace', '-d', original)

    def test_source_edit_policy_is_scoped_to_decomp(self):
        value = self.repo / 'decomp/src/value.cpp'
        value.write_bytes(b'uncommitted\n')
        with self.assertRaisesRegex(ValueError, 'local edits'):
            self.prepare()
        self.git('add', 'decomp/src/value.cpp')
        with self.assertRaisesRegex(ValueError, 'local edits'):
            self.prepare()
        self.git('reset', '-q', 'HEAD', '--', 'decomp')
        value.write_bytes(b'first\n')
        untracked = self.write('decomp/new.cpp', b'new source\n')
        with self.assertRaisesRegex(ValueError, 'local edits'):
            self.prepare()
        untracked.unlink()
        self.write('.gitignore', b'decomp/ignored.txt\n')
        self.commit()
        self.write('decomp/ignored.txt', b'not a committed input\n')
        self.write('host.txt', b'allowed unrelated edit\n')
        self.prepare()
        self.assertFalse((self.output / 'beachlife-decomp/ignored.txt').exists())

    def test_ordered_faithful_patches_and_stale_identity(self):
        import hashlib
        one = self.patch('one.patch', 'first', 'second')
        two = self.patch('two.patch', 'second', 'third')
        series = self.write('patches/beachlife-decomp/series', b'one.patch\ntwo.patch\n')
        self.write('patches/beachlife-decomp-tweaks/series', b'missing.patch\n')
        self.prepare()
        target = self.output / 'beachlife-decomp/src/value.cpp'
        self.assertEqual(target.read_bytes(), b'third\n')
        self.assertEqual((self.repo / 'decomp/src/value.cpp').read_bytes(), b'first\n')
        record = self.record()
        self.assertEqual(record['source_files']['src/value.cpp'], hashlib.sha256(b'first\n').hexdigest())
        self.assertEqual(record['files']['src/value.cpp'], hashlib.sha256(b'third\n').hexdigest())
        self.assertEqual([p['path'] for p in record['identity']['patches']],
                         ['patches/beachlife-decomp/one.patch', 'patches/beachlife-decomp/two.patch'])
        self.assertEqual(record['identity']['profile'], 'native')
        self.assertEqual(record['identity']['series'], [
            {'path': 'patches/beachlife-decomp/reconstruction-series', 'sha256': None},
            {'path': 'patches/beachlife-decomp/native-pre-series', 'sha256': None},
            {'path': 'patches/beachlife-decomp/series',
             'sha256': hashlib.sha256(series.read_bytes()).hexdigest()}])
        self.prepare(verify=True)
        series.write_bytes(b'two.patch\none.patch\n')
        with self.assertRaisesRegex(ValueError, 'stale'):
            self.prepare(verify=True)
        with self.assertRaises(subprocess.CalledProcessError):
            self.prepare()
        self.assertEqual(target.read_bytes(), b'third\n')
        self.assertEqual(record, self.record())
        series.write_bytes(b'one.patch\ntwo.patch\n')
        one.write_bytes(one.read_bytes() + b'\n')
        with self.assertRaisesRegex(ValueError, 'stale'):
            self.prepare(verify=True)
        self.prepare()
        self.assertEqual(target.read_bytes(), b'third\n')
        self.assertNotEqual(record['identity']['patches'], self.record()['identity']['patches'])
        two.unlink()
        with self.assertRaisesRegex(ValueError, 'Invalid patch entry'):
            self.prepare()

    def profile_patches(self):
        common = self.patch('common.patch', 'first', 'reconstructed')
        native = self.patch('native.patch', 'reconstructed', 'native')
        reconstruction = self.write('patches/beachlife-decomp/reconstruction-series',
                                    b'common.patch\n')
        adaptations = self.write('patches/beachlife-decomp/series', b'native.patch\n')
        return common, native, reconstruction, adaptations

    def test_failed_batch_reports_later_patch_without_publishing_partial_tree(self):
        import contextlib
        import io
        self.patch('one.patch', 'first', 'second')
        self.patch('two.patch', 'second', 'third')
        self.patch('broken.patch', 'missing context', 'fourth')
        series = self.write('patches/beachlife-decomp/series', b'one.patch\n')
        self.prepare()
        target = self.output / 'beachlife-decomp/src/value.cpp'
        stamp = self.output / 'beachlife-decomp.json'
        previous = stamp.read_bytes()
        mtime = target.stat().st_mtime_ns

        series.write_bytes(b'one.patch\ntwo.patch\nbroken.patch\n')
        errors = io.StringIO()
        with contextlib.redirect_stderr(errors), self.assertRaises(subprocess.CalledProcessError):
            self.prepare()
        # The tool reports the resolved path (/private/var on macOS, long names on Windows).
        self.assertIn('Failed to apply patch: ' + str((series.parent / 'broken.patch').resolve()),
                      errors.getvalue())
        self.assertEqual(target.read_bytes(), b'second\n')
        self.assertEqual(target.stat().st_mtime_ns, mtime)
        self.assertEqual(stamp.read_bytes(), previous)
        self.assertEqual({p.name for p in self.output.iterdir()},
                         {'beachlife-decomp', 'beachlife-decomp.json'})

    def test_profiles_share_reconstruction_before_native_adaptation(self):
        import hashlib
        common, native, reconstruction, adaptations = self.profile_patches()
        common.write_bytes(common.read_bytes() +
                           b'diff --git a/src/new.cpp b/src/new.cpp\n'
                           b'new file mode 100644\n--- /dev/null\n+++ b/src/new.cpp\n'
                           b'@@ -0,0 +1 @@\n+generated new reconstruction\n')
        for profile, result, series, patches in (
                ('native', b'native\n',
                 [reconstruction, self.repo / 'patches/beachlife-decomp/native-pre-series',
                  adaptations], [common, native]),
                ('matching', b'reconstructed\n', [reconstruction], [common])):
            with self.subTest(profile=profile):
                self.output = self.root / ('prepared-' + profile)
                # The omitted profile preserves the default native command.
                kwargs = {} if profile == 'native' else {'decomp_profile': profile}
                self.prepare(**kwargs)
                self.prepare(verify=True, decomp_profile=profile)
                target = self.output / 'beachlife-decomp'
                self.assertEqual((target / 'src/value.cpp').read_bytes(), result)
                self.assertEqual((target / 'src/new.cpp').read_bytes(),
                                 b'generated new reconstruction\n')
                record = self.record()
                identity = record['identity']
                self.assertEqual(identity['profile'], profile)
                self.assertEqual(identity['series'], [
                    {'path': p.relative_to(self.repo).as_posix(),
                     'sha256': hashlib.sha256(p.read_bytes()).hexdigest() if p.is_file() else None}
                    for p in series])
                self.assertEqual(identity['patches'], [
                    {'path': p.relative_to(self.repo).as_posix(),
                     'sha256': hashlib.sha256(p.read_bytes()).hexdigest()} for p in patches])
                self.assertNotIn('src/new.cpp', record['source_files'])
                self.assertIn('src/new.cpp', record['files'])
        self.assertEqual((self.repo / 'decomp/src/value.cpp').read_bytes(), b'first\n')
        self.assertFalse((self.repo / 'decomp/src/new.cpp').exists())

    def test_profile_switch_is_stale_and_cannot_overwrite_modified_files(self):
        self.profile_patches()
        self.prepare()
        target = self.output / 'beachlife-decomp/src/value.cpp'
        for before, after, value in (('native', 'native-experimental', b'native\n'),
                                     ('native-experimental', 'matching', b'reconstructed\n'),
                                     ('matching', 'native', b'native\n')):
            with self.subTest(before=before, after=after):
                record = self.record()
                original = target.read_bytes()
                with self.assertRaisesRegex(ValueError, 'stale'):
                    self.prepare(verify=True, decomp_profile=after)
                self.assertEqual(self.record(), record)
                self.assertEqual(target.read_bytes(), original)
                target.write_bytes(b'local modification\n')
                for verify in (False, True):
                    with self.assertRaisesRegex(ValueError, 'was edited'):
                        self.prepare(verify=verify, decomp_profile=after)
                self.assertEqual(target.read_bytes(), b'local modification\n')
                target.write_bytes(original)
                self.prepare(decomp_profile=after)
                self.assertEqual(target.read_bytes(), value)
                self.assertEqual(self.record()['identity']['profile'], after)
                self.prepare(verify=True, decomp_profile=after)

    def matching_patch(self, address, destination=None):
        destination = destination or f'src/match_{address}.cpp'
        body = (f'diff --git a/{destination} b/{destination}\n'
                'new file mode 100644\n--- /dev/null\n'
                f'+++ b/{destination}\n@@ -0,0 +1 @@\n+generated {address}\n')
        return self.write(f'patches/beachlife-decomp/matching-series.d/{address}.patch', body.encode())

    def test_parallel_matching_patches_merge_and_export_in_both_orders(self):
        from prepare_sources import DECOMP_PROFILES, tree_manifest
        self.prepare()
        native = tree_manifest(self.output / 'beachlife-decomp')
        for address in ('00402000', '00401000'):
            self.git('checkout', '-qb', 'worker-' + address, self.first)
            self.matching_patch(address)
            self.commit()
        exports = []
        for order in (('00401000', '00402000'), ('00402000', '00401000')):
            self.git('checkout', '-qb', 'merge-' + order[0], self.first)
            for address in order:
                self.git('-c', 'user.name=Yannick Suter', '-c', 'user.email=yannick.suter@gmail.com',
                         'merge', '--no-ff', '-m', 'test: merge matching registration', 'worker-' + address)
            self.output = self.root / ('merged-' + order[0])
            self.prepare(decomp_profile='matching')
            exports.append(tree_manifest(self.output / 'beachlife-decomp'))
            self.assertEqual([p['path'].rsplit('/', 1)[-1] for p in self.record()['identity']['patches']],
                             ['00401000.patch', '00402000.patch'])
            for profile in DECOMP_PROFILES:
                if profile != 'matching':
                    self.prepare(decomp_profile=profile)
                    self.assertEqual(tree_manifest(self.output / 'beachlife-decomp'), native)
        self.assertEqual(exports[0], exports[1])
        self.assertEqual(set(exports[0]) - set(native),
                         {'src/match_00401000.cpp', 'src/match_00402000.cpp'})

    def test_matching_patch_add_remove_and_change_invalidate_export(self):
        self.prepare(decomp_profile='matching')
        addition = self.matching_patch('00401000')
        for action in ('add', 'change', 'remove'):
            if action == 'change':
                addition.write_text(addition.read_text().replace('+generated', '+changed'))
            elif action == 'remove':
                addition.unlink()
            with self.subTest(action=action):
                with self.assertRaisesRegex(ValueError, 'stale'):
                    self.prepare(verify=True, decomp_profile='matching')
                self.prepare(decomp_profile='matching')
                self.prepare(verify=True, decomp_profile='matching')

    def test_matching_patches_reject_shared_edits_and_colliding_creations(self):
        addition = self.matching_patch('00401000')
        before = addition.read_bytes()
        edit = self.patch('edit.patch', 'first', 'changed').read_bytes()
        for body in (edit, before + edit, before.replace(b'100644', b'120000'),
                     before.replace(b'src/match_00401000.cpp', b'../escape')):
            addition.write_bytes(body)
            with self.subTest(body=body), self.assertRaisesRegex(ValueError, 'only create text files'):
                self.prepare(decomp_profile='matching')
        addition.write_bytes(before)
        self.matching_patch('00402000', 'src/match_00401000.cpp')
        with self.assertRaises(subprocess.CalledProcessError):
            self.prepare(decomp_profile='matching')
        self.assertFalse(self.output.joinpath('beachlife-decomp').exists())

    def test_matching_patch_directory_rejects_symlinks_and_bad_names(self):
        addition = self.matching_patch('00401000')
        wrong = addition.with_name('unkeyed.patch')
        addition.rename(wrong)
        with self.assertRaisesRegex(ValueError, 'lowercase address'):
            self.prepare(decomp_profile='matching')
        wrong.rename(addition)
        addition.unlink()
        self.symlink(addition, self.repo / 'host.txt')
        with self.assertRaisesRegex(ValueError, 'symlinked'):
            self.prepare(decomp_profile='matching')

    def test_experimental_profile_is_opt_in_and_ordered(self):
        import hashlib
        common, native, reconstruction, adaptations = self.profile_patches()
        common.write_bytes(common.read_bytes() +
                           b'diff --git a/src/helper.cpp b/src/helper.cpp\n'
                           b'new file mode 100644\n--- /dev/null\n+++ b/src/helper.cpp\n'
                           b'@@ -0,0 +1 @@\n+common helper\n')
        experimental = self.write('patches/beachlife-decomp/experimental.patch',
                                  b'diff --git a/src/helper.cpp b/src/helper.cpp\n'
                                  b'--- a/src/helper.cpp\n+++ b/src/helper.cpp\n'
                                  b'@@ -1 +1 @@\n-common helper\n+experimental helper\n')
        series = self.write('patches/beachlife-decomp/experimental-series',
                            b'experimental.patch\n')
        for profile, value, helper, ordered_series, patches in (
                ('matching', b'reconstructed\n', b'common helper\n', [reconstruction], [common]),
                ('native', b'native\n', b'common helper\n',
                 [reconstruction, self.repo / 'patches/beachlife-decomp/native-pre-series', adaptations],
                 [common, native]),
                ('native-experimental', b'native\n', b'experimental helper\n',
                 [reconstruction, self.repo / 'patches/beachlife-decomp/native-pre-series',
                  series, adaptations], [common, experimental, native])):
            with self.subTest(profile=profile):
                self.output = self.root / ('prepared-' + profile)
                self.prepare(decomp_profile=profile)
                self.prepare(verify=True, decomp_profile=profile)
                target = self.output / 'beachlife-decomp/src'
                self.assertEqual((target / 'value.cpp').read_bytes(), value)
                self.assertEqual((target / 'helper.cpp').read_bytes(), helper)
                identity = self.record()['identity']
                self.assertEqual(identity['profile'], profile)
                for key, paths in (('series', ordered_series), ('patches', patches)):
                    self.assertEqual(identity[key], [
                        {'path': p.relative_to(self.repo).as_posix(),
                         'sha256': hashlib.sha256(p.read_bytes()).hexdigest() if p.is_file() else None}
                        for p in paths])
        for path in (series, experimental):
            path.write_bytes(path.read_bytes() + b'\n')
            for profile in ('matching', 'native', 'native-experimental'):
                self.output = self.root / ('prepared-' + profile)
                if profile == 'native-experimental':
                    with self.assertRaisesRegex(ValueError, 'stale'):
                        self.prepare(verify=True, decomp_profile=profile)
                    self.prepare(decomp_profile=profile)
                self.prepare(verify=True, decomp_profile=profile)
        self.assertEqual((self.repo / 'decomp/src/value.cpp').read_bytes(), b'first\n')
        self.assertFalse((self.repo / 'decomp/src/helper.cpp').exists())

    def test_game_fixes_follow_faithful_sources_and_precede_optional_tweaks(self):
        import hashlib
        common, native, _, _ = self.profile_patches()
        game = self.patch('game.patch', 'native', 'game')
        self.write('patches/beachlife-decomp/game-series', b'game.patch\n')
        fix = self.patch('../beachlife-decomp-fixes/fix.patch', 'game', 'fixed game')
        fixes = self.write('patches/beachlife-decomp-fixes/series', b'fix.patch\n')
        tweak = self.patch('../beachlife-decomp-tweaks/tweak.patch',
                           'fixed game', 'fixed game with tweak')
        self.write('patches/beachlife-decomp-tweaks/series', b'tweak.patch\n')
        for profile, value, patches in (
                ('matching', 'reconstructed', [common]),
                ('native', 'native', [common, native]),
                ('native-experimental', 'native', [common, native]),
                ('native-game', 'fixed game', [common, native, game, fix]),
                ('native-game-tweaks', 'fixed game with tweak',
                 [common, native, game, fix, tweak])):
            with self.subTest(profile=profile):
                self.output = self.root / ('prepared-' + profile)
                self.prepare(decomp_profile=profile)
                self.prepare(verify=True, decomp_profile=profile)
                self.assertEqual((self.output / 'beachlife-decomp/src/value.cpp').read_text(),
                                 value + '\n')
                self.assertEqual(self.record()['identity']['patches'], [
                    {'path': p.resolve().relative_to(self.repo.resolve()).as_posix(),
                     'sha256': hashlib.sha256(p.read_bytes()).hexdigest()} for p in patches])
        # Either a series edit or a fix edit invalidates only game exports.
        for changed in (fixes, fix):
            changed.write_bytes(changed.read_bytes() + b'\n')
            for profile in ('matching', 'native', 'native-experimental',
                            'native-game', 'native-game-tweaks'):
                self.output = self.root / ('prepared-' + profile)
                if profile.startswith('native-game'):
                    with self.assertRaisesRegex(ValueError, 'stale'):
                        self.prepare(verify=True, decomp_profile=profile)
                    self.prepare(decomp_profile=profile)
                self.prepare(verify=True, decomp_profile=profile)
        self.assertEqual((self.repo / 'decomp/src/value.cpp').read_bytes(), b'first\n')

    def test_native_pre_series_precedes_experimental_and_is_fingerprinted(self):
        common, native, reconstruction, adaptations = self.profile_patches()
        common.write_bytes(common.read_bytes() +
                           b'diff --git a/src/header.h b/src/header.h\n'
                           b'new file mode 100644\n--- /dev/null\n+++ b/src/header.h\n'
                           b'@@ -0,0 +1 @@\n+matching declaration\n')
        early = self.write('patches/beachlife-decomp/early.patch',
                           b'diff --git a/src/header.h b/src/header.h\n'
                           b'--- a/src/header.h\n+++ b/src/header.h\n'
                           b'@@ -1 +1 @@\n-matching declaration\n+native declaration\n')
        before = self.write('patches/beachlife-decomp/native-pre-series', b'early.patch\n')
        experimental = self.write('patches/beachlife-decomp/experimental.patch',
                                  b'diff --git a/src/header.h b/src/header.h\n'
                                  b'--- a/src/header.h\n+++ b/src/header.h\n'
                                  b'@@ -1 +1 @@\n-native declaration\n+experimental declaration\n')
        self.write('patches/beachlife-decomp/experimental-series', b'experimental.patch\n')
        for profile, header, patches in (
                ('matching', b'matching declaration\n', [common]),
                ('native', b'native declaration\n', [common, early, native]),
                ('native-experimental', b'experimental declaration\n',
                 [common, early, experimental, native])):
            self.output = self.root / ('prepared-' + profile)
            self.prepare(decomp_profile=profile)
            self.prepare(verify=True, decomp_profile=profile)
            self.assertEqual((self.output / 'beachlife-decomp/src/header.h').read_bytes(), header)
            self.assertEqual([row['path'] for row in self.record()['identity']['patches']],
                             [path.relative_to(self.repo).as_posix() for path in patches])
        for changed in (before, early):
            changed.write_bytes(changed.read_bytes() + b'\n')
            for profile in ('matching', 'native', 'native-experimental'):
                self.output = self.root / ('prepared-' + profile)
                if profile != 'matching':
                    with self.assertRaisesRegex(ValueError, 'stale'):
                        self.prepare(verify=True, decomp_profile=profile)
                    self.prepare(decomp_profile=profile)
                self.prepare(verify=True, decomp_profile=profile)

    def test_experimental_series_rejects_invalid_entries_and_duplicates(self):
        self.profile_patches()
        series = self.write('patches/beachlife-decomp/experimental-series', b'missing.patch\n')
        for body, message in ((b'missing.patch\n', 'Invalid patch entry'),
                              (b'../escape.patch\n', 'Invalid patch entry'),
                              (b'common.patch\n', 'Duplicate patch entry across series')):
            with self.subTest(body=body):
                series.write_bytes(body)
                with self.assertRaisesRegex(ValueError, message):
                    self.prepare(decomp_profile='native-experimental')
                # Only the explicitly selected profile reads this series.
                for profile in ('native', 'matching'):
                    self.output = self.root / ('prepared-' + profile)
                    self.prepare(decomp_profile=profile)
                    self.prepare(verify=True, decomp_profile=profile)

    def test_profile_series_changes_are_scoped_and_fingerprinted(self):
        common, native, reconstruction, adaptations = self.profile_patches()
        for profile in ('native', 'matching'):
            self.output = self.root / ('prepared-' + profile)
            self.prepare(decomp_profile=profile)
        for path, affected in ((adaptations, ('native',)), (native, ('native',)),
                               (reconstruction, ('native', 'matching')),
                               (common, ('native', 'matching'))):
            with self.subTest(path=path.name):
                path.write_bytes(path.read_bytes() + b'\n')
                for profile in ('native', 'matching'):
                    self.output = self.root / ('prepared-' + profile)
                    if profile in affected:
                        with self.assertRaisesRegex(ValueError, 'stale'):
                            self.prepare(verify=True, decomp_profile=profile)
                        self.prepare(decomp_profile=profile)
                    self.prepare(verify=True, decomp_profile=profile)

    def test_optional_series_absence_is_recorded_and_changes_are_stale(self):
        native = self.repo / 'patches/beachlife-decomp/series'
        native.unlink()
        for profile in ('native', 'matching'):
            self.output = self.root / ('prepared-' + profile)
            self.prepare(decomp_profile=profile)
            identity = self.record()['identity']
            self.assertEqual(len(identity['series']), 3 if profile == 'native' else 1)
            self.assertTrue(all(row['sha256'] is None for row in identity['series']))
        self.write('patches/beachlife-decomp/reconstruction-series', b'# no patches yet\n')
        for profile in ('native', 'matching'):
            self.output = self.root / ('prepared-' + profile)
            with self.assertRaisesRegex(ValueError, 'stale'):
                self.prepare(verify=True, decomp_profile=profile)
            self.prepare(decomp_profile=profile)
        native.write_bytes(b'# native only\n')
        self.output = self.root / 'prepared-matching'
        self.prepare(verify=True, decomp_profile='matching')
        self.output = self.root / 'prepared-native'
        with self.assertRaisesRegex(ValueError, 'stale'):
            self.prepare(verify=True)
        self.prepare()
        self.prepare(verify=True)

    def test_duplicates_and_malformed_entries_in_applicable_lists_are_rejected(self):
        self.patch('one.patch', 'first', 'second')
        reconstruction = self.repo / 'patches/beachlife-decomp/reconstruction-series'
        adaptations = self.repo / 'patches/beachlife-decomp/series'
        pre_native = self.repo / 'patches/beachlife-decomp/native-pre-series'
        for series in (reconstruction, pre_native, adaptations):
            for body in (b'missing.patch\n', b'../escape.patch\n', b'/absolute.patch\n',
                         b'C:\\escape.patch\n', b'bad\0name.patch\n', b'one.patch\none.patch\n'):
                reconstruction.write_bytes(b'# empty\n')
                pre_native.write_bytes(b'# empty\n')
                adaptations.write_bytes(b'# empty\n')
                series.write_bytes(body)
                with self.subTest(series=series.name, body=body), self.assertRaisesRegex(
                        ValueError, 'Invalid patch entry|Duplicate patch entry'):
                    self.prepare()
                self.assertFalse(self.output.exists())
        reconstruction.write_bytes(b'one.patch\n')
        adaptations.write_bytes(b'./one.patch\n')
        with self.assertRaisesRegex(ValueError, 'Duplicate patch entry across series'):
            self.prepare()
        # The matching profile neither applies nor validates the native list.
        adaptations.write_bytes(b'missing.patch\n')
        self.prepare(decomp_profile='matching')
        self.assertEqual((self.output / 'beachlife-decomp/src/value.cpp').read_bytes(), b'second\n')
        self.prepare(verify=True, decomp_profile='matching')
        with self.assertRaisesRegex(ValueError, 'Invalid patch entry'):
            self.prepare()

    def test_series_filename_and_profile_validation(self):
        from prepare_sources import decomp_inputs, main, patch_paths
        for filename in ('../series', 'folder/series', '/series', 'folder\\series',
                         'series:stream', 'series\0', '', 'other-series'):
            with self.subTest(filename=filename), self.assertRaisesRegex(
                    ValueError, 'Invalid patch series filename'):
                patch_paths(self.repo, 'beachlife-decomp', filename)
        for profile in ('', '../native', 'other', None):
            with self.subTest(profile=profile):
                with self.assertRaisesRegex(ValueError, 'Invalid decomp profile'):
                    self.prepare(decomp_profile=profile)
                with self.assertRaisesRegex(ValueError, 'Invalid decomp profile'):
                    decomp_inputs(self.repo, self.first, profile)
        self.assertFalse(self.output.exists())
        with self.assertRaises(SystemExit) as error:
            main(['--output', str(self.output), '--decomp-profile', 'other'])
        self.assertEqual(error.exception.code, 2)
        reconstruction = self.repo / 'patches/beachlife-decomp/reconstruction-series'
        reconstruction.mkdir()
        with self.assertRaisesRegex(ValueError, 'Invalid patch series'):
            self.prepare()
        reconstruction.rmdir()
        self.symlink(reconstruction, self.repo / 'host.txt')
        with self.assertRaisesRegex(ValueError, 'symlinked'):
            self.prepare(decomp_profile='matching')

    def test_profiles_preserve_manifest_tamper_detection(self):
        import hashlib
        import json
        self.profile_patches()
        for profile in ('native', 'matching', 'native-experimental'):
            with self.subTest(profile=profile):
                self.output = self.root / ('prepared-' + profile)
                self.prepare(decomp_profile=profile)
                target = self.output / 'beachlife-decomp/src/value.cpp'
                stamp = self.output / 'beachlife-decomp.json'
                original_record = stamp.read_bytes()
                record = self.record()
                record['identity']['profile'] = 'tampered'
                stamp.write_text(json.dumps(record))
                with self.assertRaisesRegex(ValueError, 'stale'):
                    self.prepare(verify=True, decomp_profile=profile)
                stamp.write_bytes(original_record)
                record = self.record()
                target.write_bytes(b'altered output and matching manifest\n')
                record['files']['src/value.cpp'] = hashlib.sha256(target.read_bytes()).hexdigest()
                stamp.write_text(json.dumps(record))
                for verify in (False, True):
                    with self.assertRaisesRegex(ValueError, 'manifest or files were edited'):
                        self.prepare(verify=verify, decomp_profile=profile)

    def test_cli_selects_matching_and_defaults_to_native(self):
        from prepare_sources import main
        self.profile_patches()
        args = ['--output', str(self.output), '--decomp-only', '--decomp-revision', self.first]
        with patch('prepare_sources.ROOT', self.repo):
            self.assertEqual(main([*args, '--decomp-profile', 'matching']), 0)
            self.assertEqual(self.record()['identity']['profile'], 'matching')
            self.assertEqual((self.output / 'beachlife-decomp/src/value.cpp').read_bytes(),
                             b'reconstructed\n')
            with self.assertRaises(SystemExit) as error:
                main([*args, '--verify'])
            self.assertEqual(error.exception.code, 1)
            self.assertEqual(main(args), 0)
            self.assertEqual(self.record()['identity']['profile'], 'native')
            self.assertEqual((self.output / 'beachlife-decomp/src/value.cpp').read_bytes(), b'native\n')

    def test_export_and_manifest_tampering_cannot_be_reused(self):
        import hashlib
        import json
        self.prepare()
        target = self.output / 'beachlife-decomp/src/value.cpp'
        stamp = self.output / 'beachlife-decomp.json'
        original = stamp.read_bytes()
        target.write_bytes(b'tampered\n')
        for verify in (False, True):
            with self.subTest(verify=verify), self.assertRaisesRegex(ValueError, 'was edited'):
                self.prepare(verify=verify)
        # Even editing the manifest to agree with the altered file is rejected.
        record = json.loads(original)
        record['files']['src/value.cpp'] = hashlib.sha256(target.read_bytes()).hexdigest()
        stamp.write_text(json.dumps(record))
        with self.assertRaisesRegex(ValueError, 'manifest or files were edited'):
            self.prepare(verify=True)
        target.write_bytes(b'first\n')
        stamp.write_bytes(original)
        record = json.loads(original)
        record['source_files']['src/value.cpp'] = '0' * 64
        stamp.write_text(json.dumps(record))
        with self.assertRaisesRegex(ValueError, 'manifest or files were edited'):
            self.prepare(verify=True)
        stamp.write_bytes(original)
        target.unlink()
        with self.assertRaisesRegex(ValueError, 'was edited'):
            self.prepare(verify=True)
        target.write_bytes(b'first\n')
        (self.output / 'beachlife-decomp/extra.cpp').write_bytes(b'extra\n')
        with self.assertRaisesRegex(ValueError, 'was edited'):
            self.prepare(verify=True)

    def test_output_confined_to_build_tree_without_overwriting_source(self):
        for output in (self.repo, self.root, self.repo / 'decomp/prepared',
                       self.repo / 'extern/prepared', self.repo / 'src/prepared', self.repo / '.git/prepared'):
            with self.subTest(output=output), self.assertRaisesRegex(ValueError, 'source repository'):
                prepare(self.repo, output, (), decomp_revision=self.first)
        self.assertEqual((self.repo / 'decomp/src/value.cpp').read_bytes(), b'first\n')
        self.assertFalse((self.repo / 'extern').exists())
        for name in ('../escaped', '/absolute', 'beachlife-decomp'):
            with self.subTest(name=name), self.assertRaisesRegex(ValueError, 'Invalid dependency name'):
                prepare(self.repo, self.output, (name,))
        self.output = self.repo / 'build/prepared'
        self.patch('one.patch', 'first', 'second')
        self.write('patches/beachlife-decomp/series', b'one.patch\n')
        self.prepare()
        self.assertEqual((self.output / 'beachlife-decomp/src/value.cpp').read_bytes(), b'second\n')
        self.prepare(verify=True)
        self.assertEqual((self.repo / 'decomp/src/value.cpp').read_bytes(), b'first\n')

    def test_missing_or_unowned_output_is_not_deleted(self):
        with self.assertRaisesRegex(ValueError, 'stale'):
            self.prepare(verify=True)
        self.assertFalse(self.output.exists())
        target = self.output / 'beachlife-decomp'
        target.mkdir(parents=True)
        sentinel = target / 'preserve.txt'
        sentinel.write_bytes(b'keep\n')
        with self.assertRaisesRegex(ValueError, 'no complete directory and manifest'):
            self.prepare()
        self.assertEqual(sentinel.read_bytes(), b'keep\n')
        sentinel.unlink()
        target.rmdir()
        self.prepare()
        (self.output / 'beachlife-decomp.json').unlink()
        with self.assertRaisesRegex(ValueError, 'no complete directory and manifest'):
            self.prepare()
        self.assertEqual((target / 'src/value.cpp').read_bytes(), b'first\n')

    def test_symlinked_outputs_and_patch_inputs_are_rejected(self):
        other = self.root / 'other'
        other.mkdir()
        self.symlink(self.output, other)
        with self.assertRaisesRegex(ValueError, 'symlinked'):
            self.prepare()
        self.output.unlink()
        self.prepare()
        target = self.output / 'beachlife-decomp/src/value.cpp'
        target.unlink()
        self.symlink(target, self.repo / 'host.txt')
        with self.assertRaisesRegex(ValueError, 'Symlinks'):
            self.prepare(verify=True)
        target.unlink()
        target.write_bytes(b'first\n')
        series = self.repo / 'patches/beachlife-decomp/series'
        series.unlink()
        self.symlink(series, self.repo / 'host.txt')
        with self.assertRaisesRegex(ValueError, 'symlinked'):
            self.prepare()
        series.unlink()
        series.write_bytes(b'linked/patch.diff\n')
        linked = series.parent / 'linked'
        self.symlink(linked, other)
        (other / 'patch.diff').write_bytes(b'not applied\n')
        with self.assertRaisesRegex(ValueError, 'Invalid patch entry|symlinked'):
            self.prepare()

    def test_committed_symlinks_and_gitlinks_are_not_exported(self):
        for mode, blob in (('120000', self.git('hash-object', '-w', '--stdin', body='../host.txt\n')),
                           ('160000', self.first)):
            with self.subTest(mode=mode):
                self.git('update-index', '--add', '--cacheinfo', mode + ',' + blob + ',decomp/link')
                self.git('-c', 'user.name=Yannick Suter', '-c', 'user.email=yannick.suter@gmail.com',
                         'commit', '-qm', 'test: generated nonregular source')
                revision = self.git('rev-parse', 'HEAD')
                self.git('update-index', '--force-remove', 'decomp/link')
                self.git('-c', 'user.name=Yannick Suter', '-c', 'user.email=yannick.suter@gmail.com',
                         'commit', '-qm', 'test: remove generated nonregular source')
                with self.assertRaisesRegex(ValueError, 'Unsafe decomp source entry'):
                    self.prepare(revision)
        self.assertFalse(self.output.exists())

    def test_patch_escape_and_unsafe_series_do_not_publish(self):
        self.output.mkdir()
        sentinel = self.output / 'outside.txt'
        sentinel.write_bytes(b'outside\n')
        escaped = (b'diff --git a/../../outside.txt b/../../outside.txt\n'
                   b'--- a/../../outside.txt\n+++ b/../../outside.txt\n@@ -1 +1 @@\n-outside\n+changed\n')
        self.write('patches/beachlife-decomp/escape.patch', escaped)
        series = self.write('patches/beachlife-decomp/series', b'escape.patch\n')
        with self.assertRaises(subprocess.CalledProcessError):
            self.prepare()
        self.assertEqual(sentinel.read_bytes(), b'outside\n')
        self.assertFalse((self.output / 'beachlife-decomp').exists())
        for body in (b'../escape.patch\n', b'/absolute.patch\n', b'escape.patch\nescape.patch\n'):
            series.write_bytes(body)
            with self.subTest(body=body), self.assertRaisesRegex(ValueError, 'Invalid patch entry|Duplicate patch entry'):
                self.prepare()
