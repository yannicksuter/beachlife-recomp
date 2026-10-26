"""Validate and run a package extracted outside its build directory."""
import argparse
import json
import platform
import subprocess
import sys
import tempfile
from pathlib import Path
from package_build import MAC_APP, inspect_package, verify_set
from project_version import read_version

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tests'))
from check_cli import check
from check_launcher import check as check_launcher
from check_iso import check as check_iso
from check_runtime import check as check_runtime
from check_startup import check as check_startup
from check_boot_headless import check as check_boot_headless


def smoke(path):
    manifest, files = inspect_package(path)
    info = manifest['build']
    boot = info.get('decomp_profile') in ('native-game', 'native-game-tweaks')
    prefix = 'beachlife boot smoke ' if boot else 'beachlife-smoke-'
    with tempfile.TemporaryDirectory(prefix=prefix) as temp:
        root = Path(temp)
        for name, (body, mode) in files.items():
            destination = root / name
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(body)
            destination.chmod(mode)
        executable = root / ('beachlife.exe' if info['system'] == 'windows' else 'beachlife')
        if info.get('architecture') == 'universal':
            # Each half reports its own architecture; Rosetta runs the Intel one.
            executable = root / MAC_APP / 'Contents/MacOS/beachlife'
            native = 'arm64' if platform.machine() == 'arm64' else 'x86_64'
            other = 'x86_64' if native == 'arm64' else 'arm64'
            check(executable, {**info, 'architecture': native})
            if native == 'arm64':
                reported = json.loads(subprocess.check_output(
                    ['arch', '-' + other, str(executable), '--build-info'], text=True))
                assert reported == {**info, 'architecture': other}, (reported, info)
        else:
            check(executable, info)
        if info.get('capability') == 'launcher':
            check_launcher(executable)
            check_iso(executable)
            if boot:
                check_boot_headless(executable)
            if check_startup(executable, boot=boot) == 77:
                print('Game loading needs a usable graphics device; package launcher checks passed.')
            if check_runtime(executable, boot=boot) == 77:
                print('Game rendering needs a usable graphics device; runtime presentation was not verified.')
    print(f'Package startup and identity verified: {Path(path).name}')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('path', type=Path)
    parser.add_argument('--sha')
    parser.add_argument('--channel', choices=('dev', 'test', 'release'))
    parser.add_argument('--run-id')
    args = parser.parse_args()
    if args.sha:
        if not args.channel:
            parser.error('--channel is required with --sha')
        paths = verify_set(args.path, read_version(), args.sha, args.channel, args.run_id)
        print(json.dumps([p.name for p in paths], indent=2))
    else:
        smoke(args.path)
