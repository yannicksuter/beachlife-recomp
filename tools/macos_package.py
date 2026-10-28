"""The Mac release: one self-contained universal Beach Life.app.

stage  joins the arm64 and x86_64 packages into the app (unsigned) in a folder
pack   packages the signed app for the launcher's updates (macos-universal)
dmg    makes the disk image players install from: the app and an Applications link

Signing and notarization happen in between with tools/sign_macos.py.
"""
import argparse
import json
from pathlib import Path
import subprocess
import sys
import tempfile

from package_build import (MAC_APP, MAC_PACKAGE_ID, digest, inspect_package, mac_signing,
                           payload_names, universal_name, write_archive)

EXECUTABLE = 'beachlife.app/Contents/MacOS/beachlife'
IDENTITY = 'universal.json'


def stage(arm64, x86_64, folder, merge=None):
    """Writes the universal app to folder/Beach Life.app and its identity beside it.
    `merge` joins the two executables (lipo by default)."""
    packages = {}
    for path in (arm64, x86_64):
        manifest, files = inspect_package(path)
        packages[manifest['build']['architecture']] = (manifest, files)
    if set(packages) != {'arm64', 'x86_64'}:
        raise ValueError('Expected the arm64 and the x86_64 macOS package')
    (manifest, files), (other, other_files) = packages['arm64'], packages['x86_64']

    def without_architecture(info):
        return {key: value for key, value in info.items() if key != 'architecture'}

    if (without_architecture(manifest['build']) != without_architecture(other['build'])
            or manifest.get('game_traps') != other.get('game_traps')):
        raise ValueError('The macOS packages come from different builds')
    skip = {EXECUTABLE, 'beachlife', 'SOURCE-MANIFEST.json', *mac_signing(manifest['build'])}
    names = set(files) - skip
    if names != set(other_files) - skip or any(files[name] != other_files[name] for name in names):
        raise ValueError('The macOS packages differ beyond their executables')
    folder = Path(folder)
    if folder.exists():
        raise ValueError(f'The staging folder exists already: {folder}')
    for name in names:
        body, mode = files[name]
        destination = folder / universal_name(name)
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_bytes(body)
        destination.chmod(mode)
    (folder / MAC_PACKAGE_ID).write_text('macos-universal\n')
    (folder / MAC_PACKAGE_ID).chmod(0o644)
    binary = folder / universal_name(EXECUTABLE)
    binary.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory() as temp:
        slices = []
        for architecture in ('arm64', 'x86_64'):
            path = Path(temp) / architecture
            path.write_bytes(packages[architecture][1][EXECUTABLE][0])
            slices.append(path)
        (merge or lipo)(slices, binary)
    binary.chmod(0o755)
    identity = {key: manifest[key] for key in ('dependencies', 'patches', 'game_traps') if key in manifest}
    identity['build'] = {**manifest['build'], 'architecture': 'universal'}
    (folder / IDENTITY).write_text(json.dumps(identity, indent=2, sort_keys=True) + '\n')
    return folder / MAC_APP


def lipo(slices, output):
    subprocess.run(['lipo', '-create', *map(str, slices), '-output', str(output)], check=True)


def pack(folder, output):
    """The signed app as the macos-universal package the launcher updates from."""
    folder = Path(folder)
    identity = json.loads((folder / IDENTITY).read_text())
    info = identity['build']
    data, modes = {}, {}
    for path in sorted((folder / MAC_APP).rglob('*')):
        if path.is_symlink():
            raise ValueError(f'Symlink in the app: {path}')
        if path.is_file():
            name = path.relative_to(folder).as_posix()
            data[name], modes[name] = path.read_bytes(), path.stat().st_mode & 0o777
    expected = payload_names(info) | {name for name in mac_signing(info) if name in data}
    if set(data) != expected:
        raise ValueError(f'Unexpected app contents: {sorted(set(data) ^ expected)}')
    manifest = {'format': 1, 'build': info, 'dependencies': identity.get('dependencies', []),
                'patches': identity.get('patches', []),
                'files': {name: {'sha256': digest(body), 'bytes': len(body), 'mode': modes[name]}
                          for name, body in sorted(data.items())}}
    if 'game_traps' in identity:
        manifest['game_traps'] = identity['game_traps']
    data['SOURCE-MANIFEST.json'] = (json.dumps(manifest, indent=2, sort_keys=True) + '\n').encode()
    modes['SOURCE-MANIFEST.json'] = 0o644
    destination = write_archive(Path(output), info, data, modes)
    inspect_package(destination)
    return destination


def dmg(folder, output):
    """The disk image: the app beside a link to Applications, to drag it onto."""
    folder, output = Path(folder), Path(output)
    info = json.loads((folder / IDENTITY).read_text())['build']
    destination = output / f'beachlife-{info["build_version"]}-macos-universal.dmg'
    output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory() as temp:
        root = Path(temp) / 'Beach Life'
        root.mkdir()
        # ditto keeps the signature and the stapled ticket exactly as they are. The
        # tools report to the log: this command's output is only the image's path.
        subprocess.run(['ditto', str(folder / MAC_APP), str(root / MAC_APP)], check=True,
                       stdout=sys.stderr)
        (root / 'Applications').symlink_to('/Applications')
        subprocess.run(['hdiutil', 'create', '-volname', 'Beach Life', '-srcfolder', str(root),
                        '-fs', 'HFS+', '-format', 'UDZO', '-ov', str(destination)], check=True,
                       stdout=sys.stderr)
    return destination


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest='command', required=True)
    joined = commands.add_parser('stage')
    joined.add_argument('arm64', type=Path)
    joined.add_argument('x86_64', type=Path)
    joined.add_argument('folder', type=Path)
    for name in ('pack', 'dmg'):
        command = commands.add_parser(name)
        command.add_argument('folder', type=Path)
        command.add_argument('output', type=Path)
    args = parser.parse_args()
    if args.command == 'stage':
        print(stage(args.arm64, args.x86_64, args.folder))
    elif args.command == 'pack':
        print(pack(args.folder, args.output))
    else:
        print(dmg(args.folder, args.output))
