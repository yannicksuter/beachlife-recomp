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


class MatchingCompilerOptionsTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.source = self.root / 'source'
        self.source.mkdir()
        (self.source / 'helper.cpp').write_text('int fixture() { return 1; }\n')
        self.metadata = self.root / 'metadata'
        for name in ('fixtures', 'support', 'targets'):
            (self.metadata / name).mkdir(parents=True)
        (self.metadata / 'fixtures/game.cpp').write_text('void fixture_entry() {}\n')
        (self.metadata / 'support/operator_delete.cpp').write_text('// generated fixture\n')
        self.original = self.root / 'generated.bin'
        data = b'generated identity only; no executable code'
        self.original.write_bytes(data)
        self.target = {'id': 'generated', 'size': len(data),
                       'sha1': hashlib.sha1(data).hexdigest(),
                       'sha256': hashlib.sha256(data).hexdigest(), 'functions': []}
        self.args = SimpleNamespace(source=self.source, original=self.original,
                                    output=self.root / 'output', reports=self.root / 'reports')
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

    def build(self, folder, sources, name, extra_link=(), extra_compile=(), source_flags=None):
        self.calls.append('build')
        self.build_arguments = (extra_link, extra_compile, source_flags)
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
        return folder / 'generated.exe', folder / 'generated.pdb'

    def compare(self, arguments, folder):
        self.calls.append('compare')
        if self.tamper_sdk_compare:
            self.sdk_header.write_bytes(b'changed during comparison')
        report = Path(arguments[arguments.index('--json') + 1])
        report.write_text(json.dumps({'data': self.comparison_rows}))
        return ''

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
        self.assertIn(str(self.sdk_pin), result['input_sha256'])
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
        self.assertEqual(result['input_sha256'][str(support)], matching.digest(support))
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
        self.assertIn(str(self.source / 'helper.cpp'), result['input_sha256'])
        self.assertIn(str(self.metadata / 'targets/beachlife-uk-1.1.json'), result['input_sha256'])

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


if __name__ == '__main__':
    unittest.main()
