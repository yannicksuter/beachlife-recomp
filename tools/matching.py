"""Build and compare small x86 matching targets with the verified VC6 toolchain."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[1]
TARGET = ROOT / 'decomp/targets/beachlife-uk-1.1.json'


def digest(path, algorithm='sha256'):
    result = hashlib.new(algorithm)
    with Path(path).open('rb') as stream:
        for data in iter(lambda: stream.read(1024 * 1024), b''):
            result.update(data)
    return result.hexdigest()


def verify_original(path, target):
    path = Path(path)
    if path.stat().st_size != target['size']:
        raise ValueError('Original executable size does not match the target.')
    for name in ('sha1', 'sha256'):
        if digest(path, name) != target[name]:
            raise ValueError(f'Original executable {name} does not match the target.')
    return path.resolve()


def section(path, name):
    """Read a section from our generated PE32 fixture; reject truncated layouts."""
    data = Path(path).read_bytes()
    if len(data) < 64 or data[:2] != b'MZ':
        raise ValueError('Not a PE executable.')
    pe = struct.unpack_from('<I', data, 0x3c)[0]
    if pe+24 > len(data) or data[pe:pe+4] != b'PE\0\0':
        raise ValueError('Invalid PE header.')
    machine, count = struct.unpack_from('<HH', data, pe+4)
    if machine != 0x14c:
        raise ValueError('The matching fixture must be 32-bit x86.')
    headers = pe+24+struct.unpack_from('<H', data, pe+20)[0]
    if headers+count*40 > len(data):
        raise ValueError('Truncated PE sections.')
    for index in range(count):
        entry = headers+index*40
        if data[entry:entry+8].rstrip(b'\0') == name:
            size, start = struct.unpack_from('<II', data, entry+16)
            if start+size > len(data):
                raise ValueError('Truncated PE section data.')
            return data[start:start+size]
    raise ValueError(f'Missing section {name!r}.')


def save(path, value):
    Path(path).write_text(json.dumps(value, indent=2) + '\n')


def read_va(path, address, size):
    if address < 0 or size < 0:
        raise ValueError('Invalid PE address range.')
    data = Path(path).read_bytes()
    if len(data) < 64 or data[:2] != b'MZ':
        raise ValueError('Truncated PE image.')
    pe = struct.unpack_from('<I', data, 0x3c)[0]
    if pe+56 > len(data) or data[pe:pe+4] != b'PE\0\0':
        raise ValueError('Invalid PE header.')
    if struct.unpack_from('<H', data, pe+4)[0] != 0x14c:
        raise ValueError('Expected a 32-bit x86 matching image.')
    if struct.unpack_from('<H', data, pe+24)[0] != 0x10b:
        raise ValueError('Expected a PE32 matching image.')
    base = struct.unpack_from('<I', data, pe+52)[0]
    headers = pe+24+struct.unpack_from('<H', data, pe+20)[0]
    count = struct.unpack_from('<H', data, pe+6)[0]
    if headers+count*40 > len(data):
        raise ValueError('Truncated PE sections.')
    for index in range(count):
        entry = headers+40*index
        rva, length, offset = struct.unpack_from('<III', data, entry+12)
        relative = address-base-rva
        if 0 <= relative and relative+size <= length and offset+length <= len(data):
            return data[offset+relative:offset+relative+size]
    raise ValueError('Address is outside initialized PE sections.')


class Toolchain:
    def __init__(self, args):
        if not args.compiler:
            raise ValueError('Set --compiler or BEACHLIFE_MSVC6 to the VC6 SP4 directory.')
        self.compiler = Path(args.compiler).resolve() / 'Bin'
        self.output = args.output.resolve()
        self.output.mkdir(parents=True, exist_ok=True)
        self.environment = os.environ.copy()
        self.prefix = args.prefix.resolve()
        if os.name != 'nt':
            self.environment.update(WINEPREFIX=str(self.prefix), WINEDEBUG='-all',
                                    WINEDLLOVERRIDES='mscoree,mshtml=')
        pins = json.loads((ROOT / 'decomp/msvc6-sp4.json').read_text())
        for component in pins['components']:
            if digest(self.compiler / component['file']) != component['sha256']:
                raise ValueError(f"Compiler component mismatch: {component['file']}")
        program = shutil.which(args.reccmp)
        if not program:
            raise ValueError('Install decomp/requirements.txt and set --reccmp to reccmp-reccmp.')
        self.reccmp = str(Path(program).resolve())
        version = self.run([self.reccmp, '--version'], self.output)
        if version.strip() != 'reccmp-reccmp 0.1.7':
            raise ValueError(f'Expected reccmp 0.1.7, got {version.strip()}')

    def run(self, command, cwd):
        result = subprocess.run(command, cwd=cwd, env=self.environment, capture_output=True,
                                text=True, errors='replace', timeout=60)
        output = result.stdout + result.stderr
        if result.returncode:
            raise RuntimeError(f"Command failed ({result.returncode}): {command[0]}\n{output}")
        return output

    def native(self, path):
        path = Path(path).resolve()
        if os.name == 'nt':
            return str(path)
        # The dedicated Wine prefix uses Wine's standard Z: mapping to /.
        if (self.prefix / 'dosdevices/z:').resolve() != Path('/'):
            raise ValueError('Initialize the dedicated Wine prefix with wineboot before building.')
        return 'Z:' + str(path).replace('/', '\\')

    @staticmethod
    def response(path, arguments):
        if any('"' in arg or '\n' in arg or '\r' in arg for arg in arguments):
            raise ValueError('Unsupported quote/newline in compiler argument.')
        path.write_text('\n'.join('"' + arg + '"' for arg in arguments) + '\n')

    def build(self, folder, sources, name, extra_link=()):
        folder.mkdir(parents=True, exist_ok=True)
        objects, logs = [], []
        for source in sources:
            obj = source.stem + '.obj'
            if obj in objects:
                raise ValueError('Matching source basenames must be unique.')
            objects.append(obj)
            flags = ['/nologo', '/c', '/O2', '/Ob0', '/Zi', '/Gd', '/Gy',
                     '/Fdcompile.pdb', '/Fo' + obj, self.native(source)]
            self.response(folder / 'compile.rsp', flags)
            command = [str(self.compiler / 'CL.EXE'), '@compile.rsp']
            logs.append(self.run(command if os.name == 'nt' else ['wine', *command], folder))
        flags = ['/nologo', '/out:' + name + '.exe', '/pdb:' + name + '.pdb', '/debug',
                 '/pdbtype:sept', '/incremental:no', '/opt:ref', '/opt:icf',
                 '/entry:fixture_entry', '/subsystem:console', '/machine:I386',
                 '/nodefaultlib', '/map:' + name + '.map', *extra_link, *objects]
        self.response(folder / 'link.rsp', flags)
        command = [str(self.compiler / 'LINK.EXE'), '@link.rsp']
        logs.append(self.run(command if os.name == 'nt' else ['wine', *command], folder))
        (folder / 'build.log').write_text(''.join(logs))
        return folder / (name + '.exe'), folder / (name + '.pdb')

    def compare(self, original, binary, pdb, source, report):
        report.parent.mkdir(parents=True, exist_ok=True)
        output = self.run([self.reccmp, '--paths', str(original), str(binary), str(pdb),
                           str(source), '--json', str(report), '--no-color'], ROOT)
        report.with_suffix('.log').write_text(output)
        return json.loads(report.read_text())


def fixture(tool):
    folder = tool.output / 'fixture'
    folder.mkdir(exist_ok=True)
    source = folder / 'fixture.cpp'
    shutil.copy2(ROOT / 'decomp/fixtures/fixture.cpp', source)
    binary, pdb = tool.build(folder, [source], 'fixture')
    first = section(binary, b'.text')
    tool.build(folder, [source], 'fixture')
    if first != section(binary, b'.text'):
        raise ValueError('Repeated compilation changed the fixture machine code.')
    same = tool.compare(binary, binary, pdb, folder, folder / 'self.json')
    changed = tool.output / 'changed'
    changed.mkdir(exist_ok=True)
    altered = changed / 'fixture.cpp'
    altered.write_text(source.read_text().replace('0x51a9u', '0x51aau'))
    different, other_pdb = tool.build(changed, [altered], 'fixture')
    report = tool.compare(binary, different, other_pdb, changed, changed / 'different.json')
    original_matches = {row['name']: row['matching'] for row in same['data']}
    changed_matches = {row['name']: row['matching'] for row in report['data']}
    if original_matches.get('fixture_mix') != 1 or original_matches.get('fixture_entry') != 1:
        raise ValueError('Synthetic fixture does not self-match.')
    if not 0 < changed_matches.get('fixture_mix', 1) < 1:
        raise ValueError('Comparator did not detect the deliberate code change.')
    result = {'compiler': 'VC6 SP4 / C2 12.0.8799.0', 'reccmp': '0.1.7',
              'repeat_text_identical': True, 'text_sha256': hashlib.sha256(first).hexdigest(),
              'self_match': original_matches, 'changed_match': changed_matches,
              'scope': 'Generated fixture only; not a game matching result.'}
    save(tool.output / 'fixture-result.json', result)
    print(json.dumps(result, indent=2))


def game(tool, args):
    target = json.loads(TARGET.read_text())
    original = verify_original(args.original, target)
    source = args.source.resolve()
    sources = sorted(source.rglob('*.cpp'))
    if not sources:
        raise ValueError('No matching source files found.')
    folder = tool.output / 'game'
    folder.mkdir(parents=True, exist_ok=True)
    entry = folder / 'entry.cpp'
    entry.write_text('extern "C" { int _fltused = 0; }\n'
                     'float GetGameVersion();\nvolatile float fixture_result;\n'
                     'extern "C" void fixture_entry() { fixture_result = GetGameVersion(); }\n')
    binary, pdb = tool.build(folder, [*sources, entry], 'BeachLife', ['/fixed:no'])
    # CSV metadata supplies the known float in the original, whose relocations
    # were stripped. Relocations in our comparison build identify its literal.
    save(folder / 'reccmp-project.yml', {'targets': {'BEACHLIFE': {
        'filename': 'BeachLife.exe', 'source-root': [str(source)],
        'hash': {'sha256': target['sha256']},
        'data-sources': [str(ROOT / 'decomp/metadata.csv')],
        'report': {'ignore-functions': ['fixture_entry']}}}})
    save(folder / 'reccmp-user.yml', {'targets': {'BEACHLIFE': {'path': str(original)}}})
    save(folder / 'reccmp-build.yml', {'project': '.', 'targets': {'BEACHLIFE': {
        'path': str(binary), 'pdb': str(pdb)}}})
    reports = args.reports.resolve()
    reports.mkdir(parents=True, exist_ok=True)
    output = tool.run([tool.reccmp, '--target', 'BEACHLIFE', '--json',
                       str(reports / 'comparison.json'), '--no-color'], folder)
    (reports / 'comparison.log').write_text(output)
    report = json.loads((reports / 'comparison.json').read_text())
    functions = []
    for item in target['functions']:
        row = next((row for row in report['data'] if int(row['address'], 16) == int(item['address'], 16)), None)
        if row is None:
            raise ValueError(f"Comparator did not find {item['name']}.")
        exact = read_va(original, int(item['address'], 16), item['size']) == read_va(binary, int(row['recomp'], 16), item['size'])
        functions.append({'name': item['name'], 'address': item['address'], 'bytes': item['size'],
                          'normalized_match': row['matching'], 'exact_bytes': exact})
    result = {'target': target['id'], 'original_sha256': target['sha256'],
              'compiler': 'VC6 SP4 / C2 12.0.8799.0', 'reccmp': '0.1.7',
              'functions': functions, 'scope': 'Only listed functions; whole-game denominator unknown.',
              'comparison_layout': 'Relocations enabled in rebuilt comparison image; original remains unchanged.'}
    save(reports / 'result.json', result)
    print(json.dumps(result, indent=2))
    if any(item['normalized_match'] != 1 for item in functions):
        raise ValueError('One or more annotated functions do not match.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', choices=['verify', 'fixture', 'game'])
    parser.add_argument('--compiler', default=os.environ.get('BEACHLIFE_MSVC6'))
    parser.add_argument('--original', type=Path, default=ROOT / 'orig/analysis/disc/BeachLife.exe')
    parser.add_argument('--output', type=Path, default=ROOT / 'build/matching')
    parser.add_argument('--prefix', type=Path, default=ROOT / 'build/matching/wine')
    parser.add_argument('--reccmp', default='reccmp-reccmp')
    parser.add_argument('--source', type=Path, default=ROOT / 'decomp/src')
    parser.add_argument('--reports', type=Path, default=ROOT / 'orig/analysis/matching')
    args = parser.parse_args()
    try:
        if args.command == 'verify':
            target = json.loads(TARGET.read_text())
            path = verify_original(args.original, target)
            print(json.dumps({'target': target['id'], 'original': str(path), 'verified': True}))
        elif args.command == 'fixture':
            fixture(Toolchain(args))
        else:
            game(Toolchain(args), args)
    except (OSError, ValueError, RuntimeError, subprocess.TimeoutExpired) as error:
        parser.exit(1, str(error) + '\n')


if __name__ == '__main__':
    main()
