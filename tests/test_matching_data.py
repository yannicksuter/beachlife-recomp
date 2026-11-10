import copy
import importlib.util
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
spec = importlib.util.spec_from_file_location('matching', Path(__file__).resolve().parents[1] / 'tools/matching.py')
matching = importlib.util.module_from_spec(spec)
spec.loader.exec_module(matching)


def data_pe(payload, flags=0xc0000040):
    data = bytearray(0x200 + len(payload))
    data[:2] = b'MZ'
    struct.pack_into('<I', data, 0x3c, 0x80)
    data[0x80:0x84] = b'PE\0\0'
    struct.pack_into('<HHIIIHH', data, 0x84, 0x14c, 1, 0, 0, 0, 0xe0, 0)
    struct.pack_into('<H', data, 0x98, 0x10b)
    struct.pack_into('<I', data, 0xb4, 0x400000)
    struct.pack_into('<8sIIIIIIHHI', data, 0x178, b'.data', len(payload), 0x1000,
                     len(payload), 0x200, 0, 0, 0, 0, flags)
    data[0x200:] = payload
    return data


class ComparisonDataTests(unittest.TestCase):
    def setUp(self):
        build = matching.ROOT / 'build'
        build.mkdir(exist_ok=True)
        temporary = tempfile.TemporaryDirectory(dir=build)
        self.addCleanup(temporary.cleanup)
        self.directory = Path(temporary.name)
        self.metadata = self.directory / 'prepared'
        self.source = self.metadata / 'src'
        self.source.mkdir(parents=True)
        self.original = self.directory / 'original.exe'
        self.payload = struct.pack('<ii', 17, 31)
        self.original.write_bytes(data_pe(self.payload))
        self.symbol = '?items@@3PAUGeneratedRow@@A'
        self.item = {'address': '0x00401000', 'size': 8, 'name': 'items', 'symbol': self.symbol,
                     'header': 'generated.h', 'type': 'GeneratedRow', 'count': 2}
        self.target = {'functions': [{'address': '0x00402000', 'size': 16, 'name': 'Consumer'}],
                       'comparison_data_anchors': [self.item]}
        (self.source / 'generated.h').write_text('struct GeneratedRow { int value; };\nextern GeneratedRow items[2];\n')
        (self.metadata / 'metadata.csv').write_text(
            'address,type,size,symbol\n0x00401000,global,8,' + self.symbol + '\n')

    def generate(self):
        specs = matching.comparison_data_specs(self.metadata, self.target)
        return matching.generate_comparison_data(self.metadata, self.source,
                                                 self.directory / 'output', self.original, specs)

    def test_initialized_data_has_no_code_and_stays_outside_the_export(self):
        sources, objects, records = self.generate()
        self.assertEqual(len(sources), 1)
        self.assertFalse(sources[0].is_relative_to(self.metadata))
        self.assertFalse(objects[0].is_relative_to(self.metadata))
        self.assertEqual(matching.coff_external_symbols(objects[0]), ({self.symbol}, set()))
        self.assertEqual(objects[0].read_bytes()[60:68], self.payload)
        self.assertEqual(struct.unpack_from('<I', objects[0].read_bytes(), 56)[0], 0xc0300040)
        self.assertNotIn(self.payload, sources[0].read_bytes())
        self.assertIn('comparison_data_array* comparison_data_reference_00401000 = &items;', sources[0].read_text())
        self.assertEqual(records[0]['code_bytes'], 0)
        self.assertFalse(records[0]['qualified'])
        self.assertFalse(records[0]['image_execution_permitted'])
        self.assertEqual(self.generate(), (sources, objects, records))
        objects[0].write_bytes(objects[0].read_bytes() + b'x')
        with self.assertRaisesRegex(ValueError, 'changed'):
            self.generate()

    def test_code_uninitialized_and_out_of_bounds_regions_are_rejected(self):
        for flags in (0x60000020, 0x60000040, 0xc0000080):
            with self.subTest(flags=flags), self.assertRaises(ValueError):
                self.original.write_bytes(data_pe(self.payload, flags))
                matching.read_initialized_data(self.original, 0x401000, 8)
        self.original.write_bytes(data_pe(self.payload))
        for address, size in ((0x400fff, 8), (0x401000, 9), (0x401007, 2)):
            with self.subTest(address=address, size=size), self.assertRaises(ValueError):
                matching.read_initialized_data(self.original, address, size)

    def test_only_distinct_typed_global_identities_are_allowed(self):
        for change in ({'type': 'int*'}, {'type': 'void'}, {'count': 0}, {'count': True},
                       {'count': 3}, {'size': 7}, {'name': 'items;'}, {'header': '../generated.h'},
                       {'symbol': '?Other@@3HA'}, {'extra': True}, {'address': '0x00402000'}):
            with self.subTest(change=change), self.assertRaises(ValueError):
                target = copy.deepcopy(self.target)
                target['comparison_data_anchors'][0].update(change)
                matching.comparison_data_specs(self.metadata, target)
        target = copy.deepcopy(self.target)
        target['comparison_data_anchors'].append(copy.deepcopy(self.item))
        with self.assertRaises(ValueError):
            matching.comparison_data_specs(self.metadata, target)
        text = (self.metadata / 'metadata.csv').read_text()
        (self.metadata / 'metadata.csv').write_text(text.replace(',global,', ',function,'))
        with self.assertRaisesRegex(ValueError, 'global CSV'):
            self.generate()

    def test_provider_requires_ignored_output_outside_sources(self):
        specs = matching.comparison_data_specs(self.metadata, self.target)
        with self.assertRaisesRegex(ValueError, 'outside reconstructed source'):
            matching.generate_comparison_data(self.metadata, self.source, self.source, self.original, specs)
        with patch.object(matching.subprocess, 'run', return_value=subprocess.CompletedProcess([], 1)):
            with self.assertRaisesRegex(ValueError, 'ignored output'):
                self.generate()

    def test_header_checks_exact_array_type_count_and_extent(self):
        if shutil.which('c++') is None:
            self.skipTest('A host C++ compiler is needed for declaration checking.')
        sources, _, _ = self.generate()
        for declaration, accepted in (
                ('struct GeneratedRow { int value; }; extern GeneratedRow items[2];', True),
                ('struct GeneratedRow { int value; }; extern GeneratedRow items[3];', False),
                ('struct GeneratedRow { int value; }; extern int items[2];', False),
                ('struct GeneratedRow { long long value; }; extern GeneratedRow items[2];', False),
                ('struct GeneratedRow { int value; };', False)):
            with self.subTest(declaration=declaration):
                (self.source / 'generated.h').write_text(declaration)
                result = subprocess.run([shutil.which('c++'), '-std=c++98', '-D__declspec(x)=',
                                         '-fsyntax-only', '-I' + str(self.source), str(sources[0])],
                                        capture_output=True, text=True)
                self.assertEqual(result.returncode == 0, accepted, result.stderr)

    def test_link_requires_header_reference_and_original_data_bytes(self):
        sources, _, records = self.generate()
        symbol = self.symbol
        binary = self.directory / 'linked.exe'
        binary.write_bytes(data_pe(self.payload))
        def symbols(path):
            if Path(path).name == sources[0].stem + '.obj':
                return set(), {symbol}
            return {symbol}, set()
        with patch.object(matching, 'map_public_names', return_value={0x401000: {symbol}}), \
                patch.object(matching, 'coff_external_symbols', side_effect=symbols):
            matching.verify_comparison_data_links(self.directory, records, binary)
            self.assertEqual(records[0]['recomp'], '0x00401000')
            binary.write_bytes(data_pe(struct.pack('<ii', 18, 31)))
            with self.assertRaisesRegex(ValueError, 'differs from the pinned original'):
                matching.verify_comparison_data_links(self.directory, records, binary)
        for publics in ({}, {0x401000: {symbol}, 0x401004: {symbol}}):
            with self.subTest(publics=publics), \
                    patch.object(matching, 'map_public_names', return_value=publics), \
                    patch.object(matching, 'coff_external_symbols', side_effect=symbols):
                with self.assertRaisesRegex(ValueError, 'one LINK address'):
                    matching.verify_comparison_data_links(self.directory, records, binary)
        with patch.object(matching, 'coff_external_symbols', return_value=({symbol}, set())), \
                patch.object(matching, 'map_public_names', return_value={0x401000: {symbol}}):
            with self.assertRaisesRegex(ValueError, 'exact typed header reference'):
                matching.verify_comparison_data_links(self.directory, records, binary)
