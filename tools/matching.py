"""Build and compare small x86 matching targets with the verified VC6 toolchain."""
import argparse
import bisect
import csv
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys

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

    def runtime_library(self, folder, exports=()):
        folder.mkdir(parents=True, exist_ok=True)
        shutil.copy2(self.decomp_root / 'msvcrt.def', folder / 'msvcrt.def')
        if exports:
            (folder / 'msvcrt.def').write_text(runtime_definition(
                (self.decomp_root / 'msvcrt.def').read_text(), exports))
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


def pe_base_relocations(path):
    """Sorted virtual addresses of the 32-bit operands a PE32 image relocates."""
    data = Path(path).read_bytes()
    section(path, b'.text')
    pe = struct.unpack_from('<I', data, 0x3c)[0]
    if pe+168 > len(data) or struct.unpack_from('<H', data, pe+24)[0] != 0x10b:
        raise ValueError('Expected a complete PE32 optional header.')
    base = struct.unpack_from('<I', data, pe+52)[0]
    rva, size = struct.unpack_from('<II', data, pe+160)
    if struct.unpack_from('<I', data, pe+116)[0] <= 5 or not rva or not size:
        return []
    table = read_va(path, base+rva, size)
    result, position = [], 0
    while position < size:
        if position+8 > size:
            raise ValueError('Truncated PE base relocation block.')
        page, length = struct.unpack_from('<II', table, position)
        if length < 8 or length % 2 or position+length > size:
            raise ValueError('Invalid PE base relocation block.')
        for offset in range(position+8, position+length, 2):
            entry, = struct.unpack_from('<H', table, offset)
            if entry >> 12 == 3:
                result.append(base+page+(entry & 0xfff))
            elif entry >> 12:
                raise ValueError(f'Unsupported PE base relocation type {entry >> 12}.')
        position += length
    return sorted(result)


def map_public_names(path):
    """Every public and static name of a LINK map, by rebuilt virtual address."""
    names = {}
    for line in Path(path).read_text(errors='replace').splitlines():
        parts = line.split()
        if len(parts) >= 3 and ':' in parts[0] and re.fullmatch(r'[0-9a-fA-F]{8}', parts[2]):
            names.setdefault(int(parts[2], 16), set()).add(parts[1])
    return names


# Code identities that may be credited through a folded alias, and the data or
# code identities a relocated operand of such an alias may name.
FOLDED_ALIAS_TYPES = ('function', 'library')
OPERAND_IDENTITY_TYPES = ('function', 'library', 'global')


def relocated_operand_identities(original, binary, rows, publics):
    """Name the targets of absolute operands: an import slot by DLL and symbol,
    otherwise the exact CSV identity (original) or LINK map names (rebuilt)."""
    def slots(path):
        return {int(slot, 16): f'import:{dll.lower()}!{name}'
                for dll, names in pe_imports(path).items() for name, slot in names.items()}
    original_slots, rebuilt_slots = slots(original), slots(binary)
    original_symbols = {int(row['address'], 16): 'symbol:' + row['symbol'] for row in rows
                        if row.get('type') in OPERAND_IDENTITY_TYPES and row.get('symbol')}
    def original_identity(value):
        return original_slots.get(value) or original_symbols.get(value)
    def rebuilt_identities(value):
        found = {'symbol:' + name for name in publics.get(value, ())}
        if value in rebuilt_slots:
            found.add(rebuilt_slots[value])
        return found
    return original_identity, rebuilt_identities


def compare_relocated_alias(original_code, rebuilt_code, original_address, rebuilt_address,
                            relocations, relative, original_identity, rebuilt_identities):
    """Relocation-normalized comparison of one folded alias body.

    Every byte outside the operands that LINK resolves must be identical, so both
    sides decode to the same instructions. Absolute operands are the 32-bit
    operands the rebuilt image relocates; relative call or jump operands are the
    REL32 relocations of the alias's own COFF function. Each operand must name the
    same identity on both sides (the original has no relocations; its operand is
    resolved by value). Returns the operand list, or None when anything differs
    or cannot be identified."""
    size = len(rebuilt_code)
    if len(original_code) != size:
        return None
    first = bisect.bisect_left(relocations, rebuilt_address-3)
    last = bisect.bisect_left(relocations, rebuilt_address+size)
    sites = sorted([(site-rebuilt_address, False) for site in relocations[first:last]] +
                   [(offset, True) for offset in relative])
    operands, position = [], 0
    for offset, is_relative in sites:
        # An operand that crosses the extent or another operand is not comparable.
        if offset < position or offset+4 > size:
            return None
        if original_code[position:offset] != rebuilt_code[position:offset]:
            return None
        values = [struct.unpack_from('<i' if is_relative else '<I', code, offset)[0]
                  for code in (original_code, rebuilt_code)]
        if is_relative:
            values = [(start+offset+4+value) & 0xffffffff
                      for start, value in zip((original_address, rebuilt_address), values)]
        identity = original_identity(values[0])
        if identity is None or identity not in rebuilt_identities(values[1]):
            return None
        operands.append({'offset': offset, 'identity': identity, **({'relative': True} if is_relative else {})})
        position = offset+4
    if original_code[position:] != rebuilt_code[position:]:
        return None
    return operands


def folded_relative_operands(path, symbol, rebuilt_code):
    """REL32 operand offsets of one folded alias, read from its own COFF function.

    Identical-code folding merges only identical sections with identical
    relocations, so that function must be the rebuilt body byte for byte apart
    from its relocated operands; otherwise nothing is returned."""
    code, relocations = comparison_operand_coff(path, symbol)
    if len(code) < len(rebuilt_code):
        return []
    masked = [bytearray(code[:len(rebuilt_code)]), bytearray(rebuilt_code)]
    for offset, _, _, _ in relocations:
        for data in masked:
            data[offset:offset+4] = bytes(len(data[offset:offset+4]))
    if masked[0] != masked[1]:
        return []
    return [offset for offset, kind, _, _ in relocations if kind == 0x14 and offset < len(rebuilt_code)]


