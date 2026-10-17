"""Validate and run a package extracted outside its build directory."""
import argparse
import json
import sys
import tempfile
from pathlib import Path
from package_build import inspect_package, verify_set
from project_version import read_version

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tests'))
from check_cli import check


def smoke(path):
    manifest, files = inspect_package(path)
    info = manifest['build']
    with tempfile.TemporaryDirectory(prefix='beachlife-smoke-') as temp:
        root = Path(temp)
        for name, (body, mode) in files.items():
            destination = root / name
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(body)
            destination.chmod(mode)
        executable = root / ('beachlife.exe' if info['system'] == 'windows' else 'beachlife')
        check(executable, info)
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
