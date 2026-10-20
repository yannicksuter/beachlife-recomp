"""Build and compare small x86 matching targets with the verified VC6 toolchain."""
import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess

if __package__:
    from .matching_imports import (coff_import_symbols, stdcall_export_names,
                                  stdcall_import_library, validate_stdcall_imports,
                                  vc6_exception_abi_object)
    from .prepare_sources import prepare
else:
    from matching_imports import (coff_import_symbols, stdcall_export_names,
                                 stdcall_import_library, validate_stdcall_imports,
                                 vc6_exception_abi_object)
    from prepare_sources import prepare

ROOT = Path(__file__).resolve().parents[1]
TARGET = ROOT / 'decomp/targets/beachlife-uk-1.1.json'
SOURCE_PIN = ROOT / 'patches/beachlife-decomp/source.json'


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
        self.decomp_root = getattr(args, 'decomp_root', ROOT / 'decomp')
        self.output = args.output.resolve()
        self.output.mkdir(parents=True, exist_ok=True)
        self.environment = os.environ.copy()
        self.prefix = args.prefix.resolve()
        if os.name != 'nt':
            self.environment.update(WINEPREFIX=str(self.prefix), WINEDEBUG='-all',
                                    WINEDLLOVERRIDES='mscoree,mshtml=')
        pins = json.loads((self.decomp_root / 'msvc6-sp4.json').read_text())
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

    def runtime_library(self, folder):
        folder.mkdir(parents=True, exist_ok=True)
        shutil.copy2(self.decomp_root / 'msvcrt.def', folder / 'msvcrt.def')
        command = [str(self.compiler / 'LIB.EXE'), '/nologo', '/machine:IX86',
                   '/def:msvcrt.def', '/out:msvcrt-import.lib']
        log = self.run(command if os.name == 'nt' else ['wine', *command], folder)
        (folder / 'imports.log').write_text(log)
        return 'msvcrt-import.lib'

    def build(self, folder, sources, name, extra_link=(), extra_compile=(), source_flags=None):
        folder.mkdir(parents=True, exist_ok=True)
        objects, logs = [], []
        for source in sources:
            obj = source.stem + '.obj'
            if obj in objects:
                raise ValueError('Matching source basenames must be unique.')
            objects.append(obj)
            flags = ['/nologo', '/c', '/O2', '/Ob0', '/Zi', '/Gd', '/Gy',
                     '/Fdcompile.pdb', '/Fo' + obj, *extra_compile,
                     *(source_flags or {}).get(source, ()), self.native(source)]
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


def pe_imports(path):
    data = Path(path).read_bytes()
    # Reuse the bounded PE reader before following the import directory.
    section(path, b'.text')
    pe = struct.unpack_from('<I', data, 0x3c)[0]
    if pe+132 > len(data) or struct.unpack_from('<H', data, pe+24)[0] != 0x10b:
        raise ValueError('Expected a complete PE32 optional header.')
    base = struct.unpack_from('<I', data, pe+52)[0]
    directory = struct.unpack_from('<I', data, pe+128)[0]
    if not directory:
        return {}
    def read(rva, count):
        return read_va(path, base+rva, count)
    def string(rva):
        value = bytearray()
        for index in range(4096):
            char = read(rva+index, 1)
            if char == b'\0':
                return value.decode('ascii')
            value.extend(char)
        raise ValueError('Unterminated PE import name.')
    result = {}
    for index in range(4096):
        lookup, timestamp, chain, name, address = struct.unpack('<5I', read(directory+index*20, 20))
        if not any((lookup, timestamp, chain, name, address)):
            return result
        symbols = result.setdefault(string(name), {})
        for slot in range(65536):
            value, = struct.unpack('<I', read((lookup or address)+slot*4, 4))
            if not value:
                break
            symbol = f'ordinal:{value & 0xffff}' if value & 0x80000000 else string(value+2)
            symbols[symbol] = hex(base+address+slot*4)
        else:
            raise ValueError('Unterminated PE import table.')
    raise ValueError('Unterminated PE import directory.')


