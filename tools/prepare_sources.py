"""Export immutable dependency pins, apply ordered patches and verify the result.

Configuration never downloads dependencies. Run setup_dependencies.py first.
Prepared files belong to the build tree; dependency checkouts remain untouched.
"""
import argparse
import hashlib
import json
import os
import shutil
import subprocess
import tarfile
import tempfile
from pathlib import Path, PurePosixPath

ROOT = Path(__file__).resolve().parents[1]
DEPENDENCIES = ('sdl', 'imgui', 'stb', 'unshield', 'zlib', 'freetype')


def git(root, *args):
    return subprocess.check_output(['git', '-C', str(root), *args], text=True).strip()


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def tree_manifest(root):
    files = {}
    for path in sorted(Path(root).rglob('*')):
        if path.is_symlink():
            raise ValueError(f'Symlinks are not prepared inputs: {path}')
        if path.is_file():
            files[path.relative_to(root).as_posix()] = sha256(path)
    return files


def patch_paths(source, name):
    folder = Path(source) / 'patches' / name
    series = folder / 'series'
    result = []
    if series.exists():
        for line in series.read_text().splitlines():
            line = line.strip()
            if not line or line.startswith('#'):
                continue
            path = folder / line
            if (Path(line).is_absolute() or '..' in Path(line).parts or path.is_symlink()
                    or not path.is_file() or not path.resolve().is_relative_to(folder.resolve())):
                raise ValueError(f'Invalid patch entry: {line}')
            result.append(path)
    return result


def inputs(source, name):
    submodule = Path(source) / 'extern' / name
    line = git(source, 'ls-files', '--stage', '--', 'extern/' + name)
    parts = line.split()
    if len(parts) != 4 or parts[0] != '160000' or parts[2] != '0':
        raise ValueError(f'Missing immutable gitlink for {name}')
    commit = parts[1]
    if git(submodule, 'rev-parse', 'HEAD') != commit:
        raise ValueError(f'{name} is not at the pinned revision; run tools/setup_dependencies.py')
    if git(submodule, 'status', '--porcelain', '--untracked-files=all'):
        raise ValueError(f'{name} has local edits; prepared builds require the clean pin')
    patches = patch_paths(source, name)
    identity = {'name': name, 'commit': commit,
                'patches': [{'path': p.relative_to(source).as_posix(), 'sha256': sha256(p)} for p in patches]}
    return submodule, identity, patches


def prepare(source, output, names=DEPENDENCIES, verify=False):
    source, output = Path(source).resolve(), Path(output).resolve()
    if output == source or source.is_relative_to(output):
        raise ValueError('Prepared output cannot contain the source repository')
    output.mkdir(parents=True, exist_ok=True)
    identities = []
    for name in names:
        submodule, identity, patches = inputs(source, name)
        destination = output / name
        stamp = output / (name + '.json')
        if destination.is_symlink() or stamp.is_symlink():
            raise ValueError('Prepared output cannot be symlinked')
        previous = json.loads(stamp.read_text()) if stamp.exists() else None
        if previous and destination.is_dir():
            if tree_manifest(destination) != previous['files']:
                raise ValueError(f'Prepared {name} was edited; remove its prepared directory and stamp to regenerate')
            if previous['identity'] == identity:
                identities.append(identity)
                continue
        if verify:
            raise ValueError(f'Prepared {name} is stale; reconfigure the build')
        with tempfile.TemporaryDirectory(dir=output, prefix=name + '-') as temporary:
            temporary = Path(temporary)
            archive = temporary / 'source.tar'
            subprocess.run(['git', '-C', str(submodule), '-c', 'core.autocrlf=false',
                            '-c', 'core.eol=lf', 'archive', identity['commit'],
                            '-o', str(archive)], check=True)
            staged = temporary / 'source'
            staged.mkdir()
            with tarfile.open(archive) as handle:
                for member in handle:
                    path = PurePosixPath(member.name)
                    if path.is_absolute() or '..' in path.parts or not (member.isdir() or member.isfile()):
                        raise ValueError(f'Unsafe dependency archive entry: {member.name}')
                    target = staged / member.name
                    if member.isdir():
                        target.mkdir(parents=True, exist_ok=True)
                    else:
                        target.parent.mkdir(parents=True, exist_ok=True)
                        target.write_bytes(handle.extractfile(member).read())
                        target.chmod(member.mode & 0o777)
            for patch in patches:
                subprocess.run(['git', '-c', 'core.autocrlf=false', '-c', 'core.eol=lf',
                                'apply', '--no-index', str(patch)], cwd=staged, check=True)
            record = {'identity': identity, 'files': tree_manifest(staged)}
            if destination.exists():
                shutil.rmtree(destination)
            os.replace(staged, destination)
            stamp.write_text(json.dumps(record, indent=2, sort_keys=True) + '\n')
        identities.append(identity)
    return identities


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--verify', action='store_true')
    args = parser.parse_args()
    try:
        prepare(ROOT, args.output, verify=args.verify)
    except (ValueError, subprocess.CalledProcessError) as error:
        raise SystemExit(str(error)) from error
