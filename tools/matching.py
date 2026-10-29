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
    from .prepare_sources import no_symlinks, prepare
else:
    from matching_imports import (coff_import_symbols, stdcall_export_names,
                                 stdcall_import_library, validate_stdcall_imports,
                                 vc6_exception_abi_object)
    from prepare_sources import no_symlinks, prepare

ROOT = Path(__file__).resolve().parents[1]
TARGET = ROOT / 'decomp/targets/beachlife-uk-1.1.json'
SOURCE_PIN = ROOT / 'patches/beachlife-decomp/source.json'
STLPORT_PIN = ROOT / 'tools/stlport.json'
STLPORT_DEFAULT = ROOT / 'build/thirdparty-stlport/src/STLport-4.5.3/STLport-4.5.3'


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


def verify_stlport(root):
    """Verify the entire external include tree against the archive-derived pin."""
    root = Path(root).resolve()
    include = root / 'stlport'
    if not include.is_dir():
        raise ValueError('Set --stlport or BEACHLIFE_STLPORT to the extracted STLport 4.5.3 root.')
    pin = json.loads(STLPORT_PIN.read_text())
    headers = {}
    no_symlinks(include, root)
    for path in sorted(include.rglob('*')):
        no_symlinks(path, root)
        if path.is_dir():
            continue
        if not path.is_file():
            raise ValueError(f'Not a regular STLport header: {path}')
        headers[path.relative_to(include).as_posix()] = digest(path)
    fingerprint = hashlib.sha256(''.join(
        f'{name}\0{headers[name]}\n' for name in sorted(headers)).encode('utf-8')).hexdigest()
    if (len(headers), fingerprint) != (pin['header_files'], pin['headers_sha256']):
        raise ValueError('STLport header tree does not match the pinned 4.5.3 archive.')
    return include, {include / name: value for name, value in headers.items()}


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
        self.timeout = getattr(args, 'timeout', 60)
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
                                text=True, errors='replace', timeout=self.timeout)
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

    def build(self, folder, sources, name, extra_link=(), extra_compile=(), source_flags=None,
              anchor_symbols=None, source_includes=None):
        folder.mkdir(parents=True, exist_ok=True)
        objects, logs = [], []
        for source in sources:
            obj = source.stem + '.obj'
            if obj in objects:
                raise ValueError('Matching source basenames must be unique.')
            objects.append(obj)
            options = list((source_flags or {}).get(source, ()))
            packaged = '/Gy-' not in options
            # VC6 does not accept /Gy-, and /O2 implicitly enables /Gy.
            # /Ox retains speed optimization without packaging functions into
            # COMDATs, so an audited identity can remain distinct under /OPT:ICF.
            if not packaged:
                options.remove('/Gy-')
            flags = ['/nologo', '/c', '/O2' if packaged else '/Ox', '/Ob0', '/Zi', '/Gd',
                     *(['/Gy'] if packaged else []),
                     '/Fdcompile.pdb', '/Fo' + obj,
                     *('/I' + self.native(path) for path in (source_includes or {}).get(source, ())),
                     *extra_compile,
                     *options, self.native(source)]
            self.response(folder / 'compile.rsp', flags)
            command = [str(self.compiler / 'CL.EXE'), '@compile.rsp']
            logs.append(self.run(command if os.name == 'nt' else ['wine', *command], folder))
            if source in (anchor_symbols or {}):
                verify_comparison_anchor_object(folder / obj, anchor_symbols[source])
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


def registration_files(metadata, directory, pattern):
    """Keep legacy inputs first, then address-keyed additions in filename order."""
    folder = metadata / directory
    no_symlinks(folder, metadata)
    if not folder.exists():
        return []
    if not folder.is_dir():
        raise ValueError(f'Invalid matching registration directory: {folder}')
    files = sorted(folder.iterdir())
    for path in files:
        no_symlinks(path, metadata)
        if not path.is_file() or not re.fullmatch(pattern, path.name):
            raise ValueError(f'Invalid matching registration file: {path}')
    return files


