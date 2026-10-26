"""Check production rejection/preview and optional generated normal Play on a GPU.

The generated runtime executable shares the production loading flow, but supplies
its private fixture identity. Package callers need only the production executable.
"""
import argparse
import os
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from content_fixture import content_files, iso_files  # noqa: E402

SKIP = 77
UNAVAILABLE = ("is not available on this computer", "Cannot start the game", "Cannot draw into the game window",
               "Cannot initialize the launcher: No available video device")
CONFIGURATION_COMPLETED = 'RUNTIME_CONFIGURATION_SUCCESS caller_thread=1 calls=1 cleanup=1'
BOOT_CONFIGURATION_READ = 'Frame 0: opened data/script/gameparameters.pis\n'


def production_configuration(configuration, boot):
    # Retail rejects a numeric boolean in StartupConfig, before constructing Game.
    # A partial but well-typed host fixture is not a complete retail configuration.
    return b"'bFixture' 123\r\n".ljust(1242, b'\r') if boot else configuration


def production_environment(boot, directory):
    environment = dict(os.environ)
    if boot:
        environment.update(BEACHLIFE_BOOT_USER_DIR=str(directory), BEACHLIFE_BOOT_TRACE_FILES='1')
    return environment


def parse_arguments():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable')
    parser.add_argument('fixture', nargs='?')
    parser.add_argument('resources', nargs='?')
    parser.add_argument('--boot', action='store_true', help='check the retail entry/parser rejection')
    return vars(parser.parse_args())


def generated_configuration(fixture, directory):
    if fixture is None:
        # Deliberately unsupported input for the fixed production identity.
        return b'\r' * 1242
    target = directory / 'generated-configuration.pis'
    result = subprocess.run([str(Path(fixture).resolve()), '--write-configuration', str(target)],
                            capture_output=True, text=True, timeout=30)
    assert result.returncode == 0 and 'GENERATED_CONFIGURATION bytes=1242' in result.stdout, result
    value = target.read_bytes()
    assert len(value) == 1242
    return value


def resource_directory(executable, resources):
    if resources is not None:
        return str(Path(resources).resolve())
    parent = Path(executable).resolve().parent
    if parent.name == 'MacOS' and parent.parent.name == 'Contents':
        return str(parent.parent / 'Resources' / 'assets' / 'launcher')
    return str(parent / 'assets' / 'launcher')


def unsupported_configuration(result):
    assert result.returncode == 1, result
    assert 'The selected game configuration is not supported.' in result.stderr, result
    assert 'Game runtime:' not in result.stdout and 'Game configuration: loaded' not in result.stdout, result


def rejected_configuration(result, boot):
    if not boot:
        unsupported_configuration(result)
        return
    # StartupFatal retains retail's exit(0). A zero exit alone is not evidence:
    # require the actual WinMain configuration reads and no later Game/host loop.
    assert result.returncode == 0, result
    assert result.stderr.count(BOOT_CONFIGURATION_READ) == 3, result
    assert 'opened beachlife.ini' not in result.stderr and 'Beach Life:' not in result.stderr, result
    assert 'Game runtime:' not in result.stdout and 'Game configuration: loaded' not in result.stdout, result
    assert CONFIGURATION_COMPLETED not in result.stdout, result


def write_settings(config, image, *, overlay=False):
    quoted = str(image).replace('\\', '\\\\').replace('"', '\\"')
    if overlay:
        content = (f'beachlife-launcher 4\nsource.path "{quoted}"\ndisplay.fps_overlay on\n'
                   'display.fps_overlay_corner bottom-left\n')
    else:
        content = f'beachlife-launcher 2\nsource.path "{quoted}"\n'
    config.write_text(content, encoding='utf-8', newline='\n')


def check(executable, fixture=None, resources=None, *, boot=False):
    executable = str(Path(executable).resolve())
    with tempfile.TemporaryDirectory(prefix='beachlife-runtime-') as temporary:
        root = Path(temporary)
        configuration = generated_configuration(fixture, root)
        payload = iso_files(content_files(screen=True,
            configuration=production_configuration(configuration, boot))[0])
        production = root / 'production'
        production.mkdir()
        image, config, cache = production/'source.iso', production/'launcher.cfg', production/'cache'
        image.write_bytes(payload)
        write_settings(config, image)
        environment = production_environment(boot, production/'profile')
        command = [executable, '--play', '--smoke-test', '--config', str(config), '--cache-directory', str(cache)]
        result = subprocess.run([*command, '--frames', '1'], env=environment,
                                capture_output=True, text=True, timeout=60)
        if result.returncode != 0 and any(reason in result.stderr for reason in UNAVAILABLE):
            assert 'Game data:' not in result.stdout and not cache.exists(), result.stdout + result.stderr
            print('Skipped, no usable display or GPU here:', result.stderr.strip())
            return SKIP
        rejected_configuration(result, boot)
        assert 'Game data: BeachLife.ini 27 bytes read through VFS.' in result.stdout, result

        # Production preview remains independent of the game configuration owner.
        # Exercise overlay drawing, then the same selected asset offline.
        write_settings(config, image, overlay=True)
        for offline in (False, True):
            if offline:
                image.unlink()
            preview = subprocess.run([*command, '--frames', '2', '--preview-screen', 'Data/Screens/FrontScreen.tga'],
                                     env=environment, capture_output=True, text=True, timeout=60)
            assert preview.returncode == 0 and ' 2 frames' in preview.stdout, preview
            assert 'Screen preview: Data/Screens/FrontScreen.tga, 4 x 2 pixels read through VFS.' in preview.stdout, preview
            assert 'Game configuration: loaded' not in preview.stdout, preview
        missing = subprocess.run([*command, '--frames', '1', '--preview-screen', 'Data/Screens/missing.tga'],
                                 env=environment, capture_output=True, text=True, timeout=60)
        assert missing.returncode == 1 and 'Game runtime:' not in missing.stdout and missing.stderr, missing

        if fixture is not None:
            generated = root / 'generated'
            generated.mkdir()
            image, config, cache = generated/'source.iso', generated/'launcher.cfg', generated/'cache'
            image.write_bytes(iso_files(content_files(screen=True, configuration=configuration)[0]))
            command = [str(Path(fixture).resolve()), '--play', '--smoke-test', '--frames', '30',
                       '--config', str(config), '--cache-directory', str(cache),
                       '--resources', resource_directory(executable, resources)]
            for overlay in (False, True):
                write_settings(config, image, overlay=overlay)
                result = subprocess.run(command, capture_output=True, text=True, timeout=60)
                assert result.returncode == 0 and CONFIGURATION_COMPLETED in result.stdout, result
                assert 'Game configuration: loaded through VFS.' in result.stdout, result
                assert 'Game data: BeachLife.ini 27 bytes read through VFS.' in result.stdout, result
                assert 'Game runtime:' in result.stdout and ' 30 frames' in result.stdout, result
    print(('Retail parser rejection' if boot else 'Production configuration rejection') +
          ' and online/offline previews passed.' +
          (' Generated normal Play and overlay lifecycle passed.' if fixture is not None else ''))
    return 0


if __name__ == '__main__':
    sys.exit(check(**parse_arguments()))
