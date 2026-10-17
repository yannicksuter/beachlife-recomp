"""Exercise the real window and packaged resources without a desktop or game."""
import os
import subprocess
import sys
import tempfile
from pathlib import Path


def check(executable):
    with tempfile.TemporaryDirectory(prefix='beachlife-window-') as temporary:
        env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_RENDER_DRIVER='software')
        result = subprocess.run([str(Path(executable).resolve()), '--smoke-test', '--config',
                                 str(Path(temporary) / 'launcher.cfg')], env=env,
                                capture_output=True, text=True, timeout=45)
        assert result.returncode == 0, result.stdout + result.stderr
        assert 'Launcher window, artwork, fonts and pages verified.' in result.stdout, result.stdout
        assert not (Path(temporary) / 'launcher.cfg').exists(), 'Smoke must not create user preferences'


if __name__ == '__main__':
    check(sys.argv[1])