def game_metadata_paths(metadata):
    return [metadata / 'metadata.csv',
            *registration_files(metadata, 'metadata.d', r'[0-9a-f]{8}\.csv')]


def game_metadata_rows(metadata):
    rows, seen = [], {}
    paths = game_metadata_paths(metadata)
    for index, path in enumerate(paths):
        with path.open(newline='') as stream:
            reader = csv.DictReader(stream)
            if index and reader.fieldnames != ['address', 'type', 'size', 'symbol']:
                raise ValueError(f'Matching metadata fragment needs address,type,size,symbol: {path}')
            for row in reader:
                address = int(row['address'], 16)
                if len(paths) > 1 and address in seen:
                    # Parallel registrations can each name a callee they share;
                    # only the same identity may appear twice.
                    if (seen[address]['type'], seen[address]['size'], seen[address]['symbol']) != (
                            row['type'], row['size'], row['symbol']):
                        raise ValueError(f'Duplicate matching metadata address: {row["address"]}')
                    continue
                seen[address] = row
                rows.append(row)
    return rows


def merge_target_fragments(metadata, target):
    lists = ('functions', 'library_functions', 'unclassified_functions', 'comparison_anchors',
             'stlport_sources')
    mappings = ('source_compile_options', 'stdcall_imports', 'runtime_stdcall_imports')
    for path in registration_files(metadata, 'targets/beachlife-uk-1.1.d', r'[0-9a-f]{8}\.json'):
        fragment = json.loads(path.read_text())
        if not isinstance(fragment, dict) or not fragment or set(fragment) - set(lists + mappings):
            raise ValueError(f'Unsupported matching target fragment: {path}')
        for key, value in fragment.items():
            if key in lists:
                if not isinstance(value, list) or not isinstance(target.get(key, []), list):
                    raise ValueError(f'{key} must be a list in matching target fragment: {path}')
                existing = target.setdefault(key, [])
                if key == 'comparison_anchors':
                    # A callee anchor shared by parallel registrations is kept
                    # once; a different anchor at its address still fails later.
                    value = [item for item in value if item not in existing]
                existing.extend(value)
            else:
                if not isinstance(value, dict) or not isinstance(target.get(key, {}), dict):
                    raise ValueError(f'{key} must be a mapping in matching target fragment: {path}')
                existing = target.setdefault(key, {})
                if existing.keys() & value.keys():
                    raise ValueError(f'Duplicate {key} entries in matching target fragment: {path}')
                existing.update(value)
    drop_superseded_anchors(metadata, target)


def drop_superseded_anchors(metadata, target):
    """Let a matched body replace an anchor that a parallel registration kept for it."""
    anchors = target.get('comparison_anchors')
    if not isinstance(anchors, list) or not anchors:
        return
    def identity(item):
        if isinstance(item, dict) and re.fullmatch(r'0x[0-9a-fA-F]{1,8}', str(item.get('address'))):
            return int(item['address'], 16), item.get('size')
        return None
    credited = {identity(item) for key in ('functions', 'library_functions', 'unclassified_functions')
                for item in (target.get(key) if isinstance(target.get(key), list) else [])} - {None}
    symbols = {(int(row['address'], 16), row['symbol']) for row in game_metadata_rows(metadata)}
    def superseded(item):
        # Only the same address, extent and symbol; anything else still fails validation.
        key = identity(item)
        return key in credited and (key[0], item.get('symbol')) in symbols
    target['comparison_anchors'] = [item for item in anchors if not superseded(item)]


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
                len(flags) != len(set(flags)) or any(flag not in ('/GX', '/Ob1', '/Gy-') for flag in flags)):
            raise ValueError('Source compiler options must be unique entries from /GX, /Ob1 and /Gy-.')
        result[available[name]] = flags.copy()
    return result


