"""Boot artifacts must select retail parser checks after extraction."""
from contextlib import ExitStack
import sys
from pathlib import Path
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import check_package


class BootPackageChecks(unittest.TestCase):
    def test_profile_selects_checks(self):
        for profile in ('native', 'native-game', 'native-game-tweaks'):
            with self.subTest(profile=profile), ExitStack() as stack:
                boot = profile != 'native'
                info = {'system': 'linux', 'capability': 'launcher', 'decomp_profile': profile}
                stack.enter_context(patch.object(check_package, 'inspect_package',
                    return_value=({'build': info}, {'beachlife': (b'fixture', 0o755)})))
                checks = {name: stack.enter_context(patch.object(check_package, name, return_value=0))
                          for name in ('check', 'check_launcher', 'check_iso', 'check_startup',
                                       'check_runtime', 'check_boot_headless')}
                check_package.smoke('artifact.tar.gz')
                executable = checks['check'].call_args.args[0]
                checks['check_startup'].assert_called_once_with(executable, boot=boot)
                checks['check_runtime'].assert_called_once_with(executable, boot=boot)
                self.assertEqual(checks['check_boot_headless'].call_count, int(boot))
                if boot:
                    self.assertIn(' ', str(executable))


if __name__ == '__main__':
    unittest.main()