def folded_alias_objects(folder, linked, mapped, symbol):
    """Linked objects that define a folded alias's own symbol as a code function.

    The LINK map names the object whose section identical-code folding kept,
    which can be another folded symbol's object; then every linked object that
    defines this symbol is a candidate instead."""
    objects = [line.strip('"') for line in linked if re.fullmatch(r'"[^\\/:"]+\.obj"', line)]
    def defines(name):
        try:
            verify_comparison_anchor_object(folder / name, symbol)
        except (OSError, ValueError):
            return False
        return True
    if objects.count(mapped) == 1 and defines(mapped):
        return [mapped]
    return [name for name in objects if name != mapped and defines(name)]


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
             'comparison_lifetime_anchors', 'comparison_global_operands',
             'stlport_sources', 'runtime_cdecl_imports')
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
                if key in ('comparison_anchors', 'comparison_lifetime_anchors', 'runtime_cdecl_imports'):
                    # A callee anchor or CRT import shared by parallel registrations
                    # is kept once; a different anchor at its address still fails later.
                    value = [item for index, item in enumerate(value)
                             if item not in existing and item not in value[:index]]
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
    for key in ('comparison_anchors', 'comparison_lifetime_anchors'):
        drop_superseded(metadata, target, key)


def drop_superseded(metadata, target, key):
    anchors = target.get(key)
    if not isinstance(anchors, list) or not anchors:
        return
    def identity(item):
        if isinstance(item, dict) and re.fullmatch(r'0x[0-9a-fA-F]{1,8}', str(item.get('address'))):
            return int(item['address'], 16), item.get('size')
        return None
    credited = {identity(item) for group in ('functions', 'library_functions', 'unclassified_functions')
                for item in (target.get(group) if isinstance(target.get(group), list) else [])} - {None}
    symbols = {(int(row['address'], 16), row['symbol']) for row in game_metadata_rows(metadata)}
    def superseded(item):
        # Only the same address, extent and symbol; anything else still fails validation.
        found = identity(item)
        return found in credited and (found[0], item.get('symbol')) in symbols
    target[key] = [item for item in anchors if not superseded(item)]


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


def game_runtime_cdecl_imports(target):
    """Undecorated CRT functions a registration needs from the generated import library."""
    configured = target.get('runtime_cdecl_imports', [])
    if (not isinstance(configured, list) or
            any(not isinstance(name, str) or not re.fullmatch(r'_{0,2}[A-Za-z][A-Za-z0-9_]*', name)
                for name in configured) or len(configured) != len(set(configured))):
        raise ValueError('runtime_cdecl_imports must list unique undecorated MSVCRT function names.')
    return sorted(configured)


def verify_runtime_cdecl_imports(original, configured):
    """Never invent an import: the retail image must import each name from MSVCRT.dll."""
    if not configured:
        return {}
    actual = pe_imports(original).get('MSVCRT.dll', {})
    missing = [name for name in configured if name not in actual]
    if missing:
        raise ValueError(f'The original does not import {", ".join(missing)} from MSVCRT.dll.')
    return {name: actual[name] for name in configured}


def runtime_definition(text, exports):
    """Append missing names to the EXPORTS of the prepared module definition."""
    lines = text.splitlines()
    if 'EXPORTS' not in (line.strip() for line in lines):
        raise ValueError('The prepared msvcrt.def has no EXPORTS section.')
    present = {line.split()[0] for line in lines[[line.strip() for line in lines].index('EXPORTS') + 1:]
               if line.strip()}
    return '\n'.join([*lines, *('    ' + name for name in exports if name not in present)]) + '\n'


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
    signatures = set()
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
        # Overloads share a qualified name, but never a decorated identity or
        # range. Return types do not distinguish C++ overloads. The compiler
        # and the exact COFF symbol check remain authoritative for typedefs.
        signature = (item['name'], tuple(anchor_parameter(value) for value in item['parameters']),
                     item.get('qualifier', ''))
        if (end > 0x100000000 or item['name'] in names or item['symbol'] in symbols or
                signature in signatures or
                any(start < right and left < end for left, right in ranges)):
            raise ValueError('Comparison-only anchors must be distinct from credited functions and each other.')
        rows_at_address = [row for row in rows if int(row['address'], 16) == start]
        if (len(rows_at_address) != 1 or rows_at_address[0]['type'] != 'function' or
                rows_at_address[0]['size'] != str(item['size']) or
                rows_at_address[0]['symbol'] != item['symbol']):
            raise ValueError('Comparison-only anchor must match one explicit CSV identity.')
        ranges.append((start, end))
        signatures.add(signature)
        symbols.add(item['symbol'])
    return specs


