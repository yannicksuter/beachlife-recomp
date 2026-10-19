"""Export pinned dependencies or a committed local decomp tree and apply patches.

Configuration never downloads inputs. Run setup_dependencies.py for dependencies.
Prepared files belong to the build tree; source checkouts remain untouched.
"""
import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import tarfile
import tempfile
from pathlib import Path, PurePosixPath

ROOT = Path(__file__).resolve().parents[1]
DEPENDENCIES = ('sdl', 'imgui', 'stb', 'unshield', 'zlib', 'freetype')
DECOMP = 'beachlife-decomp'
DECOMP_PROFILES = {
    'native': ('reconstruction-series', 'series'),
    'matching': ('reconstruction-series',),
    'native-experimental': ('reconstruction-series', 'experimental-series', 'series'),
}


def git(root, *args):
    return subprocess.check_output(['git', '--no-replace-objects', '-C', str(root), *args], text=True).strip()


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


def no_symlinks(path, root):
    while path != root:
        if path.is_symlink():
            raise ValueError(f'Prepared paths cannot be symlinked: {path}')
        path = path.parent


def patch_paths(source, name, series_filename='series'):
    if series_filename not in ('series', 'reconstruction-series', 'experimental-series'):
        raise ValueError('Invalid patch series filename')
    folder = Path(source) / 'patches' / name
    series = folder / series_filename
    no_symlinks(series, Path(source))
    result = []
    if series.exists():
        if not series.is_file():
            raise ValueError(f'Invalid patch series: {series}')
        for line in series.read_text(encoding='utf-8').splitlines():
            line = line.strip()
            if not line or line.startswith('#'):
                continue
            path = folder / line
            if (Path(line).is_absolute() or '\\' in line or ':' in line or '\0' in line
                    or '..' in Path(line).parts or path.is_symlink()
                    or not path.is_file() or not path.resolve().is_relative_to(folder.resolve())):
                raise ValueError(f'Invalid patch entry: {line}')
            no_symlinks(path, Path(source))
            if path in result:
                raise ValueError(f'Duplicate patch entry: {line}')
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


def decomp_inputs(source, revision, decomp_profile='native'):
    if decomp_profile not in DECOMP_PROFILES:
        raise ValueError('Invalid decomp profile; choose ' + ', '.join(DECOMP_PROFILES))
    if not isinstance(revision, str) or not re.fullmatch(r'(?:[0-9a-f]{40}|[0-9a-f]{64})', revision):
        raise ValueError('The decomp revision must be a full local commit SHA')
    if Path(git(source, 'rev-parse', '--show-toplevel')).resolve() != source:
        raise ValueError('The decomp source must be this repository root')
    if git(source, 'cat-file', '-t', revision) != 'commit':
        raise ValueError('The decomp revision must identify a commit')
    if git(source, 'rev-parse', '--verify', revision + '^{commit}') != revision:
        raise ValueError('The decomp revision must be a full local commit SHA, not a named ref')
    if git(source, 'status', '--porcelain', '--untracked-files=all', '--', 'decomp'):
        raise ValueError('decomp/ has local edits; commit or remove them before preparing source')
    tree = git(source, 'rev-parse', revision + ':decomp')
    if git(source, 'cat-file', '-t', tree) != 'tree':
        raise ValueError('The selected commit has no decomp/ source tree')
    files = {}
    entries = subprocess.check_output(['git', '--no-replace-objects', '-C', str(source), 'ls-tree', '-rz', tree])
    for entry in entries.split(b'\0'):
        if not entry:
            continue
        metadata, raw_name = entry.split(b'\t', 1)
        mode, kind, blob = metadata.decode('ascii').split()
        name = raw_name.decode('utf-8')
        path = PurePosixPath(name)
        if (mode not in ('100644', '100755') or kind != 'blob' or path.is_absolute()
                or '..' in path.parts or '.git' in path.parts or '\\' in name or ':' in name):
            raise ValueError(f'Unsafe decomp source entry: {name}')
        body = subprocess.check_output(['git', '--no-replace-objects', '-C', str(source), 'cat-file', 'blob', blob])
        files[name] = hashlib.sha256(body).hexdigest()
    if not files:
        raise ValueError('The selected commit has no decomp source files')
    series_names = DECOMP_PROFILES[decomp_profile]
    patches, series, seen = [], [], set()
    for filename in series_names:
        for path in patch_paths(source, DECOMP, filename):
            resolved = path.resolve()
            if resolved in seen:
                raise ValueError(f'Duplicate patch entry across series: {path.name}')
            seen.add(resolved)
            patches.append(path)
        path = source / 'patches' / DECOMP / filename
        series.append({'path': path.relative_to(source).as_posix(),
                       'sha256': sha256(path) if path.is_file() else None})
    identity = {'name': DECOMP, 'commit': revision, 'subtree': 'decomp', 'tree': tree,
                'profile': decomp_profile, 'series': series,
                'patches': [{'path': p.relative_to(source).as_posix(), 'sha256': sha256(p)} for p in patches]}
    return identity, patches, files


def export_tree(repository, revision, archive, staged):
    subprocess.run(['git', '--no-replace-objects', '-C', str(repository), '-c', 'core.autocrlf=false',
                    '-c', 'core.eol=lf', 'archive', revision, '-o', str(archive)], check=True)
    staged.mkdir()
    with tarfile.open(archive) as handle:
        for member in handle:
            path = PurePosixPath(member.name)
            if (path.is_absolute() or '..' in path.parts or '.git' in path.parts or '\\' in member.name
                    or ':' in member.name or not (member.isdir() or member.isfile())):
                raise ValueError(f'Unsafe source archive entry: {member.name}')
            target = staged / member.name
            if member.isdir():
                target.mkdir(parents=True, exist_ok=True)
            else:
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(handle.extractfile(member).read())
                target.chmod(member.mode & 0o777)


