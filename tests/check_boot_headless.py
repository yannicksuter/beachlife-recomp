"""Require entry into the retail parser without a GPU or original game data."""
from pathlib import Path
import subprocess
import sys
import tempfile

from content_fixture import content_files, iso_files
from check_runtime import (production_configuration, production_environment,
                           rejected_configuration, write_settings)


def check(executable):
    executable = str(Path(executable).resolve())
    with tempfile.TemporaryDirectory(prefix='beachlife-boot-headless-') as directory:
        root = Path(directory)
        image, config, cache = root/'generated.iso', root/'launcher.cfg', root/'cache'
        image.write_bytes(iso_files(content_files(
            configuration=production_configuration(b'', True))[0]))
        write_settings(config, image)
        environment = production_environment(True, root/'profile')
        for key in list(environment):
            if key.startswith(('BEACHLIFE_BOOT_', 'BEACHLIFE_TWEAK_')):
                del environment[key]
        environment.update(BEACHLIFE_BOOT_USER_DIR=str(root/'profile'),
                           BEACHLIFE_BOOT_TRACE_FILES='1', BEACHLIFE_BOOT_QUIET='1',
                           SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy')
        result = subprocess.run([executable, '--play', '--headless', '--frames', '1',
                                 '--config', str(config), '--cache-directory', str(cache)],
                                cwd=root, env=environment, capture_output=True,
                                text=True, timeout=60)
        rejected_configuration(result, True)
        assert 'Game data: BeachLife.ini 27 bytes read through VFS.' in result.stdout, result
    print('Headless retail entry and three configuration reads verified.')
    return 0


if __name__ == '__main__':
    sys.exit(check(sys.argv[1]))
