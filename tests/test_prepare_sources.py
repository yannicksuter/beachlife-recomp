import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

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
                patch = f'diff --git a/value.txt b/value.txt\n--- a/value.txt\n+++ b/value.txt\n@@ -1 +1 @@\n-{before}\n+{after}\n'
                (patches / name).write_bytes(patch.encode())
            (patches / 'series').write_bytes(b'one.patch\ntwo.patch\n')
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