def game_stlport_sources(target, sources):
    """Opt in complete translation units, including their retention fixtures."""
    configured = target.get('stlport_sources', [])
    if (not isinstance(configured, list) or any(not isinstance(name, str) for name in configured) or
            len(configured) != len(set(configured))):
        raise ValueError('stlport_sources must be a list of unique source basenames.')
    available = {}
    for source in sources:
        if source.name in available or not source.is_file():
            raise ValueError('Matching source basenames must identify unique files.')
        available[source.name] = source
    for name in configured:
        if not re.fullmatch(r'[A-Za-z0-9_][A-Za-z0-9_.-]*\.cpp', name) or name not in available:
            raise ValueError('STLport must name an existing game, support or fixture source basename.')
    return [available[name] for name in sorted(configured)]


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


ANCHOR_IDENTIFIER = r'[A-Za-z_][A-Za-z_0-9]*'
ANCHOR_NAME = ANCHOR_IDENTIFIER + r'(?:::' + ANCHOR_IDENTIFIER + r')*'
ANCHOR_INTEGERS = ('char', 'signed char', 'unsigned char', 'short', 'unsigned short',
                   'int', 'unsigned int', 'long', 'unsigned long')
ANCHOR_SCALARS = (*ANCHOR_INTEGERS, 'bool', 'float', 'double')
# These tokens cannot be a typedef/class name in the deliberately small grammar.
ANCHOR_KEYWORDS = set(('void bool char short int long signed unsigned float double const volatile '
                       'auto class struct union enum typename template decltype static extern register '
                       'mutable inline virtual friend typedef operator new delete return __cdecl '
                       '__stdcall __fastcall __thiscall').split())


def anchor_named_type(value):
    return (isinstance(value, str) and re.fullmatch(ANCHOR_NAME, value) is not None and
            not (set(value.split('::')) & ANCHOR_KEYWORDS))


def anchor_type(value):
    """Allow scalar/named values, const, up to two pointers, and lvalue references.

    Callback parameters use a header typedef or the separate structured shape.
    Declarators, templates, arrays, defaults and expressions are intentionally
    excluded; this is not a C++ parser.
    """
    if not isinstance(value, str):
        return None
    match = re.fullmatch(r'(const )?(' + ANCHOR_NAME + r'|unsigned (?:char|short|int|long)|signed char)'
                         r'((?:\*(?: const)?){0,2})(&?)', value)
    if not match:
        return None
    base, pointers, reference = match[2], match[3], match[4]
    if base not in (*ANCHOR_SCALARS, 'void') and not anchor_named_type(base):
        return None
    if base == 'void' and not pointers:
        return None
    return base, pointers, reference


def anchor_parameter(parameter, name=''):
    """Render only a simple type or a bounded, non-nested callback declarator."""
    if anchor_type(parameter) is not None:
        return parameter + (' ' + name if name else '')
    if (not isinstance(parameter, dict) or
            set(parameter) != {'return_type', 'parameters', 'calling_convention'} or
            parameter['calling_convention'] not in ('__cdecl', '__stdcall') or
            not isinstance(parameter['parameters'], list) or len(parameter['parameters']) > 16 or
            not all(anchor_type(value) is not None for value in parameter['parameters'])):
        return None
    result = parameter['return_type']
    parsed = anchor_type(result)
    if not (result == 'void' or result in ANCHOR_SCALARS or
            (parsed is not None and parsed[1] and not parsed[2])):
        return None
    return (f'{result} ({parameter["calling_convention"]} *{name})'
            f'({", ".join(parameter["parameters"])})')


