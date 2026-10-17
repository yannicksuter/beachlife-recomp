import hashlib
import importlib.util
from pathlib import Path
import struct
import tempfile
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


if __name__ == '__main__':
    unittest.main()
