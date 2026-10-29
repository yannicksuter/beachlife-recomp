import copy
import importlib.util
import json
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from types import SimpleNamespace
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
spec = importlib.util.spec_from_file_location('matching', Path(__file__).resolve().parents[1] / 'tools/matching.py')
matching = importlib.util.module_from_spec(spec)
spec.loader.exec_module(matching)


class ComparisonAnchorTests(unittest.TestCase):
    def setUp(self):
        build = matching.ROOT / 'build'
        build.mkdir(exist_ok=True)
        temporary = tempfile.TemporaryDirectory(dir=build)
        self.addCleanup(temporary.cleanup)
        self.directory = Path(temporary.name)
        self.metadata = self.directory / 'prepared'
        self.source = self.metadata / 'src'
        self.source.mkdir(parents=True)
        (self.source / 'generated.h').write_text(
            'struct GeneratedResult;\n'
            'struct GeneratedOwner { GeneratedResult* Find(const char*); };\n')
        self.anchor = {'address': '0x00401100', 'size': 16, 'name': 'GeneratedOwner::Find',
                       'symbol': '?Find@GeneratedOwner@@QAEPAXPBD@Z', 'header': 'generated.h',
                       'kind': 'null-pointer-member', 'return_type': 'GeneratedResult*',
                       'parameters': ['const char*']}
        self.target = {'functions': [{'name': 'GeneratedOwner::Lookup', 'address': '0x00401000', 'size': 13}],
                       'comparison_anchors': [self.anchor]}
        (self.metadata / 'metadata.csv').write_text(
            'address,type,size,symbol\n0x00401100,function,16,?Find@GeneratedOwner@@QAEPAXPBD@Z\n')

    def test_no_anchors_produce_no_files_or_processes(self):
        with patch.object(matching.subprocess, 'run', side_effect=AssertionError('Unexpected process')):
            self.assertEqual(matching.comparison_anchor_specs(self.metadata, {'functions': []}), [])
            self.assertEqual(matching.generate_comparison_anchors(
                self.metadata, self.source, self.directory / 'absent', []), ([], []))
        self.assertFalse((self.directory / 'absent').exists())

    def test_declaration_generates_only_ignored_unqualified_source(self):
        specs = matching.comparison_anchor_specs(self.metadata, self.target)
        paths, records = matching.generate_comparison_anchors(
            self.metadata, self.source, self.directory / 'output', specs)
        self.assertEqual(len(paths), 1)
        self.assertNotIn(paths[0], self.metadata.rglob('*'))
        self.assertEqual(paths[0].read_text(),
                         '#include "generated.h"\n\n'
                         '// GENERATED COMPARISON-ONLY ANCHOR. Never execute this image.\n'
                         '// This is no implementation or qualification of the original dependency.\n'
                         'static volatile unsigned int comparison_anchor_marker;\n\n'
                         '// FUNCTION: BEACHLIFE 0x00401100\n'
                         'GeneratedResult* GeneratedOwner::Find(const char* argument)\n'
                         '{\n    comparison_anchor_marker = 0x00401100;\n    return 0;\n}\n')
        self.assertIn('Never execute this image', paths[0].read_text())
        self.assertFalse(records[0]['qualified'])
        self.assertFalse(records[0]['image_execution_permitted'])
        self.assertEqual(records[0]['sha256'], matching.digest(paths[0]))
        self.assertEqual(self.target['functions'],
                         [{'name': 'GeneratedOwner::Lookup', 'address': '0x00401000', 'size': 13}])

    def test_void_member_generates_only_ignored_void_thiscall_source(self):
        (self.source / 'generated.h').write_text(
            'struct GeneratedResult;\n'
            'struct GeneratedOwner { GeneratedResult* Find(const char*); void Load(const char*); };\n')
        anchor = {'address': '0x00401200', 'size': 20, 'name': 'GeneratedOwner::Load',
                  'symbol': '?Load@GeneratedOwner@@QAEXPBD@Z', 'header': 'generated.h',
                  'kind': 'void-member', 'return_type': 'void', 'parameters': ['const char*']}
        target = copy.deepcopy(self.target)
        target['comparison_anchors'].append(anchor)
        with (self.metadata / 'metadata.csv').open('a') as stream:
            stream.write('0x00401200,function,20,?Load@GeneratedOwner@@QAEXPBD@Z\n')
        specs = matching.comparison_anchor_specs(self.metadata, target)
        paths, records = matching.generate_comparison_anchors(
            self.metadata, self.source, self.directory / 'output', specs)
        self.assertEqual(len(paths), 2)
        self.assertEqual(paths[1].read_text(),
                         '#include "generated.h"\n\n'
                         '// GENERATED COMPARISON-ONLY ANCHOR. Never execute this image.\n'
                         '// This is no implementation or qualification of the original dependency.\n'
                         'static volatile unsigned int comparison_anchor_marker;\n\n'
                         '// FUNCTION: BEACHLIFE 0x00401200\n'
                         'void GeneratedOwner::Load(const char* argument)\n'
                         '{\n    comparison_anchor_marker = 0x00401200;\n    (void)argument;\n}\n')
        self.assertFalse(records[1]['qualified'])
        self.assertFalse(records[1]['image_execution_permitted'])
        self.assertNotIn(paths[1], self.metadata.rglob('*'))
        self.assertEqual(target['functions'], self.target['functions'])
        anchor['return_type'] = 'GeneratedResult*'
        with self.assertRaisesRegex(ValueError, 'declaration'):
            matching.comparison_anchor_specs(self.metadata, target)

    def test_typed_and_empty_parameter_lists_generate_matching_signatures(self):
        (self.source / 'generated.h').write_text(
            'struct GeneratedResult;\n'
            'struct GeneratedContext;\n'
            'struct GeneratedOwner { GeneratedResult* Find(const char*); void Close();\n'
            '    bool Request(const char*, GeneratedContext*, unsigned char); };\n')
        close = {'address': '0x00401200', 'size': 20, 'name': 'GeneratedOwner::Close',
                 'symbol': '?Close@GeneratedOwner@@QAEXXZ', 'header': 'generated.h',
                 'kind': 'void-member', 'return_type': 'void', 'parameters': []}
        request = {'address': '0x00401300', 'size': 24, 'name': 'GeneratedOwner::Request',
                   'symbol': '?Request@GeneratedOwner@@QAE_NPBDPAUGeneratedContext@@E@Z',
                   'header': 'generated.h', 'kind': 'false-member', 'return_type': 'bool',
                   'parameters': ['const char*', 'GeneratedContext*', 'unsigned char']}
        target = copy.deepcopy(self.target)
        target['comparison_anchors'] += [close, request]
        with (self.metadata / 'metadata.csv').open('a') as stream:
            stream.write('0x00401200,function,20,?Close@GeneratedOwner@@QAEXXZ\n'
                         '0x00401300,function,24,?Request@GeneratedOwner@@QAE_NPBDPAUGeneratedContext@@E@Z\n')
        specs = matching.comparison_anchor_specs(self.metadata, target)
        paths, records = matching.generate_comparison_anchors(
            self.metadata, self.source, self.directory / 'output', specs)
        prefix = ('#include "generated.h"\n\n'
                  '// GENERATED COMPARISON-ONLY ANCHOR. Never execute this image.\n'
                  '// This is no implementation or qualification of the original dependency.\n'
                  'static volatile unsigned int comparison_anchor_marker;\n\n')
        self.assertEqual(paths[1].read_text(), prefix +
                         '// FUNCTION: BEACHLIFE 0x00401200\n'
                         'void GeneratedOwner::Close()\n'
                         '{\n    comparison_anchor_marker = 0x00401200;\n}\n')
        self.assertEqual(paths[2].read_text(), prefix +
                         '// FUNCTION: BEACHLIFE 0x00401300\n'
                         'bool GeneratedOwner::Request(const char* argument1, '
                         'GeneratedContext* argument2, unsigned char argument3)\n'
                         '{\n    comparison_anchor_marker = 0x00401300;\n    return false;\n}\n')
        self.assertFalse(any(record['qualified'] or record['image_execution_permitted']
                             for record in records))
        for changed in ({'return_type': 'int'}, {'kind': 'void-member'}):
            rejected = copy.deepcopy(target)
            rejected['comparison_anchors'][2].update(changed)
            with self.assertRaisesRegex(ValueError, 'declaration'):
                matching.comparison_anchor_specs(self.metadata, rejected)

    def test_unsigned_byte_return_keeps_its_abi_and_remains_unqualified(self):
        (self.source / 'generated.h').write_text(
            'struct GeneratedOwner { unsigned char Input(unsigned int, unsigned int, '
            'unsigned int, unsigned int); };\n')
        anchor = {'address': '0x00401200', 'size': 25, 'name': 'GeneratedOwner::Input',
                  'symbol': '?Input@GeneratedOwner@@QAEEIIII@Z', 'header': 'generated.h',
                  'kind': 'false-member', 'return_type': 'unsigned char',
                  'parameters': ['unsigned int'] * 4}
        target = {'functions': self.target['functions'], 'comparison_anchors': [anchor]}
        (self.metadata / 'metadata.csv').write_text(
            'address,type,size,symbol\n0x00401200,function,25,?Input@GeneratedOwner@@QAEEIIII@Z\n')
        specs = matching.comparison_anchor_specs(self.metadata, target)
        paths, records = matching.generate_comparison_anchors(
            self.metadata, self.source, self.directory / 'output', specs)
        self.assertIn('unsigned char GeneratedOwner::Input(unsigned int argument1, '
                      'unsigned int argument2, unsigned int argument3, unsigned int argument4)',
                      paths[0].read_text())
        self.assertIn('comparison_anchor_marker = 0x00401200;', paths[0].read_text())
        self.assertNotIn(paths[0], self.metadata.rglob('*'))
        self.assertFalse(records[0]['qualified'])
        self.assertFalse(records[0]['image_execution_permitted'])
        self.assertEqual(target['functions'], self.target['functions'])
        anchor['return_type'] = 'unsigned int'
        with self.assertRaisesRegex(ValueError, 'declaration'):
            matching.comparison_anchor_specs(self.metadata, target)

    def test_credited_address_name_and_duplicate_anchor_are_rejected(self):
        for changed in ({'address': '0x00401000'}, {'name': 'GeneratedOwner::Lookup'}):
            target = copy.deepcopy(self.target)
            target['comparison_anchors'][0].update(changed)
            with self.assertRaisesRegex(ValueError, 'distinct'):
                matching.comparison_anchor_specs(self.metadata, target)
        target = copy.deepcopy(self.target)
        target['comparison_anchors'].append(copy.deepcopy(self.anchor))
        with self.assertRaisesRegex(ValueError, 'distinct'):
            matching.comparison_anchor_specs(self.metadata, target)

    def test_csv_and_declaration_must_be_exact(self):
        for changed in ({'header': '../generated.h'}, {'return_type': 'void*; bad()'},
                        {'name': 'GeneratedOwner::Find()'}, {'parameters': 'const char*'},
                        {'parameters': ['const char* argument']}, {'parameters': ['int'] * 17},
                        {'kind': 'game-implementation'}, {'unexpected': True}):
            target = copy.deepcopy(self.target)
            target['comparison_anchors'][0].update(changed)
            with self.assertRaisesRegex(ValueError, 'declaration'):
                matching.comparison_anchor_specs(self.metadata, target)
        self.anchor['size'] = 15
        with self.assertRaisesRegex(ValueError, 'CSV identity'):
            matching.comparison_anchor_specs(self.metadata, self.target)

    def test_source_tree_and_unignored_destinations_are_rejected(self):
        specs = matching.comparison_anchor_specs(self.metadata, self.target)
        with self.assertRaisesRegex(ValueError, 'outside reconstructed source'):
            matching.generate_comparison_anchors(self.metadata, self.source, self.source, specs)
        with patch.object(matching.subprocess, 'run', return_value=SimpleNamespace(returncode=1)):
            with self.assertRaisesRegex(ValueError, 'ignored output'):
                matching.generate_comparison_anchors(
                    self.metadata, self.source, self.directory / 'output', specs)

    def test_changed_and_unexpected_output_cannot_be_reused_silently(self):
        specs = matching.comparison_anchor_specs(self.metadata, self.target)
        paths, _ = matching.generate_comparison_anchors(
            self.metadata, self.source, self.directory / 'output', specs)
        paths[0].write_text('unexpected source\n')
        with self.assertRaisesRegex(ValueError, 'source changed'):
            matching.generate_comparison_anchors(
                self.metadata, self.source, self.directory / 'output', specs)
        paths[0].with_name('unexpected.cpp').write_text('unregistered\n')
        with self.assertRaisesRegex(ValueError, 'Unexpected files'):
            matching.generate_comparison_anchors(
                self.metadata, self.source, self.directory / 'output', specs)

    def generate(self, **changes):
        anchor = self.anchor | changes
        (self.metadata / 'metadata.csv').write_text(
            f'address,type,size,symbol\n{anchor["address"]},function,{anchor["size"]},{anchor["symbol"]}\n')
        target = self.target | {'comparison_anchors': [anchor]}
        specs = matching.comparison_anchor_specs(self.metadata, target)
        folder = self.directory / ('output_' + str(len(list(self.directory.glob('output_*')))))
        paths, records = matching.generate_comparison_anchors(self.metadata, self.source, folder, specs)
        self.assertFalse(records[0]['qualified'])
        self.assertFalse(records[0]['image_execution_permitted'])
        self.assertIn('static volatile unsigned int comparison_anchor_marker;', paths[0].read_text())
        return paths[0].read_text()

    def test_scalar_returns_use_the_declared_width_and_floating_type(self):
        for kind, types, zero in (
                ('integer-member', matching.ANCHOR_INTEGERS, '0'),
                ('float-member', ('float',), '0.0f'),
                ('double-member', ('double',), '0.0')):
            for return_type in types:
                with self.subTest(return_type=return_type):
                    body = self.generate(kind=kind, return_type=return_type)
                    self.assertIn(f'{return_type} GeneratedOwner::Find(', body)
                    self.assertIn(f'    return {zero};', body)
        for kind, result in (('float-member', 'double'), ('double-member', 'long double'),
                             ('integer-member', 'bool'), ('integer-member', 'unsigned __int64'),
                             ('integer-member', 'GeneratedEnum'), ('float-member', 'float&')):
            with self.subTest(kind=kind, result=result), self.assertRaisesRegex(ValueError, 'declaration'):
                self.generate(kind=kind, return_type=result)

    def test_named_aggregate_returns_are_value_initialized(self):
        for name in ('GeneratedValue', 'GeneratedOwner::Value'):
            body = self.generate(kind='aggregate-member', return_type=name, parameters=[])
            self.assertIn(f'    return {name}();', body)
        for name in ('int', 'void', 'Value[2]', 'Value*', 'Value&', 'Value<int>', 'Value(); bad()'):
            with self.subTest(name=name), self.assertRaisesRegex(ValueError, 'declaration'):
                self.generate(kind='aggregate-member', return_type=name)

    def test_five_six_and_sixteen_parameters_are_bounded(self):
        for count in (5, 6, 11, 16):
            body = self.generate(kind='void-member', return_type='void', parameters=['int'] * count)
            self.assertIn(f'int argument{count})', body)
            self.assertIn(f'    (void)argument{count};', body)
        with self.assertRaisesRegex(ValueError, 'declaration'):
            self.generate(parameters=['int'] * 17)

    def test_reference_and_double_pointer_shapes(self):
        parameters = ['GeneratedResult**', 'const GeneratedResult* const&', 'int&',
                      'const GeneratedOwner::Value&', 'unsigned short**', 'void* const*']
        body = self.generate(return_type='GeneratedResult**', parameters=parameters)
        for index, parameter in enumerate(parameters, 1):
            self.assertIn(f'{parameter} argument{index}', body)
        self.assertIn('GeneratedResult** GeneratedOwner::Find(', body)
        for parameter in ('void', 'void&', 'int&&', 'int***', 'int[]', 'int value',
                          'int = 0', '...', 'int; bad()', 'volatile int', 'long double',
                          'unsigned Thing', 'void (*)(int)', 'Thing<int>'):
            with self.subTest(parameter=parameter), self.assertRaisesRegex(ValueError, 'declaration'):
                self.generate(parameters=[parameter])
        for result in ('GeneratedResult***', 'GeneratedResult*&'):
            with self.assertRaisesRegex(ValueError, 'declaration'):
                self.generate(return_type=result)

    def test_nested_owners_and_const_members(self):
        body = self.generate(name='GeneratedOwner::Nested::Find', qualifier='const')
        self.assertIn('GeneratedOwner::Nested::Find(const char* argument) const\n', body)
        for changes in ({'name': 'GeneratedOwner::GeneratedOwner'}, {'name': 'Owner::~Owner'},
                        {'name': 'Owner::Nested::operator()'}, {'name': 'Owner<int>::Find'},
                        {'name': 'Find'}, {'name': 'Owner::void'}, {'qualifier': 'volatile'},
                        {'qualifier': 'const; bad()'}, {'calling_convention': '__stdcall'}):
            with self.subTest(changes=changes), self.assertRaisesRegex(ValueError, 'declaration'):
                self.generate(**changes)

    def test_free_functions_bind_the_header_declaration_before_definition(self):
        for convention, symbol in (('__cdecl', '?GeneratedFree@@YAHH@Z'),
                                   ('__stdcall', '_GeneratedFree@4')):
            body = self.generate(kind='integer-function', return_type='int', name='GeneratedFree',
                                 symbol=symbol, parameters=['int'], calling_convention=convention)
            binding = ('typedef int (' + convention + ' *comparison_anchor_signature)(int);\n'
                       'static comparison_anchor_signature const comparison_anchor_declaration = &GeneratedFree;')
            self.assertIn(binding, body)
            self.assertLess(body.index(binding), body.index(f'int {convention} GeneratedFree('))
        for changes in ({'calling_convention': '__fastcall'}, {'qualifier': 'const'},
                        {'name': 'GeneratedFree()'}, {'symbol': 'plain undecorated'}):
            with self.subTest(changes=changes), self.assertRaisesRegex(ValueError, 'declaration'):
                self.generate(**({'kind': 'void-function', 'return_type': 'void',
                                  'name': 'GeneratedFree'} | changes))

    def test_callbacks_use_header_typedefs_and_explicit_calling_conventions(self):
        body = self.generate(kind='null-callback-function', return_type='GeneratedCallback',
                             name='VisitGenerated', parameters=['GeneratedRange', 'GeneratedCallback'])
        self.assertIn('GeneratedCallback __cdecl VisitGenerated(GeneratedRange argument1, '
                      'GeneratedCallback argument2)', body)
        self.assertIn('    return 0;', body)
        body = self.generate(kind='void-function', return_type='void', name='GeneratedCallbackBody',
                             parameters=['void*', 'int'], calling_convention='__stdcall')
        self.assertIn('void __stdcall GeneratedCallbackBody(void* argument1, int argument2)', body)
        for result in ('void (*)(int)', 'GeneratedCallback*', 'int'):
            with self.assertRaisesRegex(ValueError, 'declaration'):
                self.generate(kind='null-callback-function', return_type=result, name='VisitGenerated')

    def test_structured_callback_parameters_cannot_inject_declarators(self):
        callback = {'return_type': 'bool', 'parameters': ['const GeneratedResult*', 'const void*'],
                    'calling_convention': '__cdecl'}
        for convention in ('__cdecl', '__stdcall'):
            body = self.generate(parameters=['unsigned int*', callback | {'calling_convention': convention}])
            self.assertIn('bool (' + convention + ' *argument2)(const GeneratedResult*, const void*)', body)
        for changed in ({'return_type': 'GeneratedResult'}, {'return_type': 'int&'},
                        {'parameters': ['void']}, {'parameters': [callback]},
                        {'parameters': ['int'] * 17}, {'calling_convention': '__fastcall'},
                        {'calling_convention': '__cdecl); bad('}, {'name': 'bad'},
                        {'parameters': ['int value']}):
            with self.subTest(changed=changed), self.assertRaisesRegex(ValueError, 'declaration'):
                self.generate(parameters=[callback | changed])

    def test_missing_duplicate_and_nonfunction_metadata_identities_fail(self):
        row = '0x00401100,function,16,?Find@GeneratedOwner@@QAEPAXPBD@Z\n'
        for rows in ('', row * 2, row.replace(',function,', ',data,'), row.replace(',16,', ',17,')):
            (self.metadata / 'metadata.csv').write_text('address,type,size,symbol\n' + rows)
            with self.assertRaisesRegex(ValueError, 'CSV identity'):
                matching.comparison_anchor_specs(self.metadata, self.target)

    def test_overlapping_ranges_and_credited_symbols_fail(self):
        for address in ('0x0040100c', '0x00400ff8'):
            with self.assertRaisesRegex(ValueError, 'distinct'):
                self.generate(address=address)
        (self.metadata / 'metadata.csv').write_text(
            'address,type,size,symbol\n'
            '0x00401000,function,13,?Find@GeneratedOwner@@QAEPAXPBD@Z\n'
            '0x00401100,function,16,?Find@GeneratedOwner@@QAEPAXPBD@Z\n')
        with self.assertRaisesRegex(ValueError, 'distinct'):
            matching.comparison_anchor_specs(self.metadata, self.target)

    @unittest.skipUnless(shutil.which('c++'), 'C++ syntax check requires a host compiler')
    def test_shared_header_is_the_authority_for_generated_definitions(self):
        header = ('struct GeneratedResult { int word; };\n'
                  'typedef void (*GeneratedCallback)(int);\n'
                  'struct GeneratedOwner {\n'
                  ' float Floating(int); double Double(); int Integer();\n'
                  ' GeneratedResult Aggregate();\n'
                  ' GeneratedResult** Pointers(GeneratedResult* const&, int**);\n'
                  ' void Many(int, int, int, int, int, int);\n'
                  ' void Callback(bool (*)(const GeneratedResult*, const void*));\n'
                  ' struct Nested { bool Read(const int&) const; };\n'
                  '};\n'
                  'int GeneratedFree(int);\n'
                  'GeneratedCallback VisitGenerated(GeneratedCallback);\n')
        (self.source / 'generated.h').write_text(header)
        cases = [
            {'kind': 'float-member', 'return_type': 'float', 'name': 'GeneratedOwner::Floating',
             'parameters': ['int']},
            {'kind': 'double-member', 'return_type': 'double', 'name': 'GeneratedOwner::Double',
             'parameters': []},
            {'kind': 'integer-member', 'return_type': 'int', 'name': 'GeneratedOwner::Integer',
             'parameters': []},
            {'kind': 'aggregate-member', 'return_type': 'GeneratedResult',
             'name': 'GeneratedOwner::Aggregate', 'parameters': []},
            {'return_type': 'GeneratedResult**', 'name': 'GeneratedOwner::Pointers',
             'parameters': ['GeneratedResult* const&', 'int**']},
            {'kind': 'void-member', 'return_type': 'void', 'name': 'GeneratedOwner::Many',
             'parameters': ['int'] * 6},
            {'kind': 'false-member', 'return_type': 'bool', 'name': 'GeneratedOwner::Nested::Read',
             'parameters': ['const int&'], 'qualifier': 'const'},
            {'kind': 'integer-function', 'return_type': 'int', 'name': 'GeneratedFree',
             'parameters': ['int']},
            {'kind': 'null-callback-function', 'return_type': 'GeneratedCallback',
             'name': 'VisitGenerated', 'parameters': ['GeneratedCallback']},
            {'kind': 'void-member', 'return_type': 'void', 'name': 'GeneratedOwner::Callback',
             'parameters': [{'return_type': 'bool', 'parameters': ['const GeneratedResult*', 'const void*'],
                             'calling_convention': '__cdecl'}]},
        ]
        for case in cases:
            for valid in (True, False):
                # A valid grammar shape still must match an actual declaration.
                changed = case if valid else case | {'name': case['name'] + 'Undeclared'}
                body = self.generate(**changed)
                path = self.directory / 'syntax.cpp'
                path.write_text(body)
                result = subprocess.run([shutil.which('c++'), '-std=c++98', '-fsyntax-only',
                                         '-D__cdecl=', '-D__stdcall=', '-I' + str(self.source), str(path)],
                                        capture_output=True, text=True)
                with self.subTest(case=case, valid=valid):
                    self.assertEqual(result.returncode == 0, valid, result.stderr)
        (self.source / 'generated.h').write_text(
            'class GeneratedResult { GeneratedResult(); };\n'
            'struct GeneratedOwner { GeneratedResult Aggregate(); };\n')
        path.write_text(self.generate(**cases[3]))
        result = subprocess.run([shutil.which('c++'), '-std=c++98', '-fsyntax-only',
                                 '-I' + str(self.source), str(path)], capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0, 'Inaccessible value initialization must fail.')