def prepare(source, output, names=DEPENDENCIES, verify=False, decomp_revision=None,
            decomp_profile='native'):
    source, requested_output = Path(source).resolve(), Path(output).absolute()
    if requested_output.is_symlink():
        raise ValueError('Prepared output cannot be symlinked')
    output = requested_output.resolve()
    if output == source or source.is_relative_to(output):
        raise ValueError('Prepared output cannot contain the source repository')
    if output.is_relative_to(source) and not output.is_relative_to(source / 'build'):
        raise ValueError('Prepared output inside the source repository must be under build/')
    if requested_output.is_relative_to(source):
        no_symlinks(requested_output, source)
    names = tuple(names)
    if any(not re.fullmatch(r'[a-z0-9][a-z0-9-]*', name) or name == DECOMP for name in names):
        raise ValueError('Invalid dependency name; use decomp_revision for the local export')
    if len(set(names)) != len(names):
        raise ValueError('Duplicate prepared dependency name')
    if decomp_profile not in DECOMP_PROFILES:
        raise ValueError('Invalid decomp profile; choose ' + ', '.join(DECOMP_PROFILES))
    local = decomp_inputs(source, decomp_revision, decomp_profile) if decomp_revision is not None else None
    if not verify:
        output.mkdir(parents=True, exist_ok=True)
    identities = []
    for name in (*names, *((DECOMP,) if local else ())):
        source_files = None
        if name == DECOMP:
            identity, patches, source_files = local
            repository, revision = source, identity['tree']
        else:
            repository, identity, patches = inputs(source, name)
            revision = identity['commit']
        destination = output / name
        stamp = output / (name + '.json')
        if destination.is_symlink() or stamp.is_symlink():
            raise ValueError('Prepared output cannot be symlinked')
        previous = None
        if destination.exists() or stamp.exists():
            if not destination.is_dir() or not stamp.is_file():
                raise ValueError(f'Prepared {name} has no complete directory and manifest; remove both to regenerate')
            previous = json.loads(stamp.read_text())
            if not isinstance(previous, dict) or not isinstance(previous.get('files'), dict):
                raise ValueError(f'Invalid prepared {name} manifest')
            if tree_manifest(destination) != previous['files']:
                raise ValueError(f'Prepared {name} was edited; remove its prepared directory and stamp to regenerate')
            if previous.get('identity') == identity and source_files is None:
                identities.append(identity)
                continue
        if verify and (not previous or previous.get('identity') != identity):
            raise ValueError(f'Prepared {name} is stale; reconfigure the build')
        with tempfile.TemporaryDirectory(dir=output, prefix=name + '-') as temporary:
            temporary = Path(temporary)
            staged = temporary / 'source'
            export_tree(repository, revision, temporary / 'source.tar', staged)
            if source_files is not None and tree_manifest(staged) != source_files:
                raise ValueError('The decomp archive differs from its committed blobs; check export attributes')
            # A nested working directory otherwise makes Git silently skip patch
            # paths outside that directory's prefix in the enclosing repository.
            # A temporary repository also enforces Git's patch path confinement.
            if patches:
                subprocess.run(['git', '--no-replace-objects', 'init', '--quiet', '--template=', str(staged)], check=True)
            for index, patch in enumerate(patches):
                body = patch.read_bytes()
                if hashlib.sha256(body).hexdigest() != identity['patches'][index]['sha256']:
                    raise ValueError(f'Patch changed while preparing source: {patch}')
                snapshot = temporary / f'patch-{index}.diff'
                snapshot.write_bytes(body)
                subprocess.run(['git', '--no-replace-objects', '-c', 'core.autocrlf=false', '-c', 'core.eol=lf',
                                'apply', str(snapshot)], cwd=staged, check=True)
            if patches:
                shutil.rmtree(staged / '.git')
            record = {'identity': identity, 'files': tree_manifest(staged)}
            if source_files is not None:
                record['source_files'] = source_files
            if previous and previous.get('identity') == identity:
                if previous != record:
                    raise ValueError(f'Prepared {name} manifest or files were edited; remove both to regenerate')
            else:
                if destination.exists():
                    shutil.rmtree(destination)
                os.replace(staged, destination)
                staged_stamp = temporary / 'manifest.json'
                staged_stamp.write_text(json.dumps(record, indent=2, sort_keys=True) + '\n')
                os.replace(staged_stamp, stamp)
        identities.append(identity)
    return identities


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--verify', action='store_true')
    parser.add_argument('--decomp-revision', help='full local commit SHA containing decomp/')
    parser.add_argument('--decomp-profile', choices=DECOMP_PROFILES, default='native',
                        help='local decomp patch profile (default: native)')
    parser.add_argument('--decomp-only', action='store_true', help='skip third-party dependencies')
    args = parser.parse_args(argv)
    if args.decomp_only and not args.decomp_revision:
        parser.error('--decomp-only requires --decomp-revision')
    try:
        prepare(ROOT, args.output, names=() if args.decomp_only else DEPENDENCIES,
                verify=args.verify, decomp_revision=args.decomp_revision,
                decomp_profile=args.decomp_profile)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, str(error) + '\n')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