def comparison_global_operand_specs(metadata, target):
    """Allow only explicit, instruction-scoped one-past-the-end array operands.

    The CSV keeps the real array extent: neither the array nor the entity that
    starts at its end address is renamed or resized. The addend is that extent,
    plus an optional member offset inside the element one past the end (a loop
    over &array[i].member); element_size must divide the extent and exceed the
    member offset. COFF and linked-image checks follow in the comparator,
    before the operand can affect a score.
    """
    specs = target.get('comparison_global_operands', [])
    if not isinstance(specs, list):
        raise ValueError('comparison_global_operands must be a list.')
    functions = {int(item['address'], 16): item
                 for key in ('functions', 'library_functions', 'unclassified_functions')
                 for item in target.get(key, [])}
    rows = game_metadata_rows(metadata)
    result, seen = [], set()
    for item in specs:
        required = {'function', 'instruction_offset', 'global', 'source'}
        if (not isinstance(item, dict) or
                set(item) not in (required, required | {'member_offset', 'element_size'}) or
                any(type(item.get(key, 0)) is not int
                    for key in ('member_offset', 'element_size')) or
                any(not isinstance(item[key], str) or
                    not re.fullmatch(r'0x[0-9a-fA-F]{1,8}', item[key])
                    for key in ('function', 'global')) or
                type(item['instruction_offset']) is not int or item['instruction_offset'] < 0 or
                not isinstance(item['source'], str) or
                not re.fullmatch(r'[A-Za-z_][A-Za-z_0-9]*\.cpp', item['source'])):
            raise ValueError('Unsupported comparison global operand.')
        function, address = int(item['function'], 16), int(item['global'], 16)
        key = function, item['instruction_offset']
        if (function not in functions or key in seen or
                item['instruction_offset'] + 5 > functions[function]['size']):
            raise ValueError('Global operand needs a unique instruction in a registered function.')
        seen.add(key)
        identities = []
        for start, kind in ((function, 'function'), (address, 'global')):
            selected = [row for row in rows if int(row['address'], 16) == start]
            if (len(selected) != 1 or selected[0]['type'] != kind or
                    not re.fullmatch(r'[1-9][0-9]*', selected[0]['size']) or
                    not re.fullmatch(r'(?:\?[^,\s]+|_[A-Za-z_][A-Za-z_0-9]*)', selected[0]['symbol']) or
                    start + int(selected[0]['size']) >= 0x100000000):
                raise ValueError('Global operand needs exact explicit function and global CSV identities.')
            identities.append(selected[0])
        if int(identities[0]['size']) != functions[function]['size']:
            raise ValueError('Global operand function extent disagrees with its CSV identity.')
        size, member = int(identities[1]['size']), item.get('member_offset', 0)
        element = item.get('element_size', size)
        if not (0 < element <= size and size % element == 0 and 0 <= member < element):
            raise ValueError('Global operand member offset must lie inside one real element.')
        result.append(dict(item, function_symbol=identities[0]['symbol'],
                           function_size=int(identities[0]['size']),
                           global_symbol=identities[1]['symbol'], size=size, addend=size + member))
    return result


def comparison_operand_coff(path, expected):
    """Return one defined function's COFF bytes and its bounded relocations.

    Each relocation is (function offset, type, target symbol, stored addend).
    """
    verify_comparison_anchor_object(path, expected)
    data = path.read_bytes()
    count_sections, = struct.unpack_from('<H', data, 2)
    offset, count = struct.unpack_from('<II', data, 8)
    strings = offset + count * 18
    symbols, index = {}, 0
    while index < count:
        entry = offset + index * 18
        name = data[entry:entry + 8]
        if name[:4] == b'\0' * 4:
            start = strings + struct.unpack_from('<I', name, 4)[0]
            name = data[start:data.index(b'\0', start)]
        value, section_number, kind, storage, auxiliary = struct.unpack_from('<IhHBB', data, entry + 8)
        symbols[index] = (name.rstrip(b'\0').decode('ascii'), value, section_number, kind, storage)
        index += auxiliary + 1
    name, value, section_number, _, _ = next(
        item for item in symbols.values() if item[0] == expected and item[2] > 0 and item[4] == 2)
    header = 20 + (section_number - 1) * 40
    length, raw, relocs = struct.unpack_from('<III', data, header + 16)
    count_relocs, = struct.unpack_from('<H', data, header + 32)
    flags, = struct.unpack_from('<I', data, header + 36)
    if (section_number > count_sections or raw < 20 + count_sections * 40 or raw + length > offset or
            count_relocs == 0xffff or flags & 0x01000000 or
            (count_relocs and (relocs < raw + length or relocs + count_relocs * 10 > offset))):
        raise ValueError('Invalid comparison operand COFF data or relocations.')
    end = min([length, *(item[1] for item in symbols.values()
                        if item[2] == section_number and item[3] == 0x20 and item[1] > value)])
    result = []
    for index in range(count_relocs):
        position, symbol, kind = struct.unpack_from('<IIH', data, relocs + index * 10)
        if position + 4 > length or symbol not in symbols:
            raise ValueError('Invalid comparison operand COFF relocation.')
        if value <= position < end:
            if position + 4 > end:
                raise ValueError('Comparison operand relocation crosses a function boundary.')
            result.append((position - value, kind, symbols[symbol][0],
                           struct.unpack_from('<I', data, raw + position)[0]))
    return data[raw + value:raw + end], result


ONE_PAST_REGISTERS = ('eax', 'ecx', 'edx', 'ebx', 'esp', 'ebp', 'esi', 'edi')


def comparison_array_end_instruction(raw, value):
    """Only CMP r32, imm32: never a dereference, branch or arbitrary literal."""
    if len(raw) == 5 and raw[0] == 0x3d:
        immediate, register = 1, 'eax'
    elif len(raw) == 6 and raw[0] == 0x81 and 0xf8 <= raw[1] <= 0xff:
        immediate, register = 2, ONE_PAST_REGISTERS[raw[1] - 0xf8]
    else:
        raise ValueError('One-past global operand must be CMP register, imm32.')
    if struct.unpack_from('<I', raw, immediate)[0] != value:
        raise ValueError('One-past global operand has the wrong address or addend.')
    return immediate, register


def comparison_array_end_sites(instructions, function, code, relocations, symbol, addend, value):
    """Rebuilt one-past comparisons, identified by their COFF relocation.

    instructions are the disassembled (address, bytes) of the linked function.
    A site needs CMP r32, imm32 equal to the linked global + addend, and its
    immediate must be the object's DIR32 relocation to that exact global with
    that exact stored addend. Returns (function offset, register) pairs.
    """
    sites = []
    for address, raw in instructions:
        offset = address - function
        try:
            immediate, register = comparison_array_end_instruction(raw, value)
        except ValueError:
            continue
        if (code[offset:offset + immediate] == raw[:immediate] and
                (offset + immediate, 6, symbol, addend) in relocations):
            sites.append((offset, register))
    return sites