def stdio_fixture(tool):
    folder = tool.output / 'stdio'
    library = tool.runtime_library(folder)
    binary, pdb = tool.build(folder, [ROOT / 'decomp/fixtures/stdio.cpp'], 'stdio',
                            [library], ['/MD', '/I' + tool.native(tool.compiler.parent / 'Include')])
    imports = pe_imports(binary)
    expected = {'fopen', 'fclose', 'fseek', 'fgetpos'}
    if set(imports) != {'MSVCRT.dll'} or set(imports['MSVCRT.dll']) != expected:
        raise ValueError(f'Unexpected stdio imports: {imports}')
    obj = (folder / 'stdio.obj').read_bytes()
    if any(('__imp__' + name + '\0').encode() not in obj for name in expected):
        raise ValueError('The stdio fixture does not use the expected x86 cdecl imports.')
    result = {'imports': imports, 'fpos_t_bytes': 8, 'pdb_created': pdb.is_file(),
              'calling_convention': 'x86 cdecl imports from pinned STDIO.H; COFF names verified',
              'library': 'Generated by pinned LIB.EXE from decomp/msvcrt.def',
              'scope': 'Generated linkage fixture; no retail binary or SDK library copied.'}
    save(tool.output / 'stdio-result.json', result)
    print(json.dumps(result, indent=2))


def platform_fixture(tool):
    """Compile SDK declarations and verify both stdcall ABI and PE export names."""
    specification = json.loads((ROOT / 'decomp/win32-imports.json').read_text())
    if (specification['schema'], specification['architecture'],
            specification['calling_convention']) != (1, 'x86', 'stdcall'):
        raise ValueError('Unsupported platform import specification.')
    headers = specification['headers_sha256']
    for name, expected in headers.items():
        if digest(tool.compiler.parent / 'Include' / name) != expected:
            raise ValueError(f'Platform header mismatch: {name}')
    folder = tool.output / 'platform'
    libraries = []
    adjustments = {}
    for dll, symbols in specification['libraries'].items():
        library, changed = stdcall_import_library(tool, folder, dll, symbols)
        libraries.append(library.name)
        adjustments[dll] = changed
    source = ROOT / 'decomp/fixtures/platform.cpp'
    binary, pdb = tool.build(folder, [source], 'platform', libraries,
                            ['/MD', '/G6', '/I' + tool.native(tool.compiler.parent / 'Include')])
    symbols = coff_import_symbols(folder / 'platform.obj')
    expected_symbols = sorted(f'__imp__{name}@{size}'
                              for functions in specification['libraries'].values()
                              for name, size in functions.items())
    if symbols != expected_symbols:
        raise ValueError(f'Platform fixture has unexpected stdcall references: {symbols}')
    imports = pe_imports(binary)
    expected_imports = sorted((dll, name) for dll, names in specification['libraries'].items()
                              for name in names)
    actual_imports = sorted((dll, name) for dll, names in imports.items() for name in names)
    if actual_imports != expected_imports:
        raise ValueError(f'Platform fixture has unexpected PE imports: {imports}')
    result = {'imports': imports, 'coff_import_symbols': symbols,
              'headers_sha256': headers, 'pdb_created': pdb.is_file(),
              'name_mode_adjustments': adjustments,
              'fixture_sha256': digest(source), 'binary_sha256': digest(binary),
              'scope': 'Generated compile/link fixture only; never executed. No SDK library code copied.'}
    save(tool.output / 'platform-result.json', result)
    print(json.dumps(result, indent=2))


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


def prepare_game_source(args):
    pin = json.loads(SOURCE_PIN.read_text())
    if not isinstance(pin, dict) or set(pin) != {'revision'}:
        raise ValueError('The reconstructed source pin must contain only a revision.')
    output = args.output.resolve() / 'prepared'
    prepare(ROOT, output, names=(), decomp_revision=pin['revision'], decomp_profile='matching')
    args.decomp_root = output / 'beachlife-decomp'
    args.prepared_manifest = output / 'beachlife-decomp.json'
    args.decomp_revision = pin['revision']
    if args.source is None:
        args.source = args.decomp_root / 'src'


def game_source_compile_options(target, sources):
    """Resolve qualified options to unique game or support source files."""
    configured = target.get('source_compile_options', {})
    if not isinstance(configured, dict):
        raise ValueError('source_compile_options must be a source-basename mapping.')
    available = {}
    for source in sources:
        if source.name in available or not source.is_file():
            raise ValueError('Matching source basenames must identify unique files.')
        available[source.name] = source
    result = {}
    for name, flags in configured.items():
        if (not isinstance(name, str) or
                not re.fullmatch(r'[A-Za-z0-9_][A-Za-z0-9_.-]*\.cpp', name) or
                name not in available):
            raise ValueError('Compiler options must name an existing game or support source basename.')
        if (not isinstance(flags, list) or any(not isinstance(flag, str) for flag in flags) or
                len(flags) != len(set(flags)) or any(flag not in ('/GX', '/Ob1') for flag in flags)):
            raise ValueError('Source compiler options must be unique entries from /GX and /Ob1.')
        result[available[name]] = flags.copy()
    return result


