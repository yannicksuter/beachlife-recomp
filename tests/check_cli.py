"""Check the actual executable's public command-line and embedded identity."""
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path


def check(executable, expected):
    executable = str(Path(executable).resolve())
    for arguments in (["--help"],):
        output = subprocess.check_output([executable, *arguments], text=True)
        assert "loads the supported game configuration" in output, output
        assert "gameplay are still in development" in output, output
    version = subprocess.check_output([executable, "--version"], text=True).strip()
    assert version == expected["build_version"], (version, expected)
    info = json.loads(subprocess.check_output([executable, "--build-info"], text=True))
    assert info == expected, (info, expected)
    for arguments in (["--unknown"], ["--help", "extra"],
                      ["--preview-screen", "Data/Screens/FrontScreen.tga"],
                      ["--play", "--preview-screen", "../outside.tga"],
                      ["--play", "--preview-screen", ""]):
        result = subprocess.run([executable, *arguments], capture_output=True, text=True)
        assert result.returncode == 2 and result.stderr, result
    # Unreadable settings must stop --play before any window/device is needed,
    # including when --source would otherwise replace the saved choice.
    with tempfile.TemporaryDirectory(prefix='beachlife-cli-') as temporary:
        root = Path(temporary)
        config, image = root/'launcher.cfg', root/'source.iso'
        newer = b'beachlife-launcher 5\nfuture.setting on\n'
        config.write_bytes(newer)
        image.write_bytes(b'generated source; settings must be rejected before reading it')
        environment = dict(os.environ, SDL_VIDEODRIVER='invalid-cli-test-driver')
        command = [executable, '--play', '--smoke-test', '--frames', '1',
                   '--config', str(config), '--cache-directory', str(root/'cache')]
        for extra in ((), ('--source', str(image))):
            result = subprocess.run([*command, *extra], env=environment, capture_output=True, text=True, timeout=30)
            assert result.returncode == 1 and 'Cannot read launcher settings' in result.stderr, result
            assert 'Game data:' not in result.stdout and config.read_bytes() == newer, result
            assert not (root/'cache').exists(), 'Rejected settings must not prepare game data'


if __name__ == "__main__":
    check(sys.argv[1], json.loads(Path(sys.argv[2]).read_text()))