def folded_identity_specs(metadata_rows, rebuilt, publics):
    """CSV code identities whose own symbol LINK placed at a folded address.

    After identical-code folding the PDB keeps one public per rebuilt address,
    so only one of the folded symbols can bind its original address by symbol.
    Every code identity at such an address is listed with the rebuilt address
    and every name that the LINK map gives it; a fold is proven only when the
    address carries another map name as well.
    """
    result = []
    for row in metadata_rows:
        symbol = row.get('symbol')
        if row.get('type') not in FOLDED_ALIAS_TYPES or not symbol:
            continue
        address = rebuilt.get(symbol)
        if address is not None and publics.get(address, set()) - {symbol}:
            result.append({'address': f"0x{int(row['address'], 16):08x}", 'symbol': symbol,
                           'recomp': f'0x{address:08x}', 'names': sorted(publics[address])})
    return result


def bind_folded_identities(specs, pdb_publics, original_match, rebuilt_match):
    """Choose the folded identities to bind at their LINK map address.

    pdb_publics maps rebuilt addresses to the PDB public names there;
    original_match and rebuilt_match return the address an original or rebuilt
    address is already matched to, or None. An identity is bound only when its
    original address is still unmatched after reccmp's own matching, the LINK
    map names its symbol and another name at that rebuilt address, the PDB
    public that reccmp kept there is one of those map names, and no other
    original address holds the address. Returns one record per identity and
    the pairs to match.
    """
    records, pairs, used = [], [], set()
    for item in specs:
        original, recomp = int(item['address'], 16), int(item['recomp'], 16)
        names, kept = set(item['names']), pdb_publics.get(recomp, set())
        matched = original_match(original)
        if matched is not None:
            bound = 'reccmp' if matched == recomp else 'other'
        elif item['symbol'] not in names or not names - {item['symbol']} or not kept or not kept <= names:
            bound = False
        elif rebuilt_match(recomp) is not None or recomp in used:
            bound = False
        else:
            used.add(recomp)
            pairs.append((original, recomp))
            bound = 'map'
        records.append(dict(item, bound=bound))
    return records, pairs


def compare_global_operands(configuration, arguments):
    """Run pinned reccmp with instruction-scoped operand names and folded code
    identities bound through their own PDB publics; images stay unchanged."""
    import reccmp
    import reccmp.compare.core as reccmp_core
    from reccmp.compare.functions import FunctionComparator
    from reccmp.compare.asm.instgen import InstructGen, SectionType
    from reccmp.types import EntityType, ImageId
    from reccmp.tools.asmcmp import main as compare_main
    if reccmp.VERSION != '0.1.7':
        raise ValueError('Global operand comparison requires reccmp 0.1.7.')
    config = json.loads(Path(configuration).read_text())
    original_compare = FunctionComparator.compare_function
    original_load_cvdump, original_match_lines = reccmp_core.load_cvdump, reccmp_core.match_lines
    applied = {}
    folded = {'publics': {}, 'records': None}

    def load_cvdump(analysis, db, recomp_bin):
        for public in analysis.parser.publics:
            if recomp_bin.is_valid_section(public.section):
                folded['publics'].setdefault(recomp_bin.get_abs_addr(public.section, public.offset),
                                             set()).add(public.name)
        return original_load_cvdump(analysis, db, recomp_bin)

    def match_lines(db, *args, **kwargs):
        # reccmp's own symbol, name and line matches come first.
        result = original_match_lines(db, *args, **kwargs)
        def matched(image, address):
            entity = db.get(image, address)
            return None if entity is None else (
                entity.recomp_addr if image == ImageId.ORIG else entity.orig_addr)
        folded['records'], pairs = bind_folded_identities(
            config.get('folded_identities', []), folded['publics'],
            lambda address: matched(ImageId.ORIG, address), lambda address: matched(ImageId.RECOMP, address))
        if pairs:
            with db.batch() as batch:
                for original, recomp in pairs:
                    batch.match(original, recomp)
        return result

    def instructions(image, start, size):
        return [(inst[0], image.read(inst[0], inst[1]))
                for section in InstructGen(image.read(start, size), start, True).sections
                if section.type == SectionType.CODE for inst in section.contents]

    def compare(self, match):
        rules = sorted((item for item in config['operands'] if int(item['function'], 16) == match.orig_addr),
                       key=lambda item: item['instruction_offset'])
        if not rules:
            return original_compare(self, match)
        replacements = ({}, {})
        original_code = dict(instructions(self.orig_bin, match.orig_addr, rules[0]['function_size']))
        rebuilt_code = instructions(self.recomp_bin, match.recomp_addr, match.size(ImageId.RECOMP))
        for global_symbol, addend in sorted({(item['global_symbol'], item['addend']) for item in rules}):
            group = [item for item in rules if (item['global_symbol'], item['addend']) == (global_symbol, addend)]
            item = group[0]
            entity = self.db.get_one_match(int(item['global'], 16))
            # The addend derives from the audited CSV extent of the matched
            # global, so only its one-past-the-end element can be expressed.
            if (match.get('symbol') != item['function_symbol'] or
                    match.size(ImageId.ORIG) != item['function_size'] or
                    entity is None or entity.entity_type != EntityType.DATA or
                    entity.get('symbol') != global_symbol or
                    entity.size(ImageId.ORIG) != item['size']):
                raise ValueError('Global operand does not resolve to its explicit matched identity.')
            path = Path(item['object'])
            if digest(path) != item['object_sha256']:
                raise ValueError('Global operand object changed during comparison.')
            code, relocations = comparison_operand_coff(path, item['function_symbol'])
            name = global_symbol + ''.join(f'+{value}' for value in (item['size'], item.get('member_offset'))
                                           if value) + ' (ONE_PAST)'
            # The audited original instruction must be exactly this comparison.
            original = []
            for rule in group:
                address = match.orig_addr + rule['instruction_offset']
                if address not in original_code:
                    raise ValueError('Global operand offset is not an original instruction boundary.')
                _, register = comparison_array_end_instruction(original_code[address],
                                                              entity.orig_addr + addend)
                replacements[0][address] = ('cmp', f'{register}, {name}')
                original.append(register)
            # Rebuilt sites come from relocations, so a changed body is still
            # compared as a mismatch instead of borrowing the original offset.
            sites = comparison_array_end_sites(rebuilt_code, match.recomp_addr, code, relocations,
                                               global_symbol, addend, entity.recomp_addr + addend)
            paired = len(sites) == len(group)
            for index, rule in enumerate(group):
                offset, register = sites[index] if paired else (None, None)
                if paired:
                    replacements[1][match.recomp_addr + offset] = ('cmp', f'{register}, {name}')
                applied[(match.orig_addr, rule['instruction_offset'])] = {
                    'function': rule['function'], 'function_symbol': rule['function_symbol'],
                    'instruction_offset': rule['instruction_offset'], 'register': original[index],
                    'recomp_function': hex(match.recomp_addr), 'recomp_instruction_offset': offset,
                    'recomp_register': register, 'global': rule['global'], 'global_symbol': global_symbol,
                    'size': rule['size'], 'addend': addend, 'recomp_global': hex(entity.recomp_addr),
                    'source': rule['source'], 'object': rule['object'],
                    'object_sha256': rule['object_sha256']}
        sanitizers = (self.orig_sanitize, self.recomp_sanitize)
        previous = [sanitizer.sanitize for sanitizer in sanitizers]
        try:
            for sanitizer, replace, default in zip(sanitizers, replacements, previous):
                sanitizer.sanitize = lambda inst, replace=replace, default=default: (
                    replace[inst[0]] if inst[0] in replace else default(inst))
            return original_compare(self, match)
        finally:
            for sanitizer, default in zip(sanitizers, previous):
                sanitizer.sanitize = default

    FunctionComparator.compare_function = compare
    reccmp_core.load_cvdump, reccmp_core.match_lines = load_cvdump, match_lines
    previous_arguments = sys.argv
    try:
        sys.argv = ['reccmp-reccmp', *arguments]
        result = compare_main()
        if len(applied) != len(config['operands']):
            raise ValueError('Comparator did not validate every configured global operand.')
        if config.get('folded_identities') and folded['records'] is None:
            raise ValueError('Comparator did not reach the folded identity binding.')
        save(config['validation'], [applied[key] for key in sorted(applied)])
        if 'folded_validation' in config:
            save(config['folded_validation'], folded['records'] or [])
        return result
    finally:
        FunctionComparator.compare_function = original_compare
        reccmp_core.load_cvdump, reccmp_core.match_lines = original_load_cvdump, original_match_lines
        sys.argv = previous_arguments