def game_stdcall_imports(target):
    """Resolve opt-in DLL export policies without changing the CRT import route."""
    configured = target.get('stdcall_imports', {})
    if not isinstance(configured, dict):
        raise ValueError('stdcall_imports must be a DLL mapping.')
    result = {}
    names = {'msvcrt.dll'}
    for dll, policy in configured.items():
        if not isinstance(policy, dict) or set(policy) != {'export_names', 'symbols'}:
            raise ValueError('Each stdcall DLL requires export_names and symbols only.')
        validate_stdcall_imports(dll, policy['symbols'], policy['export_names'])
        if dll.lower() in names:
            raise ValueError('Duplicate or reserved stdcall DLL basename.')
        names.add(dll.lower())
        result[dll] = {'export_names': policy['export_names'],
                       'symbols': dict(sorted(policy['symbols'].items()))}
    return dict(sorted(result.items()))


def verify_stdcall_pe_imports(binary, configured, exact):
    """Check actual PE names, not only matching tool labels or COFF symbols."""
    if not configured:
        return {}
    imports = pe_imports(binary)
    verified = {}
    for dll, policy in configured.items():
        expected = stdcall_export_names(policy['symbols'], policy['export_names'])
        actual = imports.get(dll, {})
        if not expected <= set(actual) or (exact and set(actual) != expected):
            raise ValueError(f'Stdcall PE import identity mismatch for {dll}.')
        verified[dll] = {name: actual[name] for name in sorted(expected)}
    return verified


def game_runtime_stdcall_imports(target):
    """Opt into the evidenced VC6 throw ABI without changing the cdecl CRT route."""
    configured = target.get('runtime_stdcall_imports', {})
    if (not isinstance(configured, dict) or
            any(name != '_CxxThrowException' or type(count) is not int or count != 8
                for name, count in configured.items())):
        raise ValueError('Only the observed _CxxThrowException runtime stdcall import is supported.')
    return dict(configured)


def verify_runtime_original_imports(binary, configured):
    if not configured:
        return {}
    actual = pe_imports(binary).get('MSVCRT.dll', {})
    if not set(configured) <= set(actual):
        raise ValueError('Original runtime PE import identity mismatch.')
    return actual


def verify_runtime_compiled_imports(binary, configured, original):
    if not configured:
        return {}
    actual = pe_imports(binary).get('MSVCRT.dll', {})
    if not set(actual) <= set(original):
        raise ValueError('Rebuilt runtime PE imports contain an unverified identity.')
    # VC6 can retain an undefined COFF throw symbol after removing its catch
    # code. LINK resolves that symbol but discards the unused import. Absence
    # here is recorded; it is not evidence of an executed runtime dependency.
    return {'linked': {name: actual[name] for name in sorted(configured) if name in actual},
            'omitted': sorted(set(configured) - set(actual))}


