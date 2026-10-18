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
        self.assertEqual(record['identity']['series']['sha256'], hashlib.sha256(series.read_bytes()).hexdigest())
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
