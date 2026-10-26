"""Exercise loading readiness/repair and optional generated normal Play on a GPU."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile

from content_fixture import content_files, iso_files
from check_runtime import (CONFIGURATION_COMPLETED, SKIP, UNAVAILABLE, generated_configuration,
                           parse_arguments, production_configuration, production_environment,
                           rejected_configuration, resource_directory)


def check(executable, fixture=None, resources=None, *, boot=False):
    executable = str(Path(executable).resolve())
    with tempfile.TemporaryDirectory(prefix='beachlife-startup-') as directory:
        root = Path(directory)
        configuration = generated_configuration(fixture, root)
        def exercise(binary, base, generated=False):
            base.mkdir()
            retail = boot and not generated
            cabinet, expected = content_files(
                configuration=production_configuration(configuration, retail))
            original = iso_files(cabinet)
            environment = production_environment(retail, base/'profile')
            image, config, cache = base/'source.iso', base/'launcher.cfg', base/'cache'
            image.write_bytes(original)
            command = [binary, '--play', '--smoke-test', '--frames', '1',
                       '--config', str(config), '--cache-directory', str(cache)]
            if generated:
                command += ['--resources', resource_directory(executable, resources)]

            def run(*args, ready=True, first=False):
                result = subprocess.run([*command, *args], env=environment,
                                        capture_output=True, text=True, timeout=40)
                if first and result.returncode != 0 and any(reason in result.stderr for reason in UNAVAILABLE):
                    # Device creation still precedes all slow setup/cache work.
                    assert not cache.exists() and 'Game data:' not in result.stdout, result
                    if config.exists():
                        assert b'content.identity ""' in config.read_bytes(), result
                    print('Skipped loading-screen integration, no usable GPU:', result.stderr.strip())
                    return None
                if ready:
                    assert f'Game data: {len(expected)} files mounted, language en.' in result.stdout, result
                    assert 'Game data: BeachLife.ini 27 bytes read through VFS.' in result.stdout, result
                    if generated:
                        assert result.returncode == 0 and CONFIGURATION_COMPLETED in result.stdout, result
                        assert 'Game configuration: loaded through VFS.' in result.stdout, result
                        assert 'Game runtime:' in result.stdout and ' 1 frames' in result.stdout, result
                    else:
                        rejected_configuration(result, retail)
                else:
                    assert result.returncode == 1 and 'Game data:' not in result.stdout and result.stderr, result
                    assert 'Game runtime:' not in result.stdout and CONFIGURATION_COMPLETED not in result.stdout, result
                return result

            if run('--source', str(image), first=True) is None:
                return SKIP
            settings = config.read_bytes()
            assert settings.startswith(b'beachlife-launcher 4\n') and b'content.identity ""' not in settings
            prepared, = [path for path in cache.iterdir() if path.is_dir()]
            manifest = json.loads((prepared/'manifest.json').read_text())
            assert len(manifest['files']) == len(expected)
            times = {path: path.stat().st_mtime_ns for path in (prepared/'files').rglob('*') if path.is_file()}
            run()
            assert times == {path: path.stat().st_mtime_ns for path in times}
            assert config.read_bytes() == settings
            image.unlink()
            run()
            target = prepared/'files/Data/loose.txt'
            target.unlink()
            run(ready=False)
            assert config.read_bytes() == settings
            image.write_bytes(original)
            run()
            assert target.read_bytes() == expected['Data/loose.txt']
            image.write_bytes(b'broken image')
            run(ready=False)
            invalid = base/'invalid.iso'
            invalid.write_bytes(original[:2048])
            run('--source', str(invalid), ready=False)
            return 0

        if exercise(executable, root/'production') == SKIP:
            return SKIP
        if fixture is not None and exercise(str(Path(fixture).resolve()), root/'generated', generated=True) == SKIP:
            return SKIP
    print(('Retail parser rejection' if boot else 'Production rejection') +
          ', warm/offline readiness, repair and invalid-source gating passed.' +
          (' Generated normal-Play lifecycle passed.' if fixture is not None else ''))
    return 0


if __name__ == '__main__':
    sys.exit(check(**parse_arguments()))