def global_operand_command(tool, metadata, target, source, sources, folder, command, folded=()):
    """Without configured operands or folded identities, return the ordinary
    reccmp command unchanged. Otherwise also return the validation paths."""
    specs = comparison_global_operand_specs(metadata, target)
    if not specs and not folded:
        return command, None, None
    linked = (folder / 'link.rsp').read_text().splitlines() if specs else []
    for item in specs:
        path = source / item['source']
        obj = folder / (path.stem + '.obj')
        if path not in sources or linked.count('"' + obj.name + '"') != 1:
            raise ValueError('Global operand must name a compiled and linked game source.')
        item.update(object=str(obj), object_sha256=digest(obj))
    config, validation = folder / 'global-operands.json', folder / 'global-operands-validated.json'
    folded_validation = folder / 'folded-identities-validated.json'
    validation.unlink(missing_ok=True)
    folded_validation.unlink(missing_ok=True)
    save(config, {'operands': specs, 'validation': str(validation),
                  **({'folded_identities': list(folded), 'folded_validation': str(folded_validation)}
                     if folded else {})})
    interpreter = Path(tool.reccmp).parent / ('python.exe' if os.name == 'nt' else 'python')
    if not interpreter.is_file():
        raise ValueError('Global operands require the Python interpreter beside reccmp-reccmp.')
    return ([str(interpreter), str(Path(__file__).resolve()), 'compare-global-operands',
             str(config), *command[1:]], validation if specs else None,
            folded_validation if folded else None)


FUNCTION_ANNOTATION = re.compile(r'^[ \t]*//[ \t]*(?:FUNCTION|LIBRARY|SYNTHETIC|TEMPLATE|STUB):[ \t]*'
                                 r'BEACHLIFE[ \t]+0x([0-9a-fA-F]{1,8})\b', re.M)


def annotated_function_addresses(paths):
    """Original function addresses that prepared source bodies already annotate."""
    found = {}
    for path in paths:
        for match in FUNCTION_ANNOTATION.finditer(path.read_text(errors='replace')):
            found.setdefault(int(match[1], 16), path)
    return found


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
    # A callee whose real body is already in the export (matched or not) keeps
    # that body: a CSV identity names it. An anchor would add a second
    # annotation for the address and a duplicate definition.
    annotated = annotated_function_addresses(
        [*sorted(source.rglob('*.cpp')), *sorted((metadata / 'support').glob('*.cpp'))])
    for item in specs:
        if int(item['address'], 16) in annotated:
            raise ValueError(f'Comparison anchor {item["address"]} duplicates the prepared body in '
                             f'{annotated[int(item["address"], 16)].name}; use its CSV identity instead.')
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


