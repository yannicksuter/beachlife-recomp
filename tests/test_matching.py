import hashlib
import importlib.util
import contextlib
import io
import json
from pathlib import Path
import struct
import tempfile
from types import SimpleNamespace
import unittest

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
        self.build_arguments = None
        self.tamper = False
        self.tool = SimpleNamespace(decomp_root=self.metadata, output=self.args.output,
                                    compiler=self.root / 'compiler/Bin', reccmp='generated-comparator',
                                    native=str, runtime_library=self.runtime_library,
                                    build=self.build, run=self.compare)

    def runtime_library(self, folder):
        self.calls.append('runtime_library')
        return 'generated-import.lib'

    def build(self, folder, sources, name, extra_link=(), extra_compile=(), source_flags=None):
        self.calls.append('build')
        self.build_arguments = (extra_link, extra_compile, source_flags)
        if self.tamper:
            path = folder / 'abi/vc6-except-list.obj'
            path.write_bytes(path.read_bytes()[:-1] + b'X')
        return folder / 'generated.exe', folder / 'generated.pdb'

    def compare(self, arguments, folder):
        self.calls.append('compare')
        report = Path(arguments[arguments.index('--json') + 1])
        report.write_text(json.dumps({'data': []}))
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
        self.assertEqual(result['link_abi_support'], [])
        self.assertFalse((self.args.output / 'game/abi').exists())

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

    def test_invalid_configuration_fails_before_any_build_tool(self):
        bad = [None, [], True, '/GX', {'helper.cpp': '/GX'}, {'helper.cpp': [True]},
               {'helper.cpp': [['/GX']]}, {'helper.cpp': ['/GX', '/GX']},
               {'helper.cpp': ['/GX-']}, {'helper.cpp': ['/O1']}, {'helper.cpp': ['/Ob2']},
               {'helper.cpp': ['/Iother']}, {'../helper.cpp': ['/GX']},
               {'sub/helper.cpp': ['/GX']}, {'sub\\helper.cpp': ['/GX']},
               {'C:helper.cpp': ['/GX']}, {'/helper.cpp': ['/GX']},
               {'helper.cpp\n': ['/GX']}, {'missing.cpp': ['/GX']},
               {'operator_delete.cpp': ['/GX']}]
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


if __name__ == '__main__':
    unittest.main()
