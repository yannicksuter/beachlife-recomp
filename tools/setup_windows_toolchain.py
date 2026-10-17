"""Download and verify the pinned Linux-hosted Windows cross compiler."""
import argparse
import hashlib
import json
import subprocess
import urllib.request
from pathlib import Path


def setup(destination):
    pin = json.loads(Path(__file__).with_name('windows-toolchain.json').read_text())
    destination.mkdir(parents=True, exist_ok=True)
    archive = destination / pin['archive']
    if not archive.exists():
        temporary = archive.with_suffix(archive.suffix + '.download')
        urllib.request.urlretrieve(pin['url'], temporary)
        temporary.replace(archive)
    if hashlib.sha256(archive.read_bytes()).hexdigest() != pin['sha256']:
        raise ValueError('LLVM-MinGW archive checksum mismatch')
    root = destination / pin['archive'].removesuffix('.tar.xz')
    subprocess.run(['tar', '-xf', str(archive), '-C', str(destination)], check=True)
    if not (root / 'bin/x86_64-w64-mingw32-clang++').is_file():
        raise ValueError('Missing cross compiler in the verified archive')
    return root.resolve()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('destination', type=Path)
    args = parser.parse_args()
    print(setup(args.destination))