def lifetime_identity_kind(name, symbol):
    """Constructor or destructor identity of one class, from its exact decoration.

    VC6 decorates a constructor ??0Class@@<access>AE@<parameters>Z and a
    destructor ??1Class@@<access>AE@XZ; only the class name is taken from it.
    A scalar deleting destructor needs no identity: VC6 generates it as a
    COMDAT wherever the class vtable is emitted.
    """
    parts = name.split('::') if isinstance(name, str) else []
    if (len(parts) != 2 or not re.fullmatch(ANCHOR_IDENTIFIER, parts[0]) or
            parts[0] in ANCHOR_KEYWORDS or not isinstance(symbol, str)):
        return None
    owner = re.escape(parts[0])
    if parts[1] == parts[0] and re.fullmatch(r'\?\?0' + owner + r'@@[AIQ]AE@(?:XZ|[^,\s@?][^,\s]*@Z)',
                                             symbol):
        return 'constructor'
    if parts[1] == '~' + parts[0] and re.fullmatch(r'\?\?1' + owner + r'@@[AIQEMU]AE@XZ', symbol):
        return 'destructor'
    return None


def comparison_lifetime_specs(metadata, target):
    """Validate constructor/destructor identities kept apart from credited code."""
    specs = target.get('comparison_lifetime_anchors', [])
    if not isinstance(specs, list):
        raise ValueError('comparison_lifetime_anchors must be a list.')
    if not specs:
        return []
    credited = [item for key in ('functions', 'library_functions', 'unclassified_functions')
                for item in target.get(key, [])]
    anchors = target.get('comparison_anchors', [])
    ranges = [(int(item['address'], 16), int(item['address'], 16) + item['size'])
              for item in (*credited, *anchors)]
    names = {item['name'] for item in (*credited, *anchors)}
    rows = game_metadata_rows(metadata)
    taken = {int(item['address'], 16) for item in (*credited, *anchors)}
    symbols = {row['symbol'] for row in rows if int(row['address'], 16) in taken}
    for item in specs:
        if (not isinstance(item, dict) or set(item) != {'address', 'size', 'name', 'symbol', 'header'} or
                not isinstance(item['address'], str) or
                not re.fullmatch(r'0x[0-9a-fA-F]{1,8}', item['address']) or
                type(item['size']) is not int or item['size'] <= 0 or
                lifetime_identity_kind(item['name'], item['symbol']) is None or
                not isinstance(item['header'], str) or
                not re.fullmatch(r'[A-Za-z_][A-Za-z_0-9]*\.h', item['header'])):
            raise ValueError('Unsupported comparison-only lifetime identity.')
        start, end = int(item['address'], 16), int(item['address'], 16) + item['size']
        if (end > 0x100000000 or item['name'] in names or item['symbol'] in symbols or
                any(start < right and left < end for left, right in ranges)):
            raise ValueError('Comparison-only lifetime identities must be distinct from credited '
                             'functions, anchors and each other.')
        rows_at_address = [row for row in rows if int(row['address'], 16) == start]
        if (len(rows_at_address) != 1 or rows_at_address[0]['type'] != 'function' or
                rows_at_address[0]['size'] != str(item['size']) or
                rows_at_address[0]['symbol'] != item['symbol']):
            raise ValueError('Comparison-only lifetime identity must match one explicit CSV identity.')
        ranges.append((start, end))
        names.add(item['name'])
        symbols.add(item['symbol'])
    return specs


def lifetime_identity_object(address, symbol):
    """An x86 COFF object defining only the decorated symbol, as a trap.

    It carries no constructor or destructor semantics (no base or member
    calls, vptr stores or vtable), so it cannot stand in for the original
    body: `mov eax, <original address>` names it, then int3 traps.
    """
    code = b'\xb8' + struct.pack('<I', address) + b'\xcc' * 3
    name = symbol.encode('ascii') + b'\0'
    raw = 20 + 40
    table = raw + len(code)
    header = struct.pack('<HHIIIHH', 0x14c, 1, 0, table, 1, 0, 0)
    # .text: code, 16-byte alignment, execute and read; not a COMDAT, so
    # /OPT:ICF never folds one identity into another.
    section = struct.pack('<8sIIIIIIHHI', b'.text', 0, 0, len(code), raw, 0, 0, 0, 0, 0x60500020)
    entry = struct.pack('<IIIhHBB', 0, 4, 0, 1, 0x20, 2, 0)
    return header + section + code + entry + struct.pack('<I', 4 + len(name)) + name


def generate_lifetime_identities(metadata, source, folder, specs):
    """Write ignored comparison-only identity objects; never part of an export."""
    if not specs:
        return [], []
    destination = (folder / 'comparison-lifetime').resolve()
    if (not destination.is_relative_to(ROOT.resolve()) or
            destination.is_relative_to(metadata.resolve()) or
            destination.is_relative_to(source.resolve())):
        raise ValueError('Lifetime identities require output outside reconstructed source.')
    ignored = subprocess.run(['git', 'check-ignore', '--quiet', str(destination)], cwd=ROOT,
                             capture_output=True, text=True, check=False)
    if ignored.returncode != 0:
        raise ValueError('Lifetime identities require ignored output inside this repository.')
    paths = [destination / ('lifetime_' + item['address'][2:].lower() + '.obj') for item in specs]
    if destination.exists() and any(path not in paths for path in destination.iterdir()):
        raise ValueError('Unexpected files in comparison-lifetime output; use a fresh build directory.')
    annotated = annotated_function_addresses(
        [*sorted(source.rglob('*.cpp')), *sorted((metadata / 'support').glob('*.cpp'))])
    for item in specs:
        address = int(item['address'], 16)
        if address in annotated:
            raise ValueError(f'Lifetime identity {item["address"]} duplicates the prepared body in '
                             f'{annotated[address].name}; use its CSV identity instead.')
        # The shared declaration must exist; LINK then proves the decoration
        # against the callers' undefined references.
        header = (source / item['header']).resolve()
        if not header.is_relative_to(source.resolve()) or not header.is_file():
            raise ValueError('Lifetime identity header is not an actual source input.')
        text = header.read_text(errors='replace')
        owner = item['name'].split('::')[0]
        declaration = {'constructor': r'(?<![A-Za-z_0-9~])', 'destructor': r'~'}[
                           lifetime_identity_kind(item['name'], item['symbol'])] + owner + r'[ \t]*\('
        if (not re.search(r'\b(?:struct|class)[ \t]+' + owner + r'\b[^;]*?\{', text) or
                not re.search(declaration, text)):
            raise ValueError(f'{item["header"]} does not declare {item["name"]}.')
    destination.mkdir(parents=True, exist_ok=True)
    records = []
    for item, path in zip(specs, paths):
        body = lifetime_identity_object(int(item['address'], 16), item['symbol'])
        if path.exists() and path.read_bytes() != body:
            raise ValueError('Comparison-only lifetime object changed; use a fresh build directory.')
        if not path.exists():
            path.write_bytes(body)
        verify_comparison_anchor_object(path, item['symbol'])
        records.append({'address': item['address'], 'original_bytes': item['size'],
                        'name': item['name'], 'symbol': item['symbol'],
                        'kind': lifetime_identity_kind(item['name'], item['symbol']),
                        'object': str(path.relative_to(ROOT)), 'sha256': digest(path),
                        'code_bytes': len(body) - 20 - 40 - 18 - 4 - len(item['symbol']) - 1,
                        'qualified': False, 'image_execution_permitted': False})
    return paths, records


