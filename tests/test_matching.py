import hashlib
import importlib.util
import contextlib
import io
import json
from pathlib import Path
import struct
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
spec = importlib.util.spec_from_file_location('matching', Path(__file__).resolve().parents[1] / 'tools/matching.py')
matching = importlib.util.module_from_spec(spec)
spec.loader.exec_module(matching)


class MatchingInputTests(unittest.TestCase):
    def test_unpacked_source_keeps_its_identity_without_disabling_other_icf(self):
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            ordinary, distinct = folder / 'ordinary.cpp', folder / 'distinct.cpp'
            for source in (ordinary, distinct):
                source.write_text('void function() {}\n')
            target = {'source_compile_options': {'distinct.cpp': ['/Gy-']}}
            options = matching.game_source_compile_options(target, [ordinary, distinct])
            tool = matching.Toolchain.__new__(matching.Toolchain)
            tool.compiler = folder
            with patch.object(tool, 'native', side_effect=str), \
                    patch.object(tool, 'run', return_value=''), \
                    patch.object(tool, 'response') as response:
                tool.build(folder / 'game', [ordinary, distinct], 'fixture', source_flags=options)
            ordinary_flags, distinct_flags, link_flags = [call.args[1] for call in response.call_args_list]
            self.assertIn('/O2', ordinary_flags)
            self.assertIn('/Gy', ordinary_flags)
            self.assertIn('/Ox', distinct_flags)
            for flag in ('/O2', '/Gy', '/Gy-'):
                self.assertNotIn(flag, distinct_flags)
            self.assertIn('/opt:icf', link_flags)
            self.assertEqual(options, {distinct: ['/Gy-']})

    def test_identity_rejects_missing_changed_and_truncated_inputs(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'generated.exe'
            data = b'generated input, not a retail executable'
            target = {'size': len(data), 'sha1': hashlib.sha1(data).hexdigest(),
                      'sha256': hashlib.sha256(data).hexdigest()}
            with self.assertRaises(FileNotFoundError):
                matching.verify_original(path, target)
            path.write_bytes(data)
            self.assertEqual(matching.verify_original(path, target), path.resolve())
            self.assertEqual(path.read_bytes(), data)
            for changed in (data[:-1], b'X' + data[1:]):
                path.write_bytes(changed)
                with self.assertRaises(ValueError):
                    matching.verify_original(path, target)

    def test_section_bounds_and_machine(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'generated.exe'
            data = bytearray(512)
            data[:2] = b'MZ'
            struct.pack_into('<I', data, 0x3c, 64)
            data[64:68] = b'PE\0\0'
            struct.pack_into('<HH', data, 68, 0x14c, 1)
            data[88:96] = b'.text\0\0\0'
            struct.pack_into('<II', data, 104, 4, 256)
            data[256:260] = b'code'
            path.write_bytes(data)
            self.assertEqual(matching.section(path, b'.text'), b'code')
            with self.assertRaises(ValueError):
                matching.section(path, b'.missing')
            for mutated in (data[:32], data[:120], data[:258]):
                path.write_bytes(mutated)
                with self.assertRaises(ValueError):
                    matching.section(path, b'.text')
            struct.pack_into('<H', data, 68, 0x8664)
            path.write_bytes(data)
            with self.assertRaises(ValueError):
                matching.section(path, b'.text')

    def test_imports_resolve_names_ordinals_and_reject_invalid_ranges(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'generated.exe'
            data = bytearray(1024)
            data[:2] = b'MZ'
            struct.pack_into('<I', data, 0x3c, 64)
            data[64:68] = b'PE\0\0'
            struct.pack_into('<HH', data, 68, 0x14c, 1)
            struct.pack_into('<H', data, 84, 224)
            struct.pack_into('<H', data, 88, 0x10b)
            struct.pack_into('<I', data, 116, 0x400000)
            struct.pack_into('<I', data, 192, 0x1000)
            data[312:320] = b'.text\0\0\0'
            struct.pack_into('<III', data, 324, 0x1000, 512, 512)
            struct.pack_into('<5I', data, 512, 0x1040, 0, 0, 0x10a0, 0x1060)
            struct.pack_into('<3I', data, 576, 0x1080, 0x80000005, 0)
            data[642:648] = b'alpha\0'
            data[672:682] = b'dummy.dll\0'
            path.write_bytes(data)
            self.assertEqual(matching.pe_imports(path), {
                'dummy.dll': {'alpha': '0x401060', 'ordinal:5': '0x401064'}})
            for offset, value in [(512, 0xffffffff), (576, 0x11ff), (524, 0x1200)]:
                malformed = bytearray(data)
                struct.pack_into('<I', malformed, offset, value)
                path.write_bytes(malformed)
                with self.assertRaises(ValueError):
                    matching.pe_imports(path)
            struct.pack_into('<I', data, 192, 0)
            path.write_bytes(data)
            self.assertEqual(matching.pe_imports(path), {})

    def test_virtual_address_reads_respect_raw_bounds(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'generated.exe'
            data = bytearray(512)
            data[:2] = b'MZ'
            struct.pack_into('<I', data, 0x3c, 64)
            data[64:68] = b'PE\0\0'
            struct.pack_into('<HH', data, 68, 0x14c, 1)
            struct.pack_into('<H', data, 84, 224)
            struct.pack_into('<H', data, 88, 0x10b)
            struct.pack_into('<I', data, 116, 0x400000)
            data[312:320] = b'.text\0\0\0'
            struct.pack_into('<III', data, 324, 0x1000, 4, 448)
            data[448:452] = b'code'
            path.write_bytes(data)
            self.assertEqual(matching.read_va(path, 0x401001, 2), b'od')
            for address, size in [(0x400fff, 1), (0x401003, 2), (-1, 1), (0x401000, -1)]:
                with self.assertRaises(ValueError):
                    matching.read_va(path, address, size)
            path.write_bytes(data[:450])
            with self.assertRaises(ValueError):
                matching.read_va(path, 0x401000, 4)

    def test_base_relocations_list_highlow_operands_and_reject_malformed_tables(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'generated.exe'
            data = bytearray(1024)
            data[:2] = b'MZ'
            struct.pack_into('<I', data, 0x3c, 64)
            data[64:68] = b'PE\0\0'
            struct.pack_into('<HH', data, 68, 0x14c, 1)
            struct.pack_into('<H', data, 84, 224)
            struct.pack_into('<H', data, 88, 0x10b)
            struct.pack_into('<I', data, 116, 0x400000)
            struct.pack_into('<I', data, 180, 16)
            struct.pack_into('<II', data, 224, 0x1100, 28)
            data[312:320] = b'.text\0\0\0'
            struct.pack_into('<III', data, 324, 0x1000, 512, 512)
            struct.pack_into('<II4H', data, 768, 0x1000, 16, 0x3010, 0x3004, 0, 0x3ffc)
            struct.pack_into('<II2H', data, 784, 0x2000, 12, 0x3008, 0)
            path.write_bytes(data)
            self.assertEqual(matching.pe_base_relocations(path), [0x401004, 0x401010, 0x401ffc, 0x402008])
            for offset, layout, value in [(774, '<H', 0xa010), (772, '<I', 6), (772, '<I', 15),
                                          (772, '<I', 36), (228, '<I', 30), (228, '<I', 0x1000)]:
                malformed = bytearray(data)
                struct.pack_into(layout, malformed, offset, value)
                path.write_bytes(malformed)
                with self.subTest(offset=offset, value=value), self.assertRaises(ValueError):
                    matching.pe_base_relocations(path)
            for offset, value in [(224, 0), (228, 0), (180, 5)]:
                stripped = bytearray(data)
                struct.pack_into('<I', stripped, offset, value)
                path.write_bytes(stripped)
                self.assertEqual(matching.pe_base_relocations(path), [])

    def test_relocated_alias_accepts_only_operands_naming_the_same_identity(self):
        # Generated byte strings stand for a body that calls through an import slot.
        def body(slot, tail=b'\x83\xc4\x0c\xc3'):
            return b'\x6a\x01\xff\x15' + struct.pack('<I', slot) + tail
        original_names = {0x500100: 'import:msvcrt.dll!memmove', 0x500200: 'symbol:?table@@3PAHA'}
        rebuilt_names = {0x420010: {'import:msvcrt.dll!memmove'}, 0x420020: {'import:msvcrt.dll!memcpy'},
                         0x430000: {'symbol:?table@@3PAHA', 'symbol:?alias@@3PAHA'}}
        def compare(original, rebuilt, relocations, address=0x401000):
            return matching.compare_relocated_alias(original, rebuilt, 0x4a0000, address, relocations, [],
                                                    original_names.get,
                                                    lambda value: rebuilt_names.get(value, set()))
        self.assertEqual(compare(body(0x500100), body(0x420010), [0x400ff0, 0x401004, 0x402000]),
                         [{'offset': 4, 'identity': 'import:msvcrt.dll!memmove'}])
        self.assertEqual(compare(body(0x500200), body(0x430000), [0x401004]),
                         [{'offset': 4, 'identity': 'symbol:?table@@3PAHA'}])
        rejected = [
            ('different import', body(0x500100), body(0x420020), [0x401004]),
            ('unidentified original operand', body(0x500300), body(0x420010), [0x401004]),
            ('unidentified rebuilt operand', body(0x500100), body(0x420030), [0x401004]),
            ('changed instruction byte', body(0x500100), b'\x6a\x02' + body(0x420010)[2:], [0x401004]),
            ('changed tail', body(0x500100), body(0x420010, b'\x83\xc4\x08\xc3'), [0x401004]),
            ('difference outside relocations', body(0x500100), body(0x420010), []),
            ('misplaced relocation', body(0x500100), body(0x420010), [0x401003]),
            ('operand before the extent', body(0x500100), body(0x420010), [0x400ffe, 0x401004]),
            ('operand past the extent', body(0x500100), body(0x420010), [0x401004, 0x40100a]),
            ('overlapping operands', body(0x500100), body(0x420010), [0x401004, 0x401006]),
            ('different extent', body(0x500100), body(0x420010) + b'\xcc', [0x401004])]
        for reason, original, rebuilt, relocations in rejected:
            with self.subTest(reason=reason):
                self.assertIsNone(compare(original, rebuilt, relocations))

    def test_relative_operands_are_compared_by_their_target_identity(self):
        # A generated call: push 1; call rel32; ret, at different addresses on each side.
        def body(start, target):
            return b'\x6a\x01\xe8' + struct.pack('<i', target - (start + 7)) + b'\xc3'
        original_names = {0x402000: 'symbol:?ReturnPool@@YAXXZ', 0x401000: 'symbol:?Erase@@YAXXZ'}
        rebuilt_names = {0x420000: {'symbol:?ReturnPool@@YAXXZ'}, 0x410000: {'symbol:?Erase@@YAXXZ'}}
        def compare(target, relative=(3,), relocations=()):
            return matching.compare_relocated_alias(body(0x404000, 0x402000), body(0x430000, target),
                                                    0x404000, 0x430000, list(relocations), list(relative),
                                                    original_names.get,
                                                    lambda value: rebuilt_names.get(value, set()))
        self.assertEqual(compare(0x420000), [{'offset': 3, 'identity': 'symbol:?ReturnPool@@YAXXZ',
                                              'relative': True}])
        for reason, arguments in [('different callee', {'target': 0x410000}),
                                  ('unidentified callee', {'target': 0x420004}),
                                  ('operand not from COFF', {'target': 0x420000, 'relative': ()}),
                                  ('misplaced operand', {'target': 0x420000, 'relative': (2,)}),
                                  ('operand past the extent', {'target': 0x420000, 'relative': (5,)}),
                                  ('absolute and relative site', {'target': 0x420000,
                                                                  'relocations': (0x430003,)})]:
            with self.subTest(reason=reason):
                self.assertIsNone(compare(**arguments))

    @staticmethod
    def call_coff(symbol, callee, code, kind=0x14, position=3):
        names = symbol.encode() + b'\0' + callee.encode() + b'\0'
        table = 60 + len(code) + 10
        header = struct.pack('<HHIIIHH', 0x14c, 1, 0, table, 2, 0, 0)
        section = struct.pack('<8sIIIIIIHHI', b'.text', 0, 0, len(code), 60,
                              60 + len(code), 0, 1, 0, 0x60000020)
        function = struct.pack('<IIIhHBB', 0, 4, 0, 1, 0x20, 2, 0)
        external = struct.pack('<IIIhHBB', 0, 5 + len(symbol), 0, 0, 0x20, 2, 0)
        return (header + section + code + struct.pack('<IIH', position, 1, kind) +
                function + external + struct.pack('<I', len(names) + 4) + names)

    def test_relative_operands_come_from_the_identical_coff_function(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'generated.obj'
            code = b'\x6a\x01\xe8\0\0\0\0\xc3'
            rebuilt = b'\x6a\x01\xe8\x10\x20\x30\x40\xc3'
            path.write_bytes(self.call_coff('?Erase@@YAXXZ', '?ReturnPool@@YAXXZ', code))
            self.assertEqual(matching.folded_relative_operands(path, '?Erase@@YAXXZ', rebuilt), [3])
            self.assertEqual(matching.folded_relative_operands(path, '?Erase@@YAXXZ', rebuilt[:6]), [3])
            for data, body in [(self.call_coff('?Erase@@YAXXZ', '?ReturnPool@@YAXXZ', code, kind=6), rebuilt),
                               (self.call_coff('?Erase@@YAXXZ', '?ReturnPool@@YAXXZ', code), b'\x6a\x02' + rebuilt[2:]),
                               (self.call_coff('?Erase@@YAXXZ', '?ReturnPool@@YAXXZ', code), rebuilt + b'\xcc')]:
                with self.subTest(body=body):
                    path.write_bytes(data)
                    self.assertEqual(matching.folded_relative_operands(path, '?Erase@@YAXXZ', body), [])
            path.write_bytes(self.call_coff('?Other@@YAXXZ', '?ReturnPool@@YAXXZ', code))
            with self.assertRaises(ValueError):
                matching.folded_relative_operands(path, '?Erase@@YAXXZ', rebuilt)

    def test_folded_alias_object_is_the_one_defining_its_symbol(self):
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            code = b'\x6a\x01\xe8\0\0\0\0\xc3'
            (folder / 'kept.obj').write_bytes(self.call_coff('?Other@@YAXXZ', '?ReturnPool@@YAXXZ', code))
            (folder / 'own.obj').write_bytes(self.call_coff('?Erase@@YAXXZ', '?ReturnPool@@YAXXZ', code))
            linked = ['"/nologo"', '"kept.obj"', '"own.obj"']
            # The map names the kept section's object, which defines another folded symbol.
            self.assertEqual(matching.folded_alias_objects(folder, linked, 'kept.obj', '?Erase@@YAXXZ'),
                             ['own.obj'])
            self.assertEqual(matching.folded_alias_objects(folder, linked, 'own.obj', '?Erase@@YAXXZ'),
                             ['own.obj'])
            self.assertEqual(matching.folded_alias_objects(folder, linked, 'kept.obj', '?Missing@@YAXXZ'), [])
            self.assertEqual(matching.folded_alias_objects(folder, linked[:2], 'kept.obj', '?Erase@@YAXXZ'), [])

    def test_operand_identities_use_import_slots_then_code_or_global_rows(self):
        rows = [{'address': '0x500100', 'type': 'global', 'symbol': '__imp__memmove'},
                {'address': '0x500200', 'type': 'library', 'symbol': '?copy@@YAXXZ'},
                {'address': '0x500300', 'type': 'global', 'symbol': '?table@@3PAHA'},
                {'address': '0x500400', 'type': 'string', 'symbol': 'text'},
                {'address': '0x500500', 'type': 'function', 'symbol': ''}]
        imports = {'original': {'MSVCRT.dll': {'memmove': '0x500100'}},
                   'rebuilt': {'msvcrt.dll': {'memmove': '0x420010'}}}
        with patch.object(matching, 'pe_imports', side_effect=lambda path: imports[path]):
            original, rebuilt = matching.relocated_operand_identities(
                'original', 'rebuilt', rows, {0x420010: {'__imp__memmove'}, 0x430000: {'?copy@@YAXXZ'}})
        self.assertEqual([original(value) for value in (0x500100, 0x500200, 0x500300, 0x500400, 0x500500)],
                         ['import:msvcrt.dll!memmove', 'symbol:?copy@@YAXXZ', 'symbol:?table@@3PAHA', None, None])
        self.assertEqual(rebuilt(0x420010), {'import:msvcrt.dll!memmove', 'symbol:__imp__memmove'})
        self.assertEqual(rebuilt(0x430000), {'symbol:?copy@@YAXXZ'})
        self.assertEqual(rebuilt(0x430004), set())

    def test_map_public_names_keep_every_name_at_an_address(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'generated.map'
            path.write_text(' Preferred load address is 00400000\n'
                            ' 0001:00000000 00000010H .text CODE\n'
                            ' 0001:00000010 ?first@@YAXXZ 00401010 f i one.obj\n'
                            ' 0001:00000010 ?second@@YAXXZ 00401010 f i two.obj\n'
                            ' 0002:00000000 __imp__memmove 00402000 msvcrt:MSVCRT.dll\n'
                            ' entry point at        0001:00000010\n')
            self.assertEqual(matching.map_public_names(path), {
                0x401010: {'?first@@YAXXZ', '?second@@YAXXZ'}, 0x402000: {'__imp__memmove'}})


class MatchingCompilerOptionsTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        # matching.py resolves its inputs; macOS /var and Windows 8.3 temp names differ.
        self.root = Path(self.temp.name).resolve()
        self.source = self.root / 'source'
        self.source.mkdir()
        (self.source / 'helper.cpp').write_text('int fixture() { return 1; }\n')
        self.metadata = self.root / 'metadata'
        for name in ('fixtures', 'support', 'targets'):
            (self.metadata / name).mkdir(parents=True)
        (self.metadata / 'fixtures/game.cpp').write_text('void fixture_entry() {}\n')
        (self.metadata / 'metadata.csv').write_text('address,type,size,symbol\n')
        (self.metadata / 'support/operator_delete.cpp').write_text('// generated fixture\n')
        self.original = self.root / 'generated.bin'
        data = b'generated identity only; no executable code'
        self.original.write_bytes(data)
        self.target = {'id': 'generated', 'size': len(data),
                       'sha1': hashlib.sha1(data).hexdigest(),
                       'sha256': hashlib.sha256(data).hexdigest(), 'functions': []}
        self.args = SimpleNamespace(source=self.source, original=self.original,
                                    output=self.root / 'output', reports=self.root / 'reports',
                                    stlport=self.root / 'vendor')
        self.stlport_header = self.args.stlport / 'stlport/vector'
        self.stlport_header.parent.mkdir(parents=True)
        self.stlport_header.write_text('// synthetic vendor input\n')
        self.stlport_pin = self.root / 'stlport.json'
        self.stlport_pin.write_text(json.dumps({
            'header_files': 1,
            'headers_sha256': hashlib.sha256(
                f'vector\0{matching.digest(self.stlport_header)}\n'.encode()).hexdigest()}))
        stlport_patch = patch.object(matching, 'STLPORT_PIN', self.stlport_pin)
        stlport_patch.start()
        self.addCleanup(stlport_patch.stop)
        self.calls = []
        self.comparison_rows = []
        self.build_arguments = None
        self.tamper = False
        self.tamper_import = False
        self.tamper_sdk_build = False
        self.tamper_sdk_pin = False
        self.tamper_sdk_compare = False
        self.tool = SimpleNamespace(decomp_root=self.metadata, output=self.args.output,
                                    compiler=self.root / 'compiler/Bin', reccmp='generated-comparator',
                                    native=str, runtime_library=self.runtime_library,
                                    build=self.build, run=self.compare)
        self.sdk_header = self.root / 'compiler/Include/FIXTURE.H'
        self.sdk_header.parent.mkdir(parents=True)
        self.sdk_header.write_bytes(b'// generated SDK declaration fixture\n')
        self.sdk_spec = {'schema': 1, 'architecture': 'x86', 'calling_convention': 'stdcall',
                         'headers_sha256': {'FIXTURE.H': matching.digest(self.sdk_header)}}
        self.sdk_pin = self.metadata / 'win32-imports.json'
        self.sdk_pin.write_text(json.dumps(self.sdk_spec))

    def runtime_library(self, folder):
        self.calls.append('runtime_library')
        return 'generated-import.lib'

    def build(self, folder, sources, name, extra_link=(), extra_compile=(), source_flags=None,
              source_includes=None):
        self.calls.append('build')
        self.build_arguments = (extra_link, extra_compile, source_flags)
        self.source_includes = source_includes
        if self.tamper:
            path = folder / 'abi/vc6-except-list.obj'
            path.write_bytes(path.read_bytes()[:-1] + b'X')
        if self.tamper_import:
            path = folder / 'imports/fixture/fixture-import.lib'
            path.write_bytes(path.read_bytes() + b'changed')
        if self.tamper_sdk_build:
            self.sdk_header.write_bytes(b'changed during compilation')
            if self.tamper_sdk_pin:
                specification = self.sdk_spec | {
                    'headers_sha256': {'FIXTURE.H': matching.digest(self.sdk_header)}}
                self.sdk_pin.write_text(json.dumps(specification))
        (folder / 'generated.map').write_text('')
        return folder / 'generated.exe', folder / 'generated.pdb'

    def compare(self, arguments, folder):
        self.calls.append('compare')
        if self.tamper_sdk_compare:
            self.sdk_header.write_bytes(b'changed during comparison')
        report = Path(arguments[arguments.index('--json') + 1])
        report.write_text(json.dumps({'data': self.comparison_rows}))
        return ''

    @staticmethod
    def fingerprint_key(path):
        # Reports use repository-relative paths when TMPDIR is in the build tree.
        return str(path.relative_to(matching.ROOT) if path.is_relative_to(matching.ROOT) else path)

    def run_game(self):
        (self.metadata / 'targets/beachlife-uk-1.1.json').write_text(json.dumps(self.target))
        with contextlib.redirect_stdout(io.StringIO()):
            matching.game(self.tool, self.args)
        return json.loads((self.args.reports / 'result.json').read_text())

    def test_default_build_keeps_existing_options_and_no_eh_object(self):
        result = self.run_game()
        link, compile_options, source_flags = self.build_arguments
        self.assertEqual(link, ['/fixed:no', 'generated-import.lib'])
        self.assertEqual(compile_options[:2], ['/MD', '/G6'])
        self.assertEqual(source_flags, {self.metadata / 'support/operator_delete.cpp': ['/O1']})
        self.assertEqual(result['source_compile_options'], {})
        self.assertEqual(result['unclassified_functions'], [])
        self.assertEqual(result['link_abi_support'], [])
        self.assertEqual(result['sdk_headers_sha256'], self.sdk_spec['headers_sha256'])
        self.assertIn(self.fingerprint_key(self.sdk_pin), result['input_sha256'])
        self.assertFalse((self.args.output / 'game/abi').exists())

    def test_comparator_error_rejects_even_complete_matching_rows(self):
        self.register_generated_functions()
        compare = self.tool.run

        def rejected_metadata(arguments, folder):
            compare(arguments, folder)
            return "[ERROR] In csv file fixture.csv: CsvInvalidEntityTypeError: line 1, 'data'\n"

        self.tool.run = rejected_metadata
        with patch.object(matching, 'read_va', return_value=b'data'):
            with self.assertRaisesRegex(ValueError, 'Comparator reported an error'):
                self.run_game()
        self.assertIn('CsvInvalidEntityTypeError',
                      (self.args.reports / 'comparison.log').read_text())
        self.assertFalse((self.args.reports / 'result.json').exists())

    def test_missing_or_changed_sdk_header_prevents_build(self):
        original = self.sdk_header.read_bytes()
        for contents in (None, original + b'changed'):
            with self.subTest(contents=contents):
                self.sdk_header.unlink(missing_ok=True)
                if contents is not None:
                    self.sdk_header.write_bytes(contents)
                with self.assertRaises((FileNotFoundError, ValueError)):
                    self.run_game()
                self.assertEqual(self.calls, [])
                self.assertFalse(self.args.output.exists())

    def test_malformed_sdk_pins_prevent_build(self):
        digest = self.sdk_spec['headers_sha256']['FIXTURE.H']
        invalid = [None, [], {}, {'FIXTURE.H': 'wrong'}, {'FIXTURE.H': True},
                   {'../FIXTURE.H': digest}, {'sub\\FIXTURE.H': digest},
                   {'/FIXTURE.H': digest}, {'C:FIXTURE.H': digest},
                   {'FIXTURE.H': digest, 'fixture.h': digest}]
        for headers in invalid:
            with self.subTest(headers=headers):
                self.sdk_pin.write_text(json.dumps(self.sdk_spec | {'headers_sha256': headers}))
                with self.assertRaises(ValueError):
                    self.run_game()
                self.assertEqual(self.calls, [])
                self.assertFalse(self.args.output.exists())

    def test_unsupported_sdk_specification_prevents_build(self):
        for specification in ([], None, self.sdk_spec | {'architecture': 'x64'},
                              self.sdk_spec | {'schema': 2},
                              self.sdk_spec | {'calling_convention': 'cdecl'}):
            with self.subTest(specification=specification):
                self.sdk_pin.write_text(json.dumps(specification))
                with self.assertRaises(ValueError):
                    self.run_game()
                self.assertEqual(self.calls, [])

    def test_sdk_change_during_build_prevents_comparison(self):
        self.tamper_sdk_build = True
        with self.assertRaisesRegex(ValueError, 'Platform header mismatch'):
            self.run_game()
        self.assertEqual(self.calls, ['runtime_library', 'build'])
        self.assertFalse(self.args.reports.exists())

    def test_sdk_change_during_comparison_prevents_result(self):
        self.tamper_sdk_compare = True
        with self.assertRaisesRegex(ValueError, 'Platform header mismatch'):
            self.run_game()
        self.assertEqual(self.calls, ['runtime_library', 'build', 'compare'])
        self.assertFalse((self.args.reports / 'result.json').exists())

    def test_replacing_header_and_pin_during_build_prevents_comparison(self):
        self.tamper_sdk_build = True
        self.tamper_sdk_pin = True
        with self.assertRaisesRegex(ValueError, 'SDK header pins changed during the build'):
            self.run_game()
        self.assertEqual(self.calls, ['runtime_library', 'build'])
        self.assertFalse(self.args.reports.exists())

    def test_only_selected_game_source_gets_options_and_eh_link_object(self):
        self.target['source_compile_options'] = {'helper.cpp': ['/GX', '/Ob1']}
        result = self.run_game()
        link, _, source_flags = self.build_arguments
        self.assertEqual(source_flags, {self.metadata / 'support/operator_delete.cpp': ['/O1'],
                                        self.source / 'helper.cpp': ['/GX', '/Ob1']})
        abi = self.args.output / 'game/abi/vc6-except-list.obj'
        self.assertEqual(link, ['/fixed:no', 'generated-import.lib', str(abi)])
        self.assertEqual(result['source_compile_options'], self.target['source_compile_options'])
        self.assertEqual(result['link_abi_support'][0]['sha256'], hashlib.sha256(abi.read_bytes()).hexdigest())
        self.assertEqual(result['link_abi_support'][0]['absolute_symbols'], {'__except_list': 0})
        self.assertEqual(self.calls, ['runtime_library', 'build', 'compare'])

    def test_inline_option_alone_does_not_add_exception_support(self):
        self.target['source_compile_options'] = {'helper.cpp': ['/Ob1']}
        result = self.run_game()
        self.assertEqual(self.build_arguments[2][self.source / 'helper.cpp'], ['/Ob1'])
        self.assertEqual(result['link_abi_support'], [])
        self.assertFalse((self.args.output / 'game/abi').exists())

    def test_support_inline_options_are_scoped_and_fingerprinted(self):
        support = self.metadata / 'support/iterator.cpp'
        support.write_text('// generated container fixture\n')
        self.target['source_compile_options'] = {'iterator.cpp': ['/Ob1']}
        result = self.run_game()
        self.assertEqual(self.build_arguments[2], {
            self.metadata / 'support/operator_delete.cpp': ['/O1'], support: ['/Ob1']})
        self.assertEqual(result['source_compile_options'], self.target['source_compile_options'])
        self.assertEqual(result['input_sha256'][self.fingerprint_key(support)], matching.digest(support))
        self.assertEqual(result['link_abi_support'], [])
        self.assertFalse((self.args.output / 'game/abi').exists())

    def test_support_options_preserve_size_optimization_and_add_exception_abi(self):
        self.target['source_compile_options'] = {'operator_delete.cpp': ['/GX', '/Ob1']}
        result = self.run_game()
        self.assertEqual(self.build_arguments[2], {
            self.metadata / 'support/operator_delete.cpp': ['/O1', '/GX', '/Ob1']})
        abi = self.args.output / 'game/abi/vc6-except-list.obj'
        self.assertEqual(self.build_arguments[0], ['/fixed:no', 'generated-import.lib', str(abi)])
        self.assertEqual(result['source_compile_options'], self.target['source_compile_options'])
        self.assertEqual(result['link_abi_support'][0]['sha256'], matching.digest(abi))
        self.assertEqual(result['link_abi_support'][0]['absolute_symbols'], {'__except_list': 0})

    def test_game_and_support_basename_collision_prevents_build(self):
        (self.metadata / 'support/helper.cpp').write_text('// generated duplicate\n')
        self.target['source_compile_options'] = {'helper.cpp': ['/Ob1']}
        with self.assertRaisesRegex(ValueError, 'unique files'):
            self.run_game()
        self.assertEqual(self.calls, [])
        self.assertFalse(self.args.output.exists())

    def test_changed_support_source_is_rejected(self):
        support = self.metadata / 'support/iterator.cpp'
        support.write_text('// generated container fixture\n')
        self.target['source_compile_options'] = {'iterator.cpp': ['/Ob1']}

        def change_after_build(*args, **kwargs):
            result = self.build(*args, **kwargs)
            support.write_text('// changed during compilation\n')
            return result

        with patch.object(self.tool, 'build', side_effect=change_after_build):
            with self.assertRaisesRegex(ValueError, 'Matching inputs changed'):
                self.run_game()
        self.assertEqual(self.calls, ['runtime_library', 'build', 'compare'])

    def test_invalid_configuration_fails_before_any_build_tool(self):
        bad = [None, [], True, '/GX', {'helper.cpp': '/GX'}, {'helper.cpp': [True]},
               {'helper.cpp': [['/GX']]}, {'helper.cpp': ['/GX', '/GX']},
               {'helper.cpp': ['/GX-']}, {'helper.cpp': ['/O1']}, {'helper.cpp': ['/Ob2']},
               {'helper.cpp': ['/Iother']}, {'../helper.cpp': ['/GX']},
               {'sub/helper.cpp': ['/GX']}, {'sub\\helper.cpp': ['/GX']},
               {'C:helper.cpp': ['/GX']}, {'/helper.cpp': ['/GX']},
               {'helper.cpp\n': ['/GX']}, {'missing.cpp': ['/GX']},
               {'game.cpp': ['/GX']}]
        for configured in bad:
            with self.subTest(configured=configured):
                self.target['source_compile_options'] = configured
                with self.assertRaises(ValueError):
                    self.run_game()
                self.assertEqual(self.calls, [])
                self.assertFalse(self.args.output.exists())

    def test_ambiguous_basename_is_rejected(self):
        nested = self.source / 'nested'
        nested.mkdir()
        (nested / 'helper.cpp').write_text('int other_fixture() { return 2; }\n')
        self.target['source_compile_options'] = {'helper.cpp': ['/GX']}
        with self.assertRaises(ValueError):
            self.run_game()
        self.assertEqual(self.calls, [])

    def test_changed_generated_eh_object_is_rejected_before_comparison(self):
        self.target['source_compile_options'] = {'helper.cpp': ['/GX']}
        self.tamper = True
        with self.assertRaisesRegex(ValueError, 'ABI object changed'):
            self.run_game()
        self.assertEqual(self.calls, ['runtime_library', 'build'])

    def set_stdcall_policy(self, mode='decorated'):
        self.target['stdcall_imports'] = {
            'fixture.dll': {'export_names': mode, 'symbols': {'FixtureCall': 8}}}

    def generated_import_library(self, tool, folder, dll, symbols, mode):
        self.calls.append('import_library')
        self.assertIs(tool, self.tool)
        self.assertEqual((dll, symbols), ('fixture.dll', {'FixtureCall': 8}))
        self.assertEqual(mode, self.target['stdcall_imports'][dll]['export_names'])
        folder.mkdir(parents=True)
        library = folder / 'fixture-import.lib'
        library.write_bytes(b'generated import metadata fixture')
        return library, [{'symbol': '_FixtureCall@8', 'before': 8,
                          'after': 4 if mode == 'decorated' else 12}]

    def test_stdcall_policy_checks_original_and_rebuilt_export_names(self):
        for mode, name in [('decorated', '_FixtureCall@8'), ('undecorated', 'FixtureCall')]:
            with self.subTest(mode=mode):
                self.set_stdcall_policy(mode)
                self.calls.clear()
                library = self.args.output / 'game/imports/fixture/fixture-import.lib'
                if library.exists():
                    library.unlink()
                    library.parent.rmdir()
                original = {'fixture.dll': {name: '0x401000', 'OtherOriginalImport': '0x401004'}}
                rebuilt = {'fixture.dll': {name: '0x402000'}}
                with patch.object(matching, 'stdcall_import_library', self.generated_import_library), \
                        patch.object(matching, 'pe_imports', side_effect=[original, rebuilt]):
                    result = self.run_game()
                self.assertEqual(self.calls, ['runtime_library', 'import_library', 'build', 'compare'])
                self.assertIn(str(library), self.build_arguments[0])
                self.assertEqual(result['original_stdcall_imports'], {'fixture.dll': {name: '0x401000'}})
                self.assertEqual(result['compiled_stdcall_imports'], rebuilt)
                self.assertEqual(result['stdcall_import_libraries'][0]['sha256'],
                                 hashlib.sha256(library.read_bytes()).hexdigest())

    def test_invalid_stdcall_configuration_fails_before_build_tools(self):
        policy = {'export_names': 'decorated', 'symbols': {'FixtureCall': 8}}
        bad = [None, [], True, {'fixture.dll': {}}, {'fixture.dll': policy | {'extra': 1}},
               {'fixture.dll': policy | {'export_names': 'ordinal'}},
               {'fixture.dll': policy | {'symbols': {'FixtureCall': True}}},
               {'../fixture.dll': policy}, {'MSVCRT.dll': policy},
               {'fixture.dll': policy, 'Fixture.dll': policy}]
        for configured in bad:
            with self.subTest(configured=configured):
                self.target['stdcall_imports'] = configured
                with self.assertRaises(ValueError):
                    self.run_game()
                self.assertEqual(self.calls, [])
                self.assertFalse(self.args.output.exists())

    def test_missing_original_stdcall_identity_fails_before_build_tools(self):
        self.set_stdcall_policy()
        with patch.object(matching, 'pe_imports', return_value={'fixture.dll': {'FixtureCall@8': '0x401000'}}):
            with self.assertRaisesRegex(ValueError, 'PE import identity'):
                self.run_game()
        self.assertEqual(self.calls, [])
        self.assertFalse(self.args.output.exists())

    def test_wrong_rebuilt_stdcall_identity_fails_before_comparison(self):
        self.set_stdcall_policy()
        original = {'fixture.dll': {'_FixtureCall@8': '0x401000'}}
        with patch.object(matching, 'stdcall_import_library', self.generated_import_library), \
                patch.object(matching, 'pe_imports', side_effect=[original, {'fixture.dll': {'FixtureCall@8': '0x402000'}}]):
            with self.assertRaisesRegex(ValueError, 'PE import identity'):
                self.run_game()
        self.assertEqual(self.calls, ['runtime_library', 'import_library', 'build'])

    def test_changed_import_archive_fails_before_comparison(self):
        self.set_stdcall_policy()
        self.tamper_import = True
        with patch.object(matching, 'stdcall_import_library', self.generated_import_library), \
                patch.object(matching, 'pe_imports', return_value={'fixture.dll': {'_FixtureCall@8': '0x401000'}}):
            with self.assertRaisesRegex(ValueError, 'import library changed'):
                self.run_game()
        self.assertEqual(self.calls, ['runtime_library', 'import_library', 'build'])

    def runtime_stdcall_library(self, tool, folder, dll, symbols, mode):
        self.calls.append('runtime_stdcall_library')
        self.assertIs(tool, self.tool)
        self.assertEqual((dll, symbols, mode),
                         ('MSVCRT.dll', {'_CxxThrowException': 8}, 'undecorated'))
        folder.mkdir(parents=True)
        library = folder / 'msvcrt-import.lib'
        library.write_bytes(b'generated runtime import metadata fixture')
        return library, [{'symbol': '__CxxThrowException@8', 'before': 8, 'after': 12}]

    def test_runtime_import_policy_preserves_linked_and_omitted_results(self):
        self.target['runtime_stdcall_imports'] = {'_CxxThrowException': 8}
        original = {'MSVCRT.dll': {'_CxxThrowException': '0x401000', 'malloc': '0x401004'}}
        for present in [False, True]:
            with self.subTest(present=present):
                self.calls.clear()
                library = self.args.output / 'game/imports/runtime/msvcrt-import.lib'
                if library.exists():
                    library.unlink()
                    library.parent.rmdir()
                rebuilt = {'MSVCRT.dll': {'malloc': '0x402004'}}
                if present:
                    rebuilt['MSVCRT.dll']['_CxxThrowException'] = '0x402000'
                with patch.object(matching, 'stdcall_import_library', self.runtime_stdcall_library), \
                        patch.object(matching, 'pe_imports', side_effect=[original, rebuilt]):
                    result = self.run_game()
                self.assertEqual(self.calls, ['runtime_library', 'runtime_stdcall_library', 'build', 'compare'])
                self.assertIn(str(library), self.build_arguments[0])
                self.assertEqual(result['original_runtime_stdcall_imports'],
                                 {'_CxxThrowException': '0x401000'})
                self.assertEqual(result['compiled_runtime_stdcall_imports'], {
                    'linked': {'_CxxThrowException': '0x402000'} if present else {},
                    'omitted': [] if present else ['_CxxThrowException']})
                self.assertEqual(result['runtime_stdcall_import_libraries'][0]['sha256'],
                                 hashlib.sha256(library.read_bytes()).hexdigest())

    def test_invalid_runtime_import_policy_fails_before_build_tools(self):
        for configured in [None, [], True, {'_CxxThrowException': True},
                           {'_CxxThrowException': 4}, {'_CxxThrowException@8': 8},
                           {'CxxThrowException': 8}, {'OtherRuntime': 8}]:
            with self.subTest(configured=configured):
                self.target['runtime_stdcall_imports'] = configured
                with self.assertRaises(ValueError):
                    self.run_game()
                self.assertEqual(self.calls, [])
                self.assertFalse(self.args.output.exists())

    def test_runtime_import_requires_the_original_export_identity(self):
        self.target['runtime_stdcall_imports'] = {'_CxxThrowException': 8}
        with patch.object(matching, 'pe_imports', return_value={
                'MSVCRT.dll': {'CxxThrowException': '0x401000'}}):
            with self.assertRaisesRegex(ValueError, 'Original runtime PE import'):
                self.run_game()
        self.assertEqual(self.calls, [])
        self.assertFalse(self.args.output.exists())

    def test_runtime_import_rejects_unverified_rebuilt_names(self):
        self.target['runtime_stdcall_imports'] = {'_CxxThrowException': 8}
        original = {'MSVCRT.dll': {'_CxxThrowException': '0x401000'}}
        rebuilt = {'MSVCRT.dll': {'CxxThrowException@8': '0x402000'}}
        with patch.object(matching, 'stdcall_import_library', self.runtime_stdcall_library), \
                patch.object(matching, 'pe_imports', side_effect=[original, rebuilt]):
            with self.assertRaisesRegex(ValueError, 'unverified identity'):
                self.run_game()
        self.assertEqual(self.calls, ['runtime_library', 'runtime_stdcall_library', 'build'])
        self.assertFalse(self.args.reports.exists())

    def test_changed_runtime_import_archive_fails_before_comparison(self):
        self.target['runtime_stdcall_imports'] = {'_CxxThrowException': 8}
        build = self.tool.build

        def changed_archive(*args, **kwargs):
            result = build(*args, **kwargs)
            path = self.args.output / 'game/imports/runtime/msvcrt-import.lib'
            path.write_bytes(path.read_bytes() + b'changed')
            return result

        self.tool.build = changed_archive
        with patch.object(matching, 'stdcall_import_library', self.runtime_stdcall_library), \
                patch.object(matching, 'pe_imports', return_value={
                    'MSVCRT.dll': {'_CxxThrowException': '0x401000'}}):
            with self.assertRaisesRegex(ValueError, 'import library changed'):
                self.run_game()
        self.assertEqual(self.calls, ['runtime_library', 'runtime_stdcall_library', 'build'])
        self.assertFalse(self.args.reports.exists())

    def register_generated_functions(self):
        for key, name, address in [('functions', 'game_fixture', '0x1000'),
                                   ('library_functions', 'library_fixture', '0x1020'),
                                   ('unclassified_functions', 'unclassified_fixture', '0x1040')]:
            self.target[key] = [{'name': name, 'address': address, 'size': 4}]
            self.comparison_rows.append({'address': address, 'recomp': '0x2000', 'matching': 1})

    def test_unclassified_group_uses_the_existing_comparison_and_raw_checks(self):
        self.register_generated_functions()
        # Generated providers exercise orchestration only; no compiler or executable runs.
        with patch.object(matching, 'read_va', return_value=b'data') as reader:
            result = self.run_game()
        for key in ['functions', 'library_functions', 'unclassified_functions']:
            self.assertEqual(len(result[key]), 1)
            self.assertEqual(result[key][0]['normalized_match'], 1)
            self.assertTrue(result[key][0]['exact_bytes'])
            self.assertEqual(result[key][0]['bytes'], 4)
        self.assertEqual(reader.call_count, 6)
        self.assertEqual(self.calls, ['runtime_library', 'build', 'compare'])
        self.assertEqual(result['source_compile_options'], {})
        self.assertEqual(self.build_arguments[1][:2], ['/MD', '/G6'])
        self.assertIn(self.fingerprint_key(self.source / 'helper.cpp'), result['input_sha256'])
        self.assertIn(self.fingerprint_key(self.metadata / 'targets/beachlife-uk-1.1.json'), result['input_sha256'])

    def test_partial_unclassified_match_fails_after_saving_separate_results(self):
        self.register_generated_functions()
        self.comparison_rows[-1]['matching'] = 0.99
        with patch.object(matching, 'read_va', side_effect=[b'a', b'a', b'b', b'b', b'c', b'd']):
            with self.assertRaisesRegex(ValueError, 'unclassified functions do not match'):
                self.run_game()
        result = json.loads((self.args.reports / 'result.json').read_text())
        self.assertEqual(result['functions'][0]['normalized_match'], 1)
        self.assertEqual(result['library_functions'][0]['normalized_match'], 1)
        self.assertEqual(result['unclassified_functions'][0]['normalized_match'], 0.99)
        self.assertFalse(result['unclassified_functions'][0]['exact_bytes'])

    def test_strict_unclassified_match_does_not_require_raw_byte_equality(self):
        self.register_generated_functions()
        with patch.object(matching, 'read_va', side_effect=[b'a', b'a', b'b', b'b', b'c', b'd']):
            result = self.run_game()
        self.assertEqual(result['unclassified_functions'][0]['normalized_match'], 1)
        self.assertFalse(result['unclassified_functions'][0]['exact_bytes'])

    def test_missing_unclassified_comparison_is_an_error(self):
        self.register_generated_functions()
        self.comparison_rows.pop()
        with patch.object(matching, 'read_va', return_value=b'data'):
            with self.assertRaisesRegex(ValueError, 'did not find unclassified_fixture'):
                self.run_game()
        self.assertFalse((self.args.reports / 'result.json').exists())

    def test_malformed_function_groups_fail_before_build_tools(self):
        valid = {'name': 'generated', 'address': '0x1000', 'size': 4}
        malformed = [None, {}, True, 'function', [None], [[]], [{}],
                     [valid | {'name': ''}], [valid | {'name': '  '}],
                     [valid | {'name': 1}], [valid | {'address': 4096}],
                     [valid | {'address': '1000'}], [valid | {'address': '0x100000000'}],
                     [valid | {'address': '0x1000\n'}], [valid | {'size': True}],
                     [valid | {'size': 0}], [valid | {'size': -1}],
                     [valid | {'size': 4.0}], [valid | {'size': '4'}],
                     [valid | {'address': '0xffffffff', 'size': 2}]]
        for key in ['functions', 'library_functions', 'unclassified_functions']:
            for configured in malformed:
                with self.subTest(key=key, configured=configured):
                    self.target[key] = configured
                    with self.assertRaises(ValueError):
                        self.run_game()
                    self.assertEqual(self.calls, [])
                    self.assertFalse(self.args.output.exists())
            self.target[key] = []

    def test_duplicate_addresses_across_all_groups_fail_before_build_tools(self):
        keys = ['functions', 'library_functions', 'unclassified_functions']
        row = {'name': 'generated', 'address': '0x1000', 'size': 4}
        for left in keys:
            for right in keys:
                with self.subTest(left=left, right=right):
                    self.target.update({key: [] for key in keys})
                    self.target[left].append(row)
                    self.target[right].append(row | {'address': '0x00001000'})
                    with self.assertRaisesRegex(ValueError, 'Duplicate function address'):
                        self.run_game()
                    self.assertEqual(self.calls, [])
                    self.assertFalse(self.args.output.exists())

    def test_cli_validates_function_groups_before_toolchain_initialization(self):
        self.target['unclassified_functions'] = [None]
        (self.metadata / 'targets/beachlife-uk-1.1.json').write_text(json.dumps(self.target))
        def prepared(args):
            args.decomp_root = self.metadata
        with patch.object(sys, 'argv', ['matching.py', 'game']), \
                patch.object(matching, 'prepare_game_source', side_effect=prepared), \
                patch.object(matching, 'Toolchain') as toolchain, \
                contextlib.redirect_stderr(io.StringIO()):
            with self.assertRaises(SystemExit) as raised:
                matching.main()
        self.assertEqual(raised.exception.code, 1)
        toolchain.assert_not_called()

    def add_registration(self, address, **target_fields):
        address = f'{address:08x}'
        paths = [self.metadata / f'metadata.d/{address}.csv',
                 self.metadata / f'targets/beachlife-uk-1.1.d/{address}.json',
                 self.metadata / f'fixtures/game.d/fixture_{address}.cpp']
        for path in paths:
            path.parent.mkdir(parents=True, exist_ok=True)
        paths[0].write_text(f'address,type,size,symbol\n0x{address},function,4,?method_{address}@@YAXXZ\n')
        paths[1].write_text(json.dumps({'functions': [
            {'address': '0x' + address, 'size': 4, 'name': 'method_' + address}], **target_fields}))
        paths[2].write_text('// generated retention fixture\n')
        return paths

    def test_registrations_reach_compiler_comparator_and_folded_symbol_lookup_in_order(self):
        later = self.add_registration(0x1040)
        earlier = self.add_registration(0x1020, source_compile_options={'helper.cpp': ['/GX']})
        self.target['functions'] = [{'address': '0x1000', 'size': 4, 'name': 'base'}]
        self.comparison_rows = [{'address': '0x1000', 'recomp': '0x2000', 'matching': 1},
                                {'address': '0x1020', 'recomp': '0x2020', 'matching': 1}]
        build = self.tool.build
        compiled = []
        def with_folded_symbol(folder, sources, *args, **kwargs):
            compiled.extend(sources)
            binary, pdb = build(folder, sources, *args, **kwargs)
            binary.with_suffix('.map').write_text(' 0001:00000000 ?method_00001040@@YAXXZ 00002020 f\n'
                                                  ' 0001:00000000 ?method_00001020@@YAXXZ 00002020 f\n')
            return binary, pdb
        self.tool.build = with_folded_symbol
        with patch.object(matching, 'read_va', return_value=b'data'):
            result = self.run_game()
        self.assertEqual([row['address'] for row in result['functions']],
                         ['0x1000', '0x00001020', '0x00001040'])
        self.assertTrue(result['functions'][-1]['folded'])
        self.assertEqual(compiled[-3:], [self.metadata / 'fixtures/game.cpp', earlier[2], later[2]])
        self.assertEqual(result['source_compile_options'], {'helper.cpp': ['/GX']})
        project = json.loads((self.args.output / 'game/reccmp-project.yml').read_text())
        self.assertEqual(project['targets']['BEACHLIFE']['data-sources'],
                         [str(self.metadata / 'metadata.csv'), str(earlier[0]), str(later[0])])
        for path in [*earlier, *later]:
            self.assertEqual(result['input_sha256'][self.fingerprint_key(path)], matching.digest(path))
        effective_target = Path(result['target_config'])
        self.assertEqual(result['input_sha256'][str(effective_target)], matching.digest(effective_target))
        self.assertEqual([row['address'] for row in json.loads(effective_target.read_text())['functions']],
                         ['0x1000', '0x00001020', '0x00001040'])

    def fold_library_row(self, names, csv_type='library'):
        (self.metadata / 'metadata.csv').write_text(f'address,type,size,symbol\n0x1020,{csv_type},8,?copy@@YAXXZ\n')
        self.target['library_functions'] = [{'address': '0x1020', 'size': 8, 'name': 'copy'}]
        build = self.tool.build
        def with_folded_symbols(folder, sources, *args, **kwargs):
            binary, pdb = build(folder, sources, *args, **kwargs)
            binary.with_suffix('.map').write_text(''.join(f' 0001:00000020 {name} 00002020 f\n' for name in names))
            return binary, pdb
        self.tool.build = with_folded_symbols

    def run_folded_library_row(self, original_slot, rebuilt_slot, rebuilt_name='memmove'):
        # Generated bytes stand for a body that calls through an import slot.
        def body(slot):
            return b'\xff\x15' + struct.pack('<I', slot) + b'\x5d\xc3'
        imports = {self.original: {'MSVCRT.dll': {'memmove': hex(original_slot)}}}
        with patch.object(matching, 'read_va', side_effect=lambda path, address, size: body(
                    original_slot if path == self.original else rebuilt_slot)), \
                patch.object(matching, 'pe_base_relocations', return_value=[0x2022]) as relocations, \
                patch.object(matching, 'pe_imports', side_effect=lambda path: imports.get(
                    path, {'MSVCRT.dll': {rebuilt_name: hex(rebuilt_slot)}})):
            result = self.run_game()
        relocations.assert_called_once()
        return result

    def test_folded_library_row_counts_through_its_own_symbol_when_link_shares_the_address(self):
        self.fold_library_row(['?copy@@YAXXZ', '?copy_aux@@YAXXZ'])
        with patch.object(matching, 'read_va', return_value=b'samebody'), \
                patch.object(matching, 'pe_base_relocations', side_effect=AssertionError):
            result = self.run_game()
        self.assertEqual(result['library_functions'], [
            {'name': 'copy', 'address': '0x1020', 'bytes': 8, 'normalized_match': 1.0,
             'exact_bytes': True, 'folded': True}])

    def test_unproven_fold_or_non_code_identity_is_not_credited(self):
        for names, csv_type in [(['?copy@@YAXXZ'], 'library'), (['?copy_aux@@YAXXZ'], 'library'),
                                (['?copy@@YAXXZ', '?copy_aux@@YAXXZ'], 'global')]:
            with self.subTest(names=names, csv_type=csv_type):
                self.tool.build = self.build
                self.fold_library_row(names, csv_type)
                with patch.object(matching, 'read_va', return_value=b'samebody'), \
                        self.assertRaisesRegex(ValueError, 'did not find copy'):
                    self.run_game()
                self.assertFalse((self.args.reports / 'result.json').exists())

    def test_folded_alias_differing_only_in_a_same_import_operand_is_normalized(self):
        self.fold_library_row(['?copy@@YAXXZ', '?copy_aux@@YAXXZ'])
        result = self.run_folded_library_row(0x500100, 0x420010)
        self.assertEqual(result['library_functions'], [
            {'name': 'copy', 'address': '0x1020', 'bytes': 8, 'normalized_match': 1.0,
             'exact_bytes': False, 'folded': True,
             'relocated_operands': [{'offset': 2, 'identity': 'import:msvcrt.dll!memmove'}]}])

    def test_folded_alias_with_a_differently_named_operand_still_fails(self):
        self.fold_library_row(['?copy@@YAXXZ', '?copy_aux@@YAXXZ'])
        with self.assertRaisesRegex(ValueError, 'did not find copy; its folded alias differs'):
            self.run_folded_library_row(0x500100, 0x420010, 'memcpy')
        self.assertFalse((self.args.reports / 'result.json').exists())

    def test_folded_alias_differing_only_in_a_relative_call_is_normalized(self):
        (self.metadata / 'metadata.csv').write_text('address,type,size,symbol\n0x1020,function,8,?Erase@@YAXXZ\n'
                                                    '0x1100,function,4,?ReturnPool@@YAXXZ\n')
        self.target['functions'] = [{'address': '0x1020', 'size': 8, 'name': 'Erase'}]
        # Generated bytes: push 1; call ReturnPool; ret, at 0x1020 and at the rebuilt 0x2020.
        bodies = {self.original: b'\x6a\x01\xe8' + struct.pack('<i', 0x1100 - 0x1027) + b'\xc3'}
        rebuilt = b'\x6a\x01\xe8' + struct.pack('<i', 0x2400 - 0x2027) + b'\xc3'
        build = self.tool.build
        def with_folded_object(folder, sources, *args, **kwargs):
            binary, pdb = build(folder, sources, *args, **kwargs)
            binary.with_suffix('.map').write_text(
                ' 0001:00000020 ?Erase@@YAXXZ 00002020 f i list.obj\n'
                ' 0001:00000020 ?EraseOwner@@YAXXZ 00002020 f i owner.obj\n'
                ' 0001:00000400 ?ReturnPool@@YAXXZ 00002400 f pool.obj\n')
            (folder / 'link.rsp').write_text('"/nologo"\n"list.obj"\n"owner.obj"\n"pool.obj"\n')
            (folder / 'list.obj').write_bytes(MatchingInputTests.call_coff(
                '?Erase@@YAXXZ', '?ReturnPool@@YAXXZ', b'\x6a\x01\xe8\0\0\0\0\xc3'))
            return binary, pdb
        self.tool.build = with_folded_object
        with patch.object(matching, 'read_va', side_effect=lambda path, address, size: bodies.get(path, rebuilt)), \
                patch.object(matching, 'pe_base_relocations', return_value=[]), \
                patch.object(matching, 'pe_imports', return_value={}):
            result = self.run_game()
            self.assertEqual(result['functions'], [
                {'name': 'Erase', 'address': '0x1020', 'bytes': 8, 'normalized_match': 1.0,
                 'exact_bytes': False, 'folded': True, 'relocated_operands': [
                     {'offset': 3, 'identity': 'symbol:?ReturnPool@@YAXXZ', 'relative': True}]}])
            rebuilt = rebuilt[:-1] + b'\x90'
            with self.assertRaisesRegex(ValueError, 'its folded alias differs'):
                self.run_game()
            (self.args.reports / 'result.json').unlink(missing_ok=True)
            rebuilt = b'\x6a\x01\xe8' + struct.pack('<i', 0x2404 - 0x2027) + b'\xc3'
            with self.assertRaisesRegex(ValueError, 'its folded alias differs'):
                self.run_game()

    def test_registration_conflicts_and_target_overrides_fail_before_build(self):
        paths = self.add_registration(0x1020)
        cases = [({'functions': [{'address': '0x1020', 'name': 'duplicate', 'size': 4}]},
                  {'functions': [{'address': '0x00001020', 'name': 'new', 'size': 4}]},
                  'Duplicate function address'),
                 ({'source_compile_options': {'helper.cpp': ['/GX']}},
                  {'source_compile_options': {'helper.cpp': ['/Ob1']}}, 'Duplicate source_compile_options'),
                 ({}, {'sha256': 'overridden'}, 'Unsupported matching target fragment')]
        original = self.target.copy()
        for base, fragment, error in cases:
            self.target = original | base
            paths[1].write_text(json.dumps(fragment))
            with self.subTest(error=error), self.assertRaisesRegex(ValueError, error):
                self.run_game()
            self.assertEqual(self.calls, [])

    def test_duplicate_metadata_and_invalid_fragment_headers_fail_before_build(self):
        paths = self.add_registration(0x1020)
        for csv in ('address,type,size,symbol\n0x1020,function,4,?a\n0x00001020,function,4,?b\n',
                    'address,symbol\n0x1020,?a\n'):
            paths[0].write_text(csv)
            with self.subTest(csv=csv), self.assertRaises(ValueError):
                self.run_game()
            self.assertEqual(self.calls, [])
        paths[0].write_text('address,type,size,symbol\n0x1020,function,4,?a\n')
        (self.metadata / 'metadata.csv').write_text('address,type,size,symbol\n0x00001020,function,4,?b\n')
        with self.assertRaisesRegex(ValueError, 'Duplicate matching metadata address'):
            self.run_game()
        self.assertEqual(self.calls, [])

    def test_shared_callee_registrations_merge_once_and_conflicts_fail(self):
        anchor = {'address': '0x1080', 'size': 8, 'name': 'Helper::Exit', 'symbol': '?Exit@Helper@@QAEXXZ',
                  'header': 'helper.h', 'kind': 'void-member', 'return_type': 'void', 'parameters': []}
        for address in (0x1020, 0x1040):
            paths = self.add_registration(address, comparison_anchors=[anchor])
            paths[0].write_text(paths[0].read_text() + '0x1080,function,8,?Exit@Helper@@QAEXXZ\n')
        rows = matching.game_metadata_rows(self.metadata)
        self.assertEqual([row['address'] for row in rows], ['0x00001020', '0x1080', '0x00001040'])
        target = {}
        matching.merge_target_fragments(self.metadata, target)
        self.assertEqual(target['comparison_anchors'], [anchor])
        paths[0].write_text(paths[0].read_text().replace(',8,', ',12,'))
        with self.assertRaisesRegex(ValueError, 'Duplicate matching metadata address'):
            matching.game_metadata_rows(self.metadata)

    def test_matched_body_supersedes_identical_anchor_only(self):
        anchor = {'address': '0x00001020', 'size': 4, 'name': 'Helper::Exit', 'symbol': '?method_00001020@@YAXXZ',
                  'header': 'helper.h', 'kind': 'void-member', 'return_type': 'void', 'parameters': []}
        self.add_registration(0x1020)
        later = self.add_registration(0x1040, comparison_anchors=[anchor])
        later[0].write_text(later[0].read_text() + '0x00001020,function,4,?method_00001020@@YAXXZ\n')
        target = {'functions': []}
        matching.merge_target_fragments(self.metadata, target)
        self.assertEqual(target['comparison_anchors'], [])
        later[1].write_text(json.dumps({'functions': [{'address': '0x00001040', 'size': 4, 'name': 'method_00001040'}],
                                        'comparison_anchors': [anchor | {'size': 8}]}))
        target = {'functions': []}
        matching.merge_target_fragments(self.metadata, target)
        self.assertEqual(target['comparison_anchors'], [anchor | {'size': 8}])


if __name__ == '__main__':
    unittest.main()
