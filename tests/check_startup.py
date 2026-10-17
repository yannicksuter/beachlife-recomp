"""Exercise Play's loading screen, saved readiness and VFS on an available GPU.

The content_startup fixture covers the same worker without a graphics device.
"""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

from content_fixture import content_files, iso_files
from check_runtime import SKIP, UNAVAILABLE


def check(executable):
    executable = str(Path(executable).resolve())
    with tempfile.TemporaryDirectory(prefix='beachlife-startup-') as directory:
        base = Path(directory)
        image, config, cache = base/'source.iso', base/'launcher.cfg', base/'cache'
        original = iso_files(content_files()[0])
        image.write_bytes(original)
        environment = dict(os.environ)
        command = [executable, '--play', '--smoke-test', '--frames', '1',
                   '--config', str(config), '--cache-directory', str(cache)]

        def run(*args, ready=True, first=False):
            result = subprocess.run([*command, *args], env=environment, capture_output=True, text=True, timeout=40)
            if first and result.returncode != 0 and any(reason in result.stderr for reason in UNAVAILABLE):
                # The graphics device must precede all slow setup work. A machine
                # without it must not create a cache or persist false readiness.
                assert not cache.exists() and 'Game data:' not in result.stdout, result
                if config.exists():
                    assert b'content.identity ""' in config.read_bytes(), result
                print('Skipped loading-screen integration, no usable GPU:', result.stderr.strip())
                return None
            if ready:
                assert 'Game data: 5 files mounted, language en.' in result.stdout, result
                assert result.returncode == 0, result
                assert 'Game data: BeachLife.ini 27 bytes read through VFS.' in result.stdout, result
                assert 'Game runtime:' in result.stdout and ' 1 frames' in result.stdout, result
            else:
                assert result.returncode == 1 and 'Game data:' not in result.stdout and result.stderr, result
            return result

        if run('--source', str(image), first=True) is None:
            return SKIP
        settings = config.read_bytes()
        assert settings.startswith(b'beachlife-launcher 4\n') and b'content.identity ""' not in settings
        root, = [p for p in cache.iterdir() if p.is_dir()]
        manifest = json.loads((root/'manifest.json').read_text())
        assert len(manifest['files']) == 5
        times = {p: p.stat().st_mtime_ns for p in (root/'files').rglob('*') if p.is_file()}
        run() # warm launch uses prepared files without rewriting them
        assert times == {p: p.stat().st_mtime_ns for p in times}
        assert config.read_bytes() == settings
        image.unlink()
        run() # same handoff with the ISO disconnected
        target = root/'files/Data/loose.txt'
        target.unlink()
        run(ready=False) # offline damaged data must not reach the runtime
        assert config.read_bytes() == settings
        image.write_bytes(original)
        run() # Play repairs the prepared set from the returned image
        assert target.read_bytes() == content_files()[1]['Data/loose.txt']
        image.write_bytes(b'broken image')
        run(ready=False) # replacing the source cannot inherit old readiness
        invalid = base/'invalid.iso'
        invalid.write_bytes(original[:2048])
        run('--source', str(invalid), ready=False)
    print('Play setup, warm/offline handoff, persisted readiness, repair and invalid-source gating passed.')
    return 0


if __name__ == '__main__': sys.exit(check(sys.argv[1]))