def coff_external_symbols(path):
    """Defined and undefined external names of an x86 COFF object."""
    data = Path(path).read_bytes()
    if len(data) < 20 or struct.unpack_from('<H', data)[0] != 0x14c:
        raise ValueError(f'Expected an x86 COFF object: {path}')
    offset, count = struct.unpack_from('<II', data, 8)
    strings = offset + count * 18
    if offset < 20 or strings + 4 > len(data):
        raise ValueError(f'Truncated COFF symbol table: {path}')
    string_size, = struct.unpack_from('<I', data, strings)
    if string_size < 4 or strings + string_size > len(data):
        raise ValueError(f'Truncated COFF string table: {path}')
    defined, undefined, index = set(), set(), 0
    while index < count:
        entry = offset + index * 18
        auxiliary = data[entry + 17]
        if index + 1 + auxiliary > count:
            raise ValueError(f'Truncated COFF auxiliary records: {path}')
        section, = struct.unpack_from('<h', data, entry + 12)
        if data[entry + 16] == 2:
            name = data[entry:entry + 8]
            if name[:4] == b'\0' * 4:
                start, = struct.unpack_from('<I', name, 4)
                end = data.find(b'\0', strings + start, strings + string_size)
                if not 4 <= start < string_size or end == -1:
                    raise ValueError(f'Invalid COFF symbol name: {path}')
                name = data[strings + start:end]
            else:
                name = name.rstrip(b'\0')
            name = name.decode('ascii', 'replace')
            if section > 0:
                defined.add(name)
            elif section == 0 and struct.unpack_from('<I', data, entry + 8)[0] == 0:
                undefined.add(name)
        index += 1 + auxiliary
    return defined, undefined