def anchor_declaration(item):
    """Validate the signature shape; the compiler checks the shared declaration."""
    required = {'address', 'size', 'name', 'symbol', 'header', 'kind', 'return_type', 'parameters'}
    optional = {'calling_convention', 'qualifier'}
    if (not isinstance(item, dict) or not required <= set(item) or set(item) - required - optional or
            not isinstance(item['kind'], str) or
            not anchor_named_type(item['name']) or not isinstance(item['return_type'], str)):
        return False
    kinds = ('void', 'null-pointer', 'false', 'integer', 'float', 'double', 'aggregate', 'null-callback')
    shape, separator, owner = item['kind'].rpartition('-')
    if not separator or shape not in kinds or owner not in ('member', 'function'):
        return False
    parts = item['name'].split('::')
    if owner == 'member':
        if (len(parts) < 2 or parts[-1] == parts[-2] or 'calling_convention' in item or
                item.get('qualifier', '') not in ('', 'const')):
            return False
    elif 'qualifier' in item or item.get('calling_convention', '__cdecl') not in ('__cdecl', '__stdcall'):
        return False
    result = item['return_type']
    parsed = anchor_type(result)
    returns = {'void': result == 'void',
               'null-pointer': parsed is not None and bool(parsed[1]) and not parsed[2],
               'false': result in ('bool', 'unsigned char'),
               'integer': result in ANCHOR_INTEGERS,
               'float': result == 'float', 'double': result == 'double',
               'aggregate': anchor_named_type(result),
               'null-callback': anchor_named_type(result)}
    return (returns[shape] and isinstance(item['parameters'], list) and
            len(item['parameters']) <= 16 and
            all(anchor_parameter(parameter) is not None for parameter in item['parameters']))


