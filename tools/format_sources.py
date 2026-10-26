"""Format the project's own C and C++ sources with the pinned clang-format.

Without --check every selected file is rewritten in place. With --check files
stay unchanged; differences are printed and the exit status is 1. Paths limit
the selection. Submodules, ignored files and original game files are skipped.

The formatter is found through --clang-format, CLANG_FORMAT, the project-local
build/format-venv install of the pinned wheel, then PATH.
"""
import argparse
import difflib
import os
import re
import shutil
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PIN = ROOT / 'tools/requirements-format.txt'
VENV = ROOT / 'build/format-venv'
SUFFIXES = {'.c', '.cc', '.cpp', '.cxx', '.h', '.hh', '.hpp', '.hxx', '.inl', '.m', '.mm'}
EXCLUDED = ('extern/', 'orig/')


def pinned_version(pin=PIN):
    match = re.search(r'^clang-format==(\S+)$', pin.read_text(), re.M)
    if not match:
        raise ValueError(f'{pin} does not pin clang-format.')
    return match.group(1)


def default_formatter():
    for local in (VENV / 'bin/clang-format', VENV / 'Scripts/clang-format.exe'):
        if local.is_file():
            return str(local)
    return 'clang-format'


def formatter(name, version):
    executable = shutil.which(name)
    install = (f'python3 -m venv {VENV.relative_to(ROOT)} && '
               f'{VENV.relative_to(ROOT)}/bin/pip install -r {PIN.relative_to(ROOT)}')
    if not executable:
        raise ValueError(f'{name} was not found. Install the pinned formatter: {install}')
    output = subprocess.run([executable, '--version'], capture_output=True, text=True,
                            check=True).stdout
    found = re.search(r'clang-format version (\S+)', output)
    if not found or found.group(1) != version:
        actual = found.group(1) if found else 'unknown'
        raise ValueError(f'{executable} is clang-format {actual}; the project pins {version}. '
                         f'Install it with {install} or pass --clang-format.')
    return executable


def sources(root, paths=()):
    listed = subprocess.run(['git', 'ls-files', '--cached', '--others', '--exclude-standard',
                             '-z', '--', *paths], cwd=root, capture_output=True, check=True)
    names = sorted({name for name in listed.stdout.decode().split('\0') if name})
    return [name for name in names
            if Path(name).suffix.lower() in SUFFIXES and not name.startswith(EXCLUDED)
            and (root / name).is_file() and not (root / name).is_symlink()]


def format_one(executable, root, name, check):
    path = root / name
    before = path.read_bytes()
    result = subprocess.run([executable, '--style=file', '--fallback-style=none', str(path)],
                            capture_output=True)
    if result.returncode:
        raise RuntimeError(f'{name}: {result.stderr.decode(errors="replace").strip()}')
    after = result.stdout
    if after == before:
        return None
    if check:
        return ''.join(difflib.unified_diff(
            before.decode(errors='replace').splitlines(keepends=True),
            after.decode(errors='replace').splitlines(keepends=True),
            f'a/{name}', f'b/{name}'))
    path.write_bytes(after)
    return ''


def run(executable, root, names, check):
    """Return the names that differ from the formatted result."""
    with ThreadPoolExecutor(max_workers=os.cpu_count() or 4) as pool:
        results = list(pool.map(lambda name: format_one(executable, root, name, check), names))
    changed = []
    for name, diff in zip(names, results):
        if diff is not None:
            changed.append(name)
            if diff:
                sys.stdout.write(diff)
    return changed


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('paths', nargs='*', type=Path, help='limit to these files or folders')
    parser.add_argument('--check', action='store_true', help='report without changing files')
    parser.add_argument('--clang-format', default=os.environ.get('CLANG_FORMAT') or default_formatter())
    args = parser.parse_args()
    try:
        executable = formatter(args.clang_format, pinned_version())
        names = sources(ROOT, [str(path.resolve()) for path in args.paths])
        changed = run(executable, ROOT, names, args.check)
    except (OSError, ValueError, RuntimeError, subprocess.CalledProcessError) as error:
        parser.exit(2, f'{error}\n')
    if args.check and changed:
        print(f'{len(changed)} of {len(names)} files need formatting. '
              'Run: python3 tools/format_sources.py', file=sys.stderr)
        return 1
    verb = 'need formatting' if args.check else 'reformatted'
    print(f'{len(names)} files checked, {len(changed)} {verb}.')
    return 0


if __name__ == '__main__':
    sys.exit(main())