def verify_lifetime_identity_links(folder, records, map_path):
    """Each identity must resolve a real caller reference and nothing else."""
    if not records:
        return
    arguments = [line.strip().strip('"') for line in (folder / 'link.rsp').read_text().splitlines()]
    compiled = [folder / name for name in arguments if re.fullmatch(r'[^\\/:"]+\.obj', name)]
    references, definitions = set(), set()
    for path in compiled:
        defined, undefined = coff_external_symbols(path)
        references |= undefined
        definitions |= defined
    owners = {}
    for line in map_path.read_text(errors='replace').splitlines():
        parts = line.split()
        if len(parts) >= 4 and re.fullmatch(r'[0-9a-fA-F]{4}:[0-9a-fA-F]{8}', parts[0]):
            owners.setdefault(parts[1], set()).add(parts[-1])
    for record in records:
        obj = Path(record['object']).name
        if record['symbol'] not in references:
            raise ValueError(f'No compiled object calls lifetime identity {record["symbol"]}.')
        if record['symbol'] in definitions or owners.get(record['symbol']) != {obj}:
            raise ValueError(f'Lifetime identity {record["symbol"]} is not defined only by {obj}.')
        if sum(Path(argument.replace('\\', '/')).name == obj for argument in arguments) != 1:
            raise ValueError(f'Lifetime identity object {obj} is not linked exactly once.')


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
    comparison_lifetime_specs(metadata, target)
    comparison_global_operand_specs(metadata, target)
    game_runtime_cdecl_imports(target)
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
    cdecl_imports = game_runtime_cdecl_imports(target)
    original_stdcall_imports = verify_stdcall_pe_imports(original, stdcall_imports, exact=False)
    original_runtime_imports = verify_runtime_original_imports(original, runtime_imports)
    original_cdecl_imports = verify_runtime_cdecl_imports(original, cdecl_imports)
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
    lifetime_objects, lifetime_records = generate_lifetime_identities(
        metadata, source, folder, comparison_lifetime_specs(metadata, target))
    inputs = set([target_path, *sources, *anchors, *lifetime_objects, *source.rglob('*.h'),
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
    library = tool.runtime_library(folder, cdecl_imports) if cdecl_imports else tool.runtime_library(folder)
    link_flags = ['/fixed:no', library, *(tool.native(path) for path in lifetime_objects)]
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
    compiled_cdecl_imports = {}
    if cdecl_imports:
        linked_imports = pe_imports(binary).get('MSVCRT.dll', {})
        if not set(linked_imports) <= set(pe_imports(original).get('MSVCRT.dll', {})):
            raise ValueError('Rebuilt runtime PE imports contain an unverified identity.')
        compiled_cdecl_imports = {'linked': {name: linked_imports[name] for name in cdecl_imports
                                             if name in linked_imports},
                                  'omitted': [name for name in cdecl_imports if name not in linked_imports]}
    verify_lifetime_identity_links(folder, lifetime_records, binary.with_suffix('.map'))
    if any(digest(ROOT / record['object']) != record['sha256'] for record in lifetime_records):
        raise ValueError('Generated lifetime identity object changed during the build.')
    # The report ignore list works by name. reccmp names a credited overload
    # by its qualified name without parameters, so an anchor of the other
    # overload must not hide it; lifetime identities appear under their symbol.
    credited_names = {item['name'].split('(')[0] for key in ('functions', 'library_functions',
                                                              'unclassified_functions')
                      for item in target.get(key, [])}
    ignored = ['fixture_entry', *(item['name'] for item in anchor_records if item['name'] not in credited_names),
               *(item['symbol'] for item in lifetime_records)]
    # CSV metadata supplies the known float in the original, whose relocations
    # were stripped. Relocations in our comparison build identify its literal.
    save(folder / 'reccmp-project.yml', {'targets': {'BEACHLIFE': {
        'filename': 'BeachLife.exe', 'source-root': [str(source), str(support)] +
        ([str(anchors[0].parent)] if anchors else []),
        'hash': {'sha256': target['sha256']},
        'data-sources': [str(path) for path in game_metadata_paths(metadata)],
        'report': {'ignore-functions': ignored}}}})
    save(folder / 'reccmp-user.yml', {'targets': {'BEACHLIFE': {'path': str(original)}}})
    save(folder / 'reccmp-build.yml', {'project': '.', 'targets': {'BEACHLIFE': {
        'path': str(binary), 'pdb': str(pdb)}}})
    reports = args.reports.resolve()
    reports.mkdir(parents=True, exist_ok=True)
    # Identical-code folding gives several symbols one rebuilt address and the
    # comparator keeps only one of their names. A folded function or library
    # identity binds its original address through its own CSV symbol, at the
    # rebuilt address that LINK gave that symbol and shares with another name,
    # so callers keep its identity; a row that still cannot be compared counts
    # only over its own original extent, when the bytes are equal or differ only
    # in absolute or relative operands that LINK resolved and that name the
    # same identities on both sides.
    map_path = binary.with_suffix('.map')
    rebuilt, objects = {}, {}
    for line in map_path.read_text(errors='replace').splitlines():
        parts = line.split()
        if len(parts) >= 3 and parts[1].startswith('?') and re.fullmatch(r'[0-9a-fA-F]{8}', parts[2]):
            rebuilt.setdefault(parts[1], int(parts[2], 16))
            objects.setdefault(parts[1], parts[-1])
    linked = (folder / 'link.rsp').read_text().splitlines() if (folder / 'link.rsp').is_file() else []
    metadata_rows = game_metadata_rows(metadata)
    symbol_at = {int(row['address'], 16): row['symbol'] for row in metadata_rows
                 if row.get('type') in FOLDED_ALIAS_TYPES and row.get('symbol')}
    folding = {'publics': map_public_names(map_path)}
    folded_specs = folded_identity_specs(metadata_rows, rebuilt, folding['publics'])
    command, operand_validation, folded_validation = global_operand_command(
        tool, metadata, target, source, sources, folder,
        [tool.reccmp, '--target', 'BEACHLIFE', '--json', str(reports / 'comparison.json'), '--no-color'],
        folded_specs)
    output = tool.run(command, folder)
    (reports / 'comparison.log').write_text(output)
    if any(re.match(r'^\s*\[(?:ERROR|CRITICAL)\]', line) for line in output.splitlines()):
        raise ValueError('Comparator reported an error; inspect comparison.log.')
    report = json.loads((reports / 'comparison.json').read_text())
    def folded_function(item):
        start, size = int(item['address'], 16), item['size']
        symbol = symbol_at.get(start, '')
        address = rebuilt.get(symbol)
        if address is None or not folding['publics'].get(address, set()) - {symbol}:
            raise ValueError(f"Comparator did not find {item['name']}.")
        found = {'name': item['name'], 'address': item['address'], 'bytes': size,
                 'normalized_match': 1.0, 'exact_bytes': True, 'folded': True}
        original_code, rebuilt_code = read_va(original, start, size), read_va(binary, address, size)
        if original_code == rebuilt_code:
            return found
        if 'relocations' not in folding:
            folding['relocations'] = pe_base_relocations(binary)
            folding['identities'] = relocated_operand_identities(
                original, binary, metadata_rows, folding['publics'])
        # A relative operand comes only from the alias's own linked COFF function.
        candidates = folded_alias_objects(folder, linked, objects.get(symbol, ''), symbol)
        relative = next((found for found in (folded_relative_operands(folder / name, symbol, rebuilt_code)
                                             for name in candidates) if found), [])
        operands = compare_relocated_alias(original_code, rebuilt_code, start, address, folding['relocations'],
                                           relative, *folding['identities'])
        if not operands:
            raise ValueError(f"Comparator did not find {item['name']}; its folded alias differs.")
        return found | {'exact_bytes': False, 'relocated_operands': operands}
    def compare_functions(items):
        functions = []
        for item in items:
            row = next((row for row in report['data'] if int(row['address'], 16) == int(item['address'], 16)), None)
            if row is None:
                functions.append(folded_function(item))
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
    if lifetime_records:
        result['comparison_lifetime_anchors'] = lifetime_records
        result['image_execution_permitted'] = False
    if cdecl_imports:
        result['runtime_cdecl_imports'] = {'original': original_cdecl_imports,
                                           'compiled': compiled_cdecl_imports}
    if operand_validation is not None:
        result['comparison_global_operands'] = json.loads(operand_validation.read_text())
        result['image_execution_permitted'] = False
    if folded_validation is not None:
        if not folded_validation.is_file():
            raise ValueError('Comparator did not validate the folded identities.')
        result['comparison_folded_identities'] = json.loads(folded_validation.read_text())
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
    if len(sys.argv) > 2 and sys.argv[1] == 'compare-global-operands':
        sys.exit(compare_global_operands(sys.argv[2], sys.argv[3:]))
    else:
        main()