def comparison_anchor_specs(metadata, target):
    """Validate unqualified address anchors separately from credited functions."""
    specs = target.get('comparison_anchors', [])
    if not isinstance(specs, list):
        raise ValueError('comparison_anchors must be a list.')
    if not specs:
        return []
    credited = [item for key in ('functions', 'library_functions', 'unclassified_functions')
                for item in target.get(key, [])]
    ranges = [(int(item['address'], 16), int(item['address'], 16) + item['size'])
              for item in credited]
    names = {item['name'] for item in credited}
    symbols = set()
    with (metadata / 'metadata.csv').open(newline='') as stream:
        rows = list(csv.DictReader(stream))
    keys = {'address', 'size', 'name', 'symbol', 'header', 'kind', 'return_type', 'parameters'}
    for item in specs:
        if (not isinstance(item, dict) or set(item) != keys or
                item.get('kind') not in ('null-pointer-member', 'void-member') or
                not isinstance(item.get('address'), str) or
                not re.fullmatch(r'0x[0-9a-fA-F]{1,8}', item['address']) or
                type(item.get('size')) is not int or item['size'] <= 0 or
                not isinstance(item.get('name'), str) or
                not re.fullmatch(r'[A-Za-z_]\w*::[A-Za-z_]\w*', item['name']) or
                not isinstance(item.get('symbol'), str) or
                not re.fullmatch(r'\?[^,\s]+', item['symbol']) or
                not isinstance(item.get('header'), str) or
                not re.fullmatch(r'[A-Za-z_]\w*\.h', item['header']) or
                not isinstance(item.get('return_type'), str) or
                not ((item['kind'] == 'null-pointer-member' and
                      re.fullmatch(r'[A-Za-z_]\w*\*', item['return_type'])) or
                     (item['kind'] == 'void-member' and item['return_type'] == 'void')) or
                item.get('parameters') != ['const char*']):
            raise ValueError('Unsupported comparison-only anchor declaration.')
        start, end = int(item['address'], 16), int(item['address'], 16) + item['size']
        if (end > 0x100000000 or item['name'] in names or item['symbol'] in symbols or
                any(start < right and left < end for left, right in ranges)):
            raise ValueError('Comparison-only anchors must be distinct from credited functions and each other.')
        rows_at_address = [row for row in rows if int(row['address'], 16) == start]
        if (len(rows_at_address) != 1 or rows_at_address[0]['type'] != 'function' or
                rows_at_address[0]['size'] != str(item['size']) or
                rows_at_address[0]['symbol'] != item['symbol']):
            raise ValueError('Comparison-only anchor must match one explicit CSV identity.')
        ranges.append((start, end))
        names.add(item['name'])
        symbols.add(item['symbol'])
    return specs


def generate_comparison_anchors(metadata, source, folder, specs):
    """Generate ignored matching-only definitions; never add them to an export."""
    if not specs:
        return [], []
    destination = (folder / 'comparison-only').resolve()
    if (not destination.is_relative_to(ROOT.resolve()) or
            destination.is_relative_to(metadata.resolve()) or
            destination.is_relative_to(source.resolve())):
        raise ValueError('Comparison anchors require output outside reconstructed source.')
    ignored = subprocess.run(['git', 'check-ignore', '--quiet', str(destination)], cwd=ROOT,
                             capture_output=True, text=True, check=False)
    if ignored.returncode != 0:
        raise ValueError('Comparison anchors require ignored output inside this repository.')
    paths = [destination / ('anchor_' + item['address'][2:].lower() + '.cpp') for item in specs]
    if destination.exists() and any(path not in paths for path in destination.iterdir()):
        raise ValueError('Unexpected files in comparison-only output; use a fresh build directory.')
    for item in specs:
        header = (source / item['header']).resolve()
        if not header.is_relative_to(source.resolve()) or not header.is_file():
            raise ValueError('Comparison anchor header is not an actual source input.')
    destination.mkdir(parents=True, exist_ok=True)
    records = []
    for item, path in zip(specs, paths):
        statement = ('    return 0;\n' if item['kind'] == 'null-pointer-member'
                     else '    (void)argument;\n')
        body = (f'#include "{item["header"]}"\n\n'
                '// GENERATED COMPARISON-ONLY ANCHOR. Never execute this image.\n'
                '// This is no implementation or qualification of the original dependency.\n'
                f'// FUNCTION: BEACHLIFE {item["address"]}\n'
                f'{item["return_type"]} {item["name"]}(const char* argument)\n'
                '{\n' + statement + '}\n')
        if path.exists() and path.read_text() != body:
            raise ValueError('Comparison-only source changed; use a fresh build directory.')
        if not path.exists():
            path.write_text(body)
        records.append({'address': item['address'], 'original_bytes': item['size'],
                        'name': item['name'], 'symbol': item['symbol'],
                        'source': str(path.relative_to(ROOT)), 'sha256': digest(path),
                        'qualified': False, 'image_execution_permitted': False})
    return paths, records