class ComparisonAnchorObjectTests(unittest.TestCase):
    @staticmethod
    def coff(symbol, section=1, symbol_type=0x20, storage=2, copies=1):
        # Synthetic COFF tables and one placeholder code byte, no retail input.
        header = struct.pack('<HHIIIHH', 0x14c, 1, 0, 61, copies, 0, 0)
        code = struct.pack('<8sIIIIIIHHI', b'.text', 0, 0, 1, 60, 0, 0, 0, 0, 0x60000020)
        entry = struct.pack('<IIIhHBB', 0, 4, 0, section, symbol_type, storage, 0)
        name = symbol.encode('ascii') + b'\0'
        return header + code + b'\0' + entry * copies + struct.pack('<I', len(name) + 4) + name

    def test_exact_defined_symbol_is_required(self):
        expected = '?Generated@@YAHXZ'
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / 'generated.obj'
            path.write_bytes(self.coff(expected))
            matching.verify_comparison_anchor_object(path, expected)
            for data in (self.coff('?Wrong@@YAHXZ'), self.coff(expected, section=0),
                         self.coff(expected, symbol_type=0), self.coff(expected, storage=3),
                         self.coff(expected, copies=2), self.coff(expected, section=2),
                         self.coff(expected)[:-1], b'not COFF'):
                with self.subTest(data=data), self.assertRaises(ValueError):
                    path.write_bytes(data)
                    matching.verify_comparison_anchor_object(path, expected)

    def test_build_rejects_wrong_anchor_symbol_before_linking(self):
        with tempfile.TemporaryDirectory() as folder:
            folder = Path(folder)
            source = folder / 'generated.cpp'
            source.write_text('// synthetic compiler input\n')
            tool = object.__new__(matching.Toolchain)
            tool.compiler = folder
            tool.native = str
            def compile_only(command, cwd):
                self.assertTrue(any('CL.EXE' in str(argument) for argument in command))
                (cwd / 'generated.obj').write_bytes(self.coff('?Wrong@@YAHXZ'))
                return ''
            tool.run = compile_only
            with self.assertRaisesRegex(ValueError, 'exactly the declared symbol'):
                tool.build(folder, [source], 'generated', anchor_symbols={source: '?Generated@@YAHXZ'})


if __name__ == '__main__':
    unittest.main()
