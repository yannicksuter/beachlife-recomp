"""Open the game window on a real GPU with a generated ISO, run the game loop and exit cleanly.

Skipped (exit code 77) where no display or GPU device exists, such as hosted CI runners.
"""
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from content_fixture import content_files, iso_files  # noqa: E402

SKIP = 77
UNAVAILABLE = ("is not available on this computer", "Cannot start the game", "Cannot draw into the game window")


def check(executable):
    with tempfile.TemporaryDirectory(prefix='beachlife-runtime-') as temporary:
        image = Path(temporary) / 'source.iso'
        image.write_bytes(iso_files(content_files()[0]))
        config = Path(temporary) / 'launcher.cfg'
        quoted = str(image).replace('\\', '\\\\').replace('"', '\\"')
        config.write_text(f'beachlife-launcher 2\nsource.path "{quoted}"\n', encoding='utf-8')
        result = subprocess.run([str(Path(executable).resolve()), '--play', '--smoke-test', '--frames', '30',
                                 '--config', str(config), '--cache-directory', str(Path(temporary)/'cache')],
                                capture_output=True, text=True, timeout=60)
        if result.returncode != 0 and 'Cannot initialize the launcher: No available video device' in result.stderr:
            print('Skipped, no usable display here:', result.stderr.strip())
            return SKIP
        if result.returncode != 0 and any(reason in result.stderr for reason in UNAVAILABLE):
            assert 'Game data: 5 files mounted' in result.stdout, result.stdout + result.stderr
            print('Skipped, no usable display or GPU here:', result.stderr.strip())
            return SKIP
        assert result.returncode == 0, result.stdout + result.stderr
        assert 'Game data: BeachLife.ini 27 bytes read through VFS.' in result.stdout, result.stdout
        assert 'Game runtime:' in result.stdout and ' 30 frames' in result.stdout, result.stdout
        print(result.stdout.strip())
        # The same window with the performance overlay (its fonts, usage sampler and drawing).
        config.write_text(f'beachlife-launcher 4\nsource.path "{quoted}"\ndisplay.fps_overlay on\n'
                          'display.fps_overlay_corner bottom-left\n', encoding='utf-8')
        overlay = subprocess.run([str(Path(executable).resolve()), '--play', '--smoke-test', '--frames', '30',
                                  '--config', str(config), '--cache-directory', str(Path(temporary)/'cache')],
                                 capture_output=True, text=True, timeout=60)
        assert overlay.returncode == 0 and ' 30 frames' in overlay.stdout, overlay.stdout + overlay.stderr
    return 0


if __name__ == '__main__':
    sys.exit(check(sys.argv[1]))