def game_target(metadata):
    """Validate every registered group before invoking matching build tools."""
    path = metadata / 'targets/beachlife-uk-1.1.json'
    target = json.loads(path.read_text())
    if not isinstance(target, dict) or 'functions' not in target:
        raise ValueError('The matching target requires a functions list.')
    seen = set()
    for key in ('functions', 'library_functions', 'unclassified_functions'):
        items = target.get(key, [])
        if not isinstance(items, list):
            raise ValueError(f'{key} must be a function list.')
        for item in items:
            if (not isinstance(item, dict) or
                    not isinstance(item.get('name'), str) or not item['name'].strip() or
                    not isinstance(item.get('address'), str) or
                    not re.fullmatch(r'0x[0-9a-fA-F]{1,8}', item['address']) or
                    type(item.get('size')) is not int or item['size'] <= 0):
                raise ValueError(f'{key} needs named functions with a 32-bit address and positive integer size.')
            start = int(item['address'], 16)
            if start + item['size'] > 0x100000000:
                raise ValueError('A registered function extends outside the 32-bit image.')
            if start in seen:
                raise ValueError(f'Duplicate function address across matching groups: {item["address"]}')
            seen.add(start)
    comparison_anchor_specs(metadata, target)
    return path, target


def verify_sdk_headers(metadata, include):
    """Check the declared SDK inputs without copying any SDK contents."""
    specification = json.loads((metadata / 'win32-imports.json').read_text())
    if not isinstance(specification, dict) or (
            specification.get('schema'), specification.get('architecture'),
            specification.get('calling_convention')) != (1, 'x86', 'stdcall'):
        raise ValueError('Unsupported platform import specification.')
    headers = specification.get('headers_sha256')
    if not isinstance(headers, dict) or not headers:
        raise ValueError('The platform specification needs pinned SDK headers.')
    seen = set()
    for name, expected in headers.items():
        if (not isinstance(name, str) or not re.fullmatch(r'[A-Za-z0-9_-]+\.[Hh]', name) or
                not isinstance(expected, str) or not re.fullmatch(r'[0-9a-f]{64}', expected) or
                name.casefold() in seen):
            raise ValueError('SDK pins need unique header basenames and SHA-256 digests.')
        seen.add(name.casefold())
    for name, expected in headers.items():
        if digest(include / name) != expected:
            raise ValueError(f'Platform header mismatch: {name}')
    return headers