def verify_comparison_anchor_object(path, expected):
    """Require the exact defined function identity, not an undefined reference."""
    data = path.read_bytes()
    if len(data) < 20 or struct.unpack_from('<H', data)[0] != 0x14c:
        raise ValueError('Comparison anchor needs an x86 COFF object.')
    count_sections, = struct.unpack_from('<H', data, 2)
    offset, count = struct.unpack_from('<II', data, 8)
    optional_size, = struct.unpack_from('<H', data, 16)
    sections = 20 + optional_size
    strings = offset + count * 18
    if (optional_size or sections + count_sections * 40 > len(data) or
            offset < sections + count_sections * 40 or strings + 4 > len(data)):
        raise ValueError('Truncated comparison anchor COFF tables.')
    string_size, = struct.unpack_from('<I', data, strings)
    if string_size < 4 or strings + string_size > len(data):
        raise ValueError('Truncated comparison anchor COFF strings.')
    definitions, index = [], 0
    while index < count:
        entry = offset + index * 18
        auxiliary = data[entry + 17]
        if index + 1 + auxiliary > count:
            raise ValueError('Truncated comparison anchor COFF auxiliary records.')
        name = data[entry:entry + 8]
        if name[:4] == b'\0' * 4:
            start, = struct.unpack_from('<I', name, 4)
            if not 4 <= start < string_size:
                raise ValueError('Invalid comparison anchor COFF name offset.')
            end = data.find(b'\0', strings + start, strings + string_size)
            if end == -1:
                raise ValueError('Unterminated comparison anchor COFF name.')
            name = data[strings + start:end]
        else:
            name = name.rstrip(b'\0')
        section_number, symbol_type, storage = struct.unpack_from('<hHB', data, entry + 12)
        if name == expected.encode('ascii') and storage == 2 and section_number > 0:
            if section_number > count_sections:
                raise ValueError('Invalid comparison anchor COFF section.')
            section_header = sections + (section_number - 1) * 40
            flags, = struct.unpack_from('<I', data, section_header + 36)
            size, = struct.unpack_from('<I', data, section_header + 16)
            value, = struct.unpack_from('<I', data, entry + 8)
            if symbol_type != 0x20 or not flags & 0x20 or value >= size:
                raise ValueError('Comparison anchor symbol is not a defined code function.')
            definitions.append(name)
        index += 1 + auxiliary
    if len(definitions) != 1:
        raise ValueError(f'Comparison anchor must define exactly the declared symbol: {expected}')


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
    rows = game_metadata_rows(metadata)
    credited_addresses = {int(item['address'], 16) for item in credited}
    symbols = {row['symbol'] for row in rows if int(row['address'], 16) in credited_addresses}
    for item in specs:
        if (not anchor_declaration(item) or
                not isinstance(item.get('address'), str) or
                not re.fullmatch(r'0x[0-9a-fA-F]{1,8}', item['address']) or
                type(item.get('size')) is not int or item['size'] <= 0 or
                not isinstance(item.get('symbol'), str) or
                not re.fullmatch(r'(?:\?[^,\s]+|_[A-Za-z_][A-Za-z_0-9]*(?:@[0-9]+)?)', item['symbol']) or
                not isinstance(item.get('header'), str) or
                not re.fullmatch(r'[A-Za-z_][A-Za-z_0-9]*\.h', item['header'])):
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
        names = (['argument'] if len(item['parameters']) == 1 else
                 ['argument' + str(index) for index in range(1, len(item['parameters']) + 1)])
        parameters = ', '.join(anchor_parameter(parameter, name)
                               for parameter, name in zip(item['parameters'], names))
        shape, _, owner = item['kind'].rpartition('-')
        if shape == 'void':
            statement = ''.join('    (void)' + name + ';\n' for name in names)
        else:
            value = {'null-pointer': '0', 'false': 'false', 'integer': '0',
                     'float': '0.0f', 'double': '0.0', 'aggregate': item['return_type'] + '()',
                     'null-callback': '0'}[shape]
            statement = f'    return {value};\n'
        declaration, convention = '', ''
        if owner == 'function':
            convention = item.get('calling_convention', '__cdecl') + ' '
            # A free definition alone could invent a missing declaration. Resolve
            # the precise typed address BEFORE defining it, against the header.
            declaration = (f'typedef {item["return_type"]} ({convention}*comparison_anchor_signature)'
                           f'({", ".join(anchor_parameter(parameter) for parameter in item["parameters"])});\n'
                           'static comparison_anchor_signature const comparison_anchor_declaration = '
                           f'&{item["name"]};\n\n')
        qualifier = ' ' + item['qualifier'] if item.get('qualifier') else ''
        # A file-local marker keeps identical-COMDAT folding from merging an
        # anchor with another anchor or with a credited body of the same code.
        body = (f'#include "{item["header"]}"\n\n'
                '// GENERATED COMPARISON-ONLY ANCHOR. Never execute this image.\n'
                '// This is no implementation or qualification of the original dependency.\n'
                'static volatile unsigned int comparison_anchor_marker;\n\n' + declaration +
                f'// FUNCTION: BEACHLIFE {item["address"]}\n'
                f'{item["return_type"]} {convention}{item["name"]}({parameters}){qualifier}\n'
                '{\n'
                f'    comparison_anchor_marker = {item["address"]};\n' + statement + '}\n')
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
    merge_target_fragments(metadata, target)
    game_metadata_rows(metadata)
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
    stlport_include, stlport_headers = verify_stlport(args.stlport)
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
    fixtures = registration_files(metadata, 'fixtures/game.d', r'fixture_[0-9a-f]{8}\.cpp')
    stlport_sources = game_stlport_sources(target, [*sources, *support_sources, entry, *fixtures])
    folder = tool.output / 'game'
    # Report consumers use one fingerprinted target, including fragment entries.
    # Keep the legacy target path and bytes unchanged when no fragments exist.
    if registration_files(metadata, 'targets/beachlife-uk-1.1.d', r'[0-9a-f]{8}\.json'):
        folder.mkdir(parents=True, exist_ok=True)
        target_path = folder / 'target.json'
        save(target_path, target)
    anchors, anchor_records = generate_comparison_anchors(
        metadata, source, folder, comparison_anchor_specs(metadata, target))
    inputs = set([target_path, *sources, *anchors, *source.rglob('*.h'),
                  *(path for path in metadata.rglob('*') if path.is_file()),
                  ROOT / 'tools/matching_imports.py', ROOT / 'tools/prepare_sources.py',
                  STLPORT_PIN, *stlport_headers,
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
    for path in stlport_sources:
        source_flags.setdefault(path, []).append('/D_STLP_NO_OWN_IOSTREAMS=1')
    binary, pdb = tool.build(folder, [*sources, *support_sources, *anchors, entry, *fixtures], 'BeachLife', link_flags,
                            ['/MD', '/G6', '/I' + tool.native(tool.compiler.parent / 'Include'),
                             '/I' + tool.native(source)],
                            source_flags=source_flags,
                            **({'source_includes': {path: [stlport_include] for path in stlport_sources}}
                               if stlport_sources else {}),
                            **({'anchor_symbols': {path: record['symbol'] for path, record in
                                                   zip(anchors, anchor_records)}} if anchors else {}))
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
        'data-sources': [str(path) for path in game_metadata_paths(metadata)],
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
    # Identical-code folding gives several symbols one rebuilt address and the
    # comparator reports only one of their names. A folded function counts only
    # when its rebuilt bytes equal the original bytes exactly.
    rebuilt = {}
    for line in binary.with_suffix('.map').read_text(errors='replace').splitlines():
        parts = line.split()
        if len(parts) >= 3 and parts[1].startswith('?') and re.fullmatch(r'[0-9a-fA-F]{8}', parts[2]):
            rebuilt.setdefault(parts[1], int(parts[2], 16))
    symbol_at = {int(row['address'], 16): row['symbol'] for row in game_metadata_rows(metadata)
                 if row.get('type') == 'function' and row.get('symbol')}
    def compare_functions(items):
        functions = []
        for item in items:
            row = next((row for row in report['data'] if int(row['address'], 16) == int(item['address'], 16)), None)
            if row is None:
                address = rebuilt.get(symbol_at.get(int(item['address'], 16), ''))
                if address is None or read_va(original, int(item['address'], 16), item['size']) != \
                        read_va(binary, address, item['size']):
                    raise ValueError(f"Comparator did not find {item['name']}.")
                functions.append({'name': item['name'], 'address': item['address'], 'bytes': item['size'],
                                  'normalized_match': 1.0, 'exact_bytes': True, 'folded': True})
                continue
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
    if verify_stlport(args.stlport)[1] != stlport_headers:
        raise ValueError('STLport headers changed during comparison.')
    if verify_sdk_headers(metadata, tool.compiler.parent / 'Include') != sdk_headers:
        raise ValueError('SDK header pins changed during comparison.')
    result = {'target': target['id'], 'original_sha256': target['sha256'],
              'compiler': 'VC6 SP4 / C2 12.0.8799.0', 'reccmp': '0.1.7',
              'functions': functions, 'scope': 'Only listed functions; whole-game denominator unknown.',
              'library_functions': library_functions,
              'unclassified_functions': unclassified_functions,
              'source_compile_options': {path.name: flags for path, flags in sorted(compile_options.items())},
              'sdk_headers_sha256': sdk_headers,
              'stlport_root': str(stlport_include.parent),
              'stlport_sources': [path.name for path in stlport_sources],
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
    parser.add_argument('--stlport', type=Path, default=os.environ.get('BEACHLIFE_STLPORT', STLPORT_DEFAULT),
                        help='extracted, pinned STLport 4.5.3 root (containing stlport/); matching only')
    parser.add_argument('--original', type=Path, default=ROOT / 'orig/analysis/disc/BeachLife.exe')
    parser.add_argument('--output', type=Path, default=ROOT / 'build/matching')
    parser.add_argument('--prefix', type=Path, default=ROOT / 'build/matching/wine')
    parser.add_argument('--reccmp', default='reccmp-reccmp')
    parser.add_argument('--source', type=Path, help='experimental source override; default is the matching export')
    parser.add_argument('--reports', type=Path, default=ROOT / 'orig/analysis/matching')
    parser.add_argument('--timeout', type=int, default=int(os.environ.get('BEACHLIFE_MATCHING_TIMEOUT', 60)),
                        help='seconds allowed per compiler, linker or reccmp command (default 60)')
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
