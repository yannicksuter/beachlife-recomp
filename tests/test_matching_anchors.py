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

    def test_existing_prepared_body_needs_an_identity_not_an_anchor(self):
        # A destructor (or any callee) that already has a body in the export is
        # named by its CSV identity; anchors stay for callees without a body.
        (self.source / 'generated.h').write_text(
            'struct GeneratedResult;\n'
            'struct GeneratedOwner { ~GeneratedOwner(); GeneratedResult* Find(const char*); };\n')
        (self.source / 'existing.cpp').write_text(
            '#include "generated.h"\n\n// FUNCTION: BEACHLIFE 0x00401200\n'
            'GeneratedOwner::~GeneratedOwner()\n{\n}\n'
            '// GLOBAL: BEACHLIFE 0x00401100\nint generated_data;\n')
        with (self.metadata / 'metadata.csv').open('a') as stream:
            stream.write('0x00401200,function,16,??1GeneratedOwner@@QAE@XZ\n')
        specs = matching.comparison_anchor_specs(self.metadata, self.target)
        paths, records = matching.generate_comparison_anchors(
            self.metadata, self.source, self.directory / 'output', specs)
        self.assertEqual([path.name for path in paths], ['anchor_00401100.cpp'])
        self.assertEqual([record['address'] for record in records], ['0x00401100'])
        self.assertEqual(matching.annotated_function_addresses([self.source / 'existing.cpp']),
                         {0x00401200: self.source / 'existing.cpp'})
        support = self.metadata / 'support'
        support.mkdir()
        for path, text in ((self.source / 'existing.cpp', '// FUNCTION: BEACHLIFE 0x00401100\n'),
                           (self.source / 'nested/folded.cpp', '// FUNCTION: BEACHLIFE 0x00401100 FOLDED\n'),
                           (support / 'library.cpp', '  // LIBRARY: BEACHLIFE 0x00401100\n')):
            with self.subTest(path=path.name):
                path.parent.mkdir(exist_ok=True)
                previous = (self.source / 'existing.cpp').read_text()
                path.write_text(text)
                with self.assertRaisesRegex(ValueError, 'CSV identity'):
                    matching.generate_comparison_anchors(
                        self.metadata, self.source, self.directory / 'rejected', specs)
                if path.name == 'existing.cpp':
                    path.write_text(previous)
                else:
                    path.unlink()
        self.assertFalse((self.directory / 'rejected').exists())

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

    def test_overloaded_anchors_keep_separate_symbols_and_addresses(self):
        overload = dict(self.anchor, address='0x00401200', parameters=['const unsigned short*'],
                        symbol='?Find@GeneratedOwner@@QAEPAXPBG@Z')
        self.target['comparison_anchors'].append(overload)
        with (self.metadata / 'metadata.csv').open('a') as stream:
            stream.write('0x00401200,function,16,?Find@GeneratedOwner@@QAEPAXPBG@Z\n')
        specs = matching.comparison_anchor_specs(self.metadata, self.target)
        paths, records = matching.generate_comparison_anchors(
            self.metadata, self.source, self.directory / 'output', specs)
        self.assertEqual([path.name for path in paths], ['anchor_00401100.cpp', 'anchor_00401200.cpp'])
        self.assertIn('Find(const char* argument)', paths[0].read_text())
        self.assertIn('Find(const unsigned short* argument)', paths[1].read_text())
        self.assertEqual([record['symbol'] for record in records], [item['symbol'] for item in specs])
        self.assertTrue(all(not r['qualified'] and not r['image_execution_permitted'] for r in records))
        for change in ({'symbol': self.anchor['symbol']}, {'address': '0x00401108'},
                       {'parameters': self.anchor['parameters']},
                       {'parameters': self.anchor['parameters'], 'return_type': 'void*'},
                       {'name': 'GeneratedOwner::Lookup'}):
            with self.subTest(change=change):
                target = copy.deepcopy(self.target)
                target['comparison_anchors'][1].update(change)
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


class ComparisonGlobalOperandTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.folder = Path(temporary.name)
        self.symbol = '?GeneratedArray@@3PAHA'
        self.caller = '?GeneratedCaller@@YAXXZ'
        self.item = {'function': '0x00401000', 'instruction_offset': 0,
                     'global': '0x00501000', 'source': 'generated.cpp'}
        self.target = {'functions': [{'address': '0x00401000', 'size': 12, 'name': 'GeneratedCaller'}],
                       'comparison_global_operands': [self.item]}
        self.csv = ('address,type,size,symbol\n'
                    '0x00401000,function,12,?GeneratedCaller@@YAXXZ\n'
                    '0x00501000,global,16,?GeneratedArray@@3PAHA\n'
                    '0x00501010,string,8,\n')
        (self.folder / 'metadata.csv').write_text(self.csv)

    def test_scope_keeps_the_real_array_and_adjacent_identity(self):
        record, = matching.comparison_global_operand_specs(self.folder, self.target)
        self.assertEqual(record['size'], 16)
        self.assertEqual(record['global_symbol'], self.symbol)
        self.assertEqual(record['function_symbol'], self.caller)
        self.assertEqual((self.folder / 'metadata.csv').read_text(), self.csv)
        self.assertEqual(self.item, {'function': '0x00401000', 'instruction_offset': 0,
                                    'global': '0x00501000', 'source': 'generated.cpp'})
        for change in ({'function': '0x00401001'}, {'instruction_offset': 8},
                       {'instruction_offset': True}, {'instruction_offset': -1},
                       {'global': '0x00501010'}, {'source': '../generated.cpp'},
                       {'offset': 17}, {'unknown': True}):
            with self.subTest(change=change), self.assertRaises(ValueError):
                matching.comparison_global_operand_specs(
                    self.folder, dict(self.target, comparison_global_operands=[dict(self.item, **change)]))
        with self.assertRaisesRegex(ValueError, 'unique instruction'):
            matching.comparison_global_operand_specs(
                self.folder, dict(self.target, comparison_global_operands=[self.item, self.item]))
        for changed in (self.csv.replace('global,16', 'function,16'),
                        self.csv.replace('function,12', 'function,13'),
                        self.csv.replace('global,16', 'global,0'),
                        self.csv.replace(self.symbol, ''), self.csv + self.csv.splitlines()[2] + '\n'):
            with self.subTest(csv=changed), self.assertRaises(ValueError):
                (self.folder / 'metadata.csv').write_text(changed)
                matching.comparison_global_operand_specs(self.folder, self.target)

    def test_member_of_the_element_past_the_end_is_bounded_by_the_element(self):
        # A loop over &array[i].member ends at array + extent + member offset.
        record, = matching.comparison_global_operand_specs(self.folder, dict(
            self.target, comparison_global_operands=[dict(self.item, member_offset=4, element_size=8)]))
        self.assertEqual((record['size'], record['addend']), (16, 20))
        record, = matching.comparison_global_operand_specs(self.folder, self.target)
        self.assertEqual(record['addend'], 16)
        for change in ({'member_offset': 8, 'element_size': 8}, {'member_offset': 4, 'element_size': 6},
                       {'member_offset': 4, 'element_size': 32}, {'member_offset': -1, 'element_size': 8},
                       {'member_offset': True, 'element_size': 8}, {'member_offset': 4, 'element_size': 0},
                       {'member_offset': 4}, {'element_size': 8}):
            with self.subTest(change=change), self.assertRaises(ValueError):
                matching.comparison_global_operand_specs(self.folder, dict(
                    self.target, comparison_global_operands=[dict(self.item, **change)]))

    @staticmethod
    def coff(symbol, global_symbol, addend=16, kind=6, target_index=1):
        code = b'\x81\xff' + struct.pack('<I', addend) + b'\xc3'
        names = symbol.encode() + b'\0' + global_symbol.encode() + b'\0'
        table = 60 + len(code) + 10
        header = struct.pack('<HHIIIHH', 0x14c, 1, 0, table, 2, 0, 0)
        section = struct.pack('<8sIIIIIIHHI', b'.text', 0, 0, len(code), 60,
                              60 + len(code), 0, 1, 0, 0x60000020)
        function = struct.pack('<IIIhHBB', 0, 4, 0, 1, 0x20, 2, 0)
        global_entry = struct.pack('<IIIhHBB', 0, 5 + len(symbol), 0, 0, 0, 2, 0)
        return (header + section + code + struct.pack('<IIH', 2, target_index, kind) +
                function + global_entry + struct.pack('<I', len(names) + 4) + names)

    def test_coff_preserves_symbol_addend_and_relocation_kind(self):
        path = self.folder / 'generated.obj'
        path.write_bytes(self.coff(self.caller, self.symbol))
        raw, relocations = matching.comparison_operand_coff(path, self.caller)
        self.assertEqual(raw, b'\x81\xff\x10\0\0\0\xc3')
        self.assertEqual(relocations, [(2, 6, self.symbol, 16)])
        for data in (self.coff(self.caller, self.symbol, target_index=2),
                     self.coff(self.caller, self.symbol)[:-1],
                     self.coff('?Wrong@@YAXXZ', self.symbol)):
            with self.subTest(data=data), self.assertRaises(ValueError):
                path.write_bytes(data)
                matching.comparison_operand_coff(path, self.caller)

    def test_only_exact_address_comparisons_are_admitted(self):
        for opcode, register, immediate in ((b'\x3d', 'eax', 1), (b'\x81\xff', 'edi', 2)):
            self.assertEqual(matching.comparison_array_end_instruction(
                opcode + struct.pack('<I', 0x501010), 0x501010), (immediate, register))
        for raw in (b'\x81\xff' + struct.pack('<I', 0x501014),  # past the real end
                    b'\x81\x3d' + struct.pack('<II', 0x501010, 0),  # dereference
                    b'\x68' + struct.pack('<I', 0x501010),  # not a comparison
                    b'\xb8' + struct.pack('<I', 0x501010), b'\x83\xff\x10', b''):
            with self.subTest(raw=raw), self.assertRaises(ValueError):
                matching.comparison_array_end_instruction(raw, 0x501010)


    def test_rebuilt_sites_come_from_exact_relocations(self):
        value, symbol = 0x00601010, self.symbol
        code = (b'\x90' + b'\x81\xf9' + struct.pack('<I', 16) + b'\x3d' + struct.pack('<I', 16) +
                b'\xb8' + struct.pack('<I', 16) + b'\xc3')
        linked = [(0x00402000, b'\x90'), (0x00402001, b'\x81\xf9' + struct.pack('<I', value)),
                  (0x00402007, b'\x3d' + struct.pack('<I', value)),
                  (0x0040200c, b'\xb8' + struct.pack('<I', value)), (0x00402011, b'\xc3')]
        relocations = [(3, 6, symbol, 16), (8, 6, symbol, 16), (13, 6, symbol, 16)]
        self.assertEqual(matching.comparison_array_end_sites(
            linked, 0x00402000, code, relocations, symbol, 16, value), [(1, 'ecx'), (7, 'eax')])
        for changed in ([(3, 6, symbol, 12), (8, 6, symbol, 16)],  # another addend
                        [(3, 20, symbol, 16), (8, 6, symbol, 16)],  # relative, not DIR32
                        [(3, 6, '?Adjacent@@3PADA', 16), (8, 6, symbol, 16)],  # adjacent entity
                        [(2, 6, symbol, 16), (8, 6, symbol, 16)]):  # not the immediate
            with self.subTest(relocations=changed):
                self.assertEqual(matching.comparison_array_end_sites(
                    linked, 0x00402000, code, changed, symbol, 16, value), [(7, 'eax')])
        # The linked address must be the array end, and the opcode must agree with the object.
        self.assertEqual(matching.comparison_array_end_sites(
            linked, 0x00402000, code, relocations, symbol, 16, value + 4), [])
        self.assertEqual(matching.comparison_array_end_sites(
            linked, 0x00402000, b'\x90\x81\xfe' + code[3:], relocations, symbol, 16, value),
            [(7, 'eax')])

if __name__ == '__main__':
    unittest.main()