def game(tool, args):
    metadata = tool.decomp_root
    target_path, target = game_target(metadata)
    sdk_headers = verify_sdk_headers(metadata, tool.compiler.parent / 'Include')
    original = verify_original(args.original, target)
    source = args.source.resolve()
    sources = sorted(source.rglob('*.cpp'))
    support = metadata / 'support'
    support_sources = sorted(support.glob('*.cpp'))
    if not sources:
        raise ValueError('No matching source files found.')
    compile_options = game_source_compile_options(target, [*sources, *support_sources])
    stdcall_imports = game_stdcall_imports(target)
    runtime_imports = game_runtime_stdcall_imports(target)
    original_stdcall_imports = verify_stdcall_pe_imports(original, stdcall_imports, exact=False)
    original_runtime_imports = verify_runtime_original_imports(original, runtime_imports)
    # The fixture is part of the patchable source export, never a game driver.
    entry = metadata / 'fixtures/game.cpp'
    if not entry.is_file():
        raise ValueError('Prepared source has no comparison retention fixture.')
    folder = tool.output / 'game'
    anchors, anchor_records = generate_comparison_anchors(
        metadata, source, folder, comparison_anchor_specs(metadata, target))
    inputs = set([*sources, *anchors, *source.rglob('*.h'),
                  *(path for path in metadata.rglob('*') if path.is_file()),
                  ROOT / 'tools/matching_imports.py', ROOT / 'tools/prepare_sources.py',
                  Path(__file__).resolve()])
    manifest_path = getattr(args, 'prepared_manifest', None)
    if manifest_path is not None:
        manifest = json.loads(manifest_path.read_text())
        inputs.update([manifest_path, SOURCE_PIN])
        inputs.update(ROOT / item['path'] for item in manifest['identity']['patches'])
        inputs.update(ROOT / item['path'] for item in manifest['identity']['series']
                      if item['sha256'] is not None)
    inputs = sorted(inputs)
    def input_hashes():
        return {str(path.relative_to(ROOT) if path.is_relative_to(ROOT) else path): digest(path)
                for path in inputs}
    fingerprints = input_hashes()
    folder.mkdir(parents=True, exist_ok=True)
    library = tool.runtime_library(folder)
    link_flags = ['/fixed:no', library]
    import_libraries = []
    for dll, policy in stdcall_imports.items():
        library_path, adjustments = stdcall_import_library(
            tool, folder / 'imports' / dll[:-4].lower(), dll,
            policy['symbols'], policy['export_names'])
        link_flags.append(tool.native(library_path))
        import_libraries.append({
            'dll': dll, 'export_names': policy['export_names'],
            'symbols': policy['symbols'],
            'library': str(library_path.relative_to(ROOT) if library_path.is_relative_to(ROOT) else library_path),
            'sha256': digest(library_path), 'name_mode_adjustments': adjustments})
    runtime_libraries = []
    if runtime_imports:
        library_path, adjustments = stdcall_import_library(
            tool, folder / 'imports/runtime', 'MSVCRT.dll', runtime_imports, 'undecorated')
        link_flags.append(tool.native(library_path))
        runtime_libraries.append({
            'dll': 'MSVCRT.dll', 'export_names': 'undecorated', 'symbols': runtime_imports,
            'library': str(library_path.relative_to(ROOT) if library_path.is_relative_to(ROOT) else library_path),
            'sha256': digest(library_path), 'name_mode_adjustments': adjustments})
    abi_support = []
    if any('/GX' in flags for flags in compile_options.values()):
        abi_object = folder / 'abi/vc6-except-list.obj'
        abi_object.parent.mkdir(exist_ok=True)
        abi_bytes = vc6_exception_abi_object()
        abi_object.write_bytes(abi_bytes)
        link_flags.append(tool.native(abi_object))
        abi_support.append({'object': str(abi_object.relative_to(ROOT) if abi_object.is_relative_to(ROOT) else abi_object),
                            'sha256': digest(abi_object), 'absolute_symbols': {'__except_list': 0},
                            'sections': 0, 'code_bytes': 0, 'data_bytes': 0})
    source_flags = {support / 'operator_delete.cpp': ['/O1']}
    for path, flags in compile_options.items():
        source_flags.setdefault(path, []).extend(flags)
    binary, pdb = tool.build(folder, [*sources, *support_sources, *anchors, entry], 'BeachLife', link_flags,
                            ['/MD', '/G6', '/I' + tool.native(tool.compiler.parent / 'Include'),
                             '/I' + tool.native(source)],
                            source_flags=source_flags)
    if verify_sdk_headers(metadata, tool.compiler.parent / 'Include') != sdk_headers:
        raise ValueError('SDK header pins changed during the build.')
    if abi_support and abi_object.read_bytes() != abi_bytes:
        raise ValueError('Generated VC6 exception ABI object changed during the build.')
    for library_record in [*import_libraries, *runtime_libraries]:
        library_path = ROOT / library_record['library']
        if digest(library_path) != library_record['sha256']:
            raise ValueError('Generated stdcall import library changed during the build.')
    compiled_stdcall_imports = verify_stdcall_pe_imports(binary, stdcall_imports, exact=True)
    compiled_runtime_imports = verify_runtime_compiled_imports(binary, runtime_imports, original_runtime_imports)
    # CSV metadata supplies the known float in the original, whose relocations
    # were stripped. Relocations in our comparison build identify its literal.
    save(folder / 'reccmp-project.yml', {'targets': {'BEACHLIFE': {
        'filename': 'BeachLife.exe', 'source-root': [str(source), str(support)] +
        ([str(anchors[0].parent)] if anchors else []),
        'hash': {'sha256': target['sha256']},
        'data-sources': [str(metadata / 'metadata.csv')],
        'report': {'ignore-functions': ['fixture_entry'] +
                   [item['name'] for item in anchor_records]}}}})
    save(folder / 'reccmp-user.yml', {'targets': {'BEACHLIFE': {'path': str(original)}}})
    save(folder / 'reccmp-build.yml', {'project': '.', 'targets': {'BEACHLIFE': {
        'path': str(binary), 'pdb': str(pdb)}}})
    reports = args.reports.resolve()
    reports.mkdir(parents=True, exist_ok=True)
    output = tool.run([tool.reccmp, '--target', 'BEACHLIFE', '--json',
                       str(reports / 'comparison.json'), '--no-color'], folder)
    (reports / 'comparison.log').write_text(output)
    if any(re.match(r'^\s*\[(?:ERROR|CRITICAL)\]', line) for line in output.splitlines()):
        raise ValueError('Comparator reported an error; inspect comparison.log.')
    report = json.loads((reports / 'comparison.json').read_text())
    def compare_functions(items):
        functions = []
        for item in items:
            row = next((row for row in report['data'] if int(row['address'], 16) == int(item['address'], 16)), None)
            if row is None:
                raise ValueError(f"Comparator did not find {item['name']}.")
            exact = read_va(original, int(item['address'], 16), item['size']) == read_va(binary, int(row['recomp'], 16), item['size'])
            functions.append({'name': item['name'], 'address': item['address'], 'bytes': item['size'],
                              'normalized_match': row['matching'], 'exact_bytes': exact})
        return functions
    functions = compare_functions(target['functions'])
    library_functions = compare_functions(target.get('library_functions', []))
    unclassified_functions = compare_functions(target.get('unclassified_functions', []))
    if manifest_path is not None:
        prepare(ROOT, manifest_path.parent, names=(), verify=True,
                decomp_revision=args.decomp_revision, decomp_profile='matching')
    if input_hashes() != fingerprints:
        raise ValueError('Matching inputs changed during comparison; rerun the comparison.')
    if verify_sdk_headers(metadata, tool.compiler.parent / 'Include') != sdk_headers:
        raise ValueError('SDK header pins changed during comparison.')
    result = {'target': target['id'], 'original_sha256': target['sha256'],
              'compiler': 'VC6 SP4 / C2 12.0.8799.0', 'reccmp': '0.1.7',
              'functions': functions, 'scope': 'Only listed functions; whole-game denominator unknown.',
              'library_functions': library_functions,
              'unclassified_functions': unclassified_functions,
              'source_compile_options': {path.name: flags for path, flags in sorted(compile_options.items())},
              'sdk_headers_sha256': sdk_headers,
              'link_abi_support': abi_support,
              'stdcall_import_libraries': import_libraries,
              'original_stdcall_imports': original_stdcall_imports,
              'compiled_stdcall_imports': compiled_stdcall_imports,
              'runtime_stdcall_import_libraries': runtime_libraries,
              'original_runtime_stdcall_imports': {
                  name: original_runtime_imports[name] for name in sorted(runtime_imports)},
              'compiled_runtime_stdcall_imports': compiled_runtime_imports,
              'input_sha256': fingerprints,
              'target_config': str(target_path.relative_to(ROOT) if target_path.is_relative_to(ROOT) else target_path),
              'source_root': str(source.relative_to(ROOT) if source.is_relative_to(ROOT) else source),
              'source_revision': getattr(args, 'decomp_revision', None),
              'comparison_layout': 'Relocations enabled in rebuilt comparison image; original remains unchanged.'}
    if anchor_records:
        result['comparison_anchors'] = anchor_records
        result['image_execution_permitted'] = False
    save(reports / 'result.json', result)
    print(json.dumps(result, indent=2))
    if any(item['normalized_match'] != 1 for item in functions):
        raise ValueError('One or more annotated functions do not match.')
    if any(item['normalized_match'] != 1 for item in library_functions):
        raise ValueError('One or more library support functions do not match.')
    if any(item['normalized_match'] != 1 for item in unclassified_functions):
        raise ValueError('One or more unclassified functions do not match.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', choices=['verify', 'fixture', 'stdio-fixture', 'platform-fixture', 'game'])
    parser.add_argument('--compiler', default=os.environ.get('BEACHLIFE_MSVC6'))
    parser.add_argument('--original', type=Path, default=ROOT / 'orig/analysis/disc/BeachLife.exe')
    parser.add_argument('--output', type=Path, default=ROOT / 'build/matching')
    parser.add_argument('--prefix', type=Path, default=ROOT / 'build/matching/wine')
    parser.add_argument('--reccmp', default='reccmp-reccmp')
    parser.add_argument('--source', type=Path, help='experimental source override; default is the matching export')
    parser.add_argument('--reports', type=Path, default=ROOT / 'orig/analysis/matching')
    args = parser.parse_args()
    try:
        if args.command == 'verify':
            target = json.loads(TARGET.read_text())
            path = verify_original(args.original, target)
            print(json.dumps({'target': target['id'], 'original': str(path), 'verified': True}))
        elif args.command == 'fixture':
            fixture(Toolchain(args))
        elif args.command == 'stdio-fixture':
            stdio_fixture(Toolchain(args))
        elif args.command == 'platform-fixture':
            platform_fixture(Toolchain(args))
        else:
            prepare_game_source(args)
            game_target(args.decomp_root)
            game(Toolchain(args), args)
    except (OSError, ValueError, RuntimeError, subprocess.CalledProcessError, subprocess.TimeoutExpired) as error:
        parser.exit(1, str(error) + '\n')


if __name__ == '__main__':
    main()
