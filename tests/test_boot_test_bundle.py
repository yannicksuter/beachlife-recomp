"""Check that cross-built CTest invocations keep paths, skips and environments."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import boot_test_bundle


class NativeTestBundle(unittest.TestCase):
    def test_relocated_ctest_runs_and_skips(self):
        with tempfile.TemporaryDirectory(prefix='native test bundle ') as temp:
            root = Path(temp)
            source, bundle = root/'source', root/'build'
            source.mkdir()
            bundle.mkdir()
            script = source/'check.py'
            script.write_text('import os, sys\n'
                              'from pathlib import Path\n'
                              'assert os.environ["BUNDLE_FIXTURE"] == "value with spaces"\n'
                              'assert Path(sys.argv[1]).is_file()\n'
                              'sys.exit(int(sys.argv[2]))\n')
            (bundle/'file.txt').write_text('fixture')
            tests = []
            for name, status in (('pass', 0), ('skip', 77)):
                tests.append({'name': name,
                    'command': ['/old/python3.12', '/old/src/check.py', '/old/build/file.txt', str(status)],
                    'properties': [
                        {'name': 'WORKING_DIRECTORY', 'value': '/old/src'},
                        {'name': 'ENVIRONMENT', 'value': ['BUNDLE_FIXTURE=value with spaces']},
                        {'name': 'SKIP_RETURN_CODE', 'value': 77},
                        {'name': 'TIMEOUT', 'value': 10.0}]})
            (bundle/'tests.json').write_text(json.dumps({
                'source': '/old/src', 'build': '/old/build', 'tests': tests}))
            with patch.object(boot_test_bundle, 'ROOT', source):
                boot_test_bundle.configure(bundle)
            result = subprocess.run(['ctest', '--test-dir', str(bundle), '--output-on-failure'],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn('100% tests passed', result.stdout)
            self.assertIn('Skipped', result.stdout)

    def test_windows_paths_and_list_values(self):
        self.assertEqual(boot_test_bundle.relocate(['/old/build/a.exe', '/old/src/test.py'],
            '/old/src', '/old/build', r'C:\source', r'D:\test dir'),
            'D:/test dir/a.exe;C:/source/test.py')
        self.assertEqual(boot_test_bundle.relocate('/old/build-other/a', '/old/src', '/old/build',
            'source', 'build'), '/old/build-other/a')


if __name__ == '__main__':
    unittest.main()
