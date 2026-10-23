import copy
import importlib.util
import json
from pathlib import Path
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
                        {'parameters': ['const char* argument']}, {'parameters': ['int'] * 5},
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


if __name__ == '__main__':
    unittest.main()
