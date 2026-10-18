import struct
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from matching_imports import (coff_import_symbols, rewrite_stdcall_import_archive,
                              stdcall_import_library, undecorate_import_archive,
                              vc6_exception_abi_object)


def archive_member(body):
    header = (f'{"fixture/":<16}{0:<12}{0:<6}{0:<6}{0:<8}{len(body):<10}`\n').encode('ascii')
    return header + body + (b'\n' if len(body) % 2 else b'')


def short_import(name='_FixtureCall@8', dll='fixture.dll', mode=2):
    strings = name.encode('ascii') + b'\0' + dll.encode('ascii') + b'\0'
    return struct.pack('<HHHHIIHH', 0, 0xffff, 0, 0x14c, 0, len(strings), 0, mode << 2) + strings


class ImportMetadataTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name) / 'fixture.lib'

    def test_exception_abi_is_only_one_absolute_zero_symbol(self):
        data = vc6_exception_abi_object()
        self.assertEqual(data, vc6_exception_abi_object())
        machine, sections, timestamp, symbols, count, optional, characteristics = struct.unpack_from('<HHIIIHH', data)
        self.assertEqual((machine, sections, timestamp, symbols, count, optional, characteristics),
                         (0x14c, 0, 0, 20, 1, 0, 0))
        name_zero, name_offset, value, section, symbol_type, storage, auxiliaries = struct.unpack_from('<IIIhHBB', data, symbols)
        self.assertEqual((name_zero, value, section, symbol_type, storage, auxiliaries),
                         (0, 0, -1, 0, 2, 0))
        strings = symbols + 18 * count
        size, = struct.unpack_from('<I', data, strings)
        self.assertEqual(strings + size, len(data))
        self.assertEqual(data[strings + name_offset:], b'__except_list\0')
        self.path.write_bytes(data)
        self.assertEqual(coff_import_symbols(self.path), [])

    def test_only_name_mode_changes_and_archive_members_stay_in_place(self):
        prefix = b'!<arch>\n' + archive_member(b'generated descriptor')
        before = prefix + archive_member(short_import())
        self.path.write_bytes(before)
        changes = undecorate_import_archive(self.path, 'fixture.dll', {'FixtureCall': 8})
        after = self.path.read_bytes()
        self.assertEqual(len(after), len(before))
        differences = [index for index, (a, b) in enumerate(zip(before, after)) if a != b]
        self.assertEqual(differences, [len(prefix) + 60 + 18])
        self.assertEqual(before[differences[0]], 8)
        self.assertEqual(after[differences[0]], 12)
        self.assertEqual(changes[0]['symbol'], '_FixtureCall@8')

    def test_rejects_corruption_or_wrong_identity_without_partial_write(self):
        valid = b'!<arch>\n' + archive_member(short_import())
        controls = [valid[:7], valid[:50], valid[:-2],
                    b'!<arch>\n' + archive_member(short_import(dll='other.dll')),
                    b'!<arch>\n' + archive_member(short_import(name='_FixtureCall@4')),
                    b'!<arch>\n' + archive_member(short_import(mode=3)),
                    valid + archive_member(short_import()),
                    valid + archive_member(short_import(name='_Unexpected@0'))]
        for before in controls:
            with self.subTest(size=len(before)):
                self.path.write_bytes(before)
                with self.assertRaises(ValueError):
                    undecorate_import_archive(self.path, 'fixture.dll', {'FixtureCall': 8})
                self.assertEqual(self.path.read_bytes(), before)

    def test_decorated_exports_keep_the_complete_coff_name(self):
        prefix = b'!<arch>\n' + archive_member(b'generated descriptor')
        before = prefix + archive_member(short_import())
        self.path.write_bytes(before)
        changes = rewrite_stdcall_import_archive(
            self.path, 'fixture.dll', {'FixtureCall': 8}, 'decorated')
        after = self.path.read_bytes()
        differences = [i for i, (a, b) in enumerate(zip(before, after)) if a != b]
        self.assertEqual(len(after), len(before))
        self.assertEqual(differences, [len(prefix) + 60 + 18])
        self.assertEqual(after[differences[0]], 1 << 2)
        self.assertEqual(changes[0]['symbol'], '_FixtureCall@8')
        self.assertIn(b'_FixtureCall@8\0fixture.dll\0', after)

    def test_import_policy_fails_before_files_or_librarian_calls(self):
        folder = self.path.parent / 'not-created'
        bad = [('../fixture.dll', {'FixtureCall': 8}, 'decorated'),
               ('fixture.dll', {}, 'decorated'),
               ('fixture.dll', [], 'decorated'),
               ('fixture.dll', {'FixtureCall': True}, 'decorated'),
               ('fixture.dll', {'FixtureCall': 3}, 'decorated'),
               ('fixture.dll', {'FixtureCall': 65536}, 'decorated'),
               ('fixture.dll', {'FixtureCall': -4}, 'decorated'),
               ('fixture.dll', {'FixtureCall@8': 8}, 'decorated'),
               ('fixture.dll', {'FixtureCall\n': 8}, 'decorated'),
               ('fixture.dll', {'FixtureCall': 8}, 'ordinal'),
               ('fixture.dll', {'FixtureCall': 8}, [])]
        for dll, symbols, mode in bad:
            with self.subTest(dll=dll, symbols=symbols, mode=mode):
                # No tool object exists: validation must finish before it is used.
                with self.assertRaises(ValueError):
                    stdcall_import_library(None, folder, dll, symbols, mode)
                self.assertFalse(folder.exists())

    def test_reads_undefined_coff_imports_not_incidental_strings(self):
        name = b'__imp__FixtureCall@8\0'
        header = struct.pack('<HHIIIHH', 0x14c, 0, 0, 20, 2, 0, 0)
        external = struct.pack('<IIIhHBB', 0, 4, 0, 0, 0, 2, 0)
        defined = struct.pack('<8sIhHBB', b'local\0\0\0', 0, 1, 0, 2, 0)
        strings = struct.pack('<I', 4 + len(name)) + name
        valid = header + external + defined + strings + b'__imp__Unreferenced@4\0'
        self.path.write_bytes(valid)
        self.assertEqual(coff_import_symbols(self.path), ['__imp__FixtureCall@8'])
        controls = [valid[:19], valid[:40], valid[:60]]
        offset = bytearray(valid)
        struct.pack_into('<I', offset, 24, len(strings) + 4)
        controls.append(bytes(offset))
        auxiliary = bytearray(valid)
        auxiliary[37] = 2
        controls.append(bytes(auxiliary))
        for body in controls:
            self.path.write_bytes(body)
            with self.assertRaises(ValueError):
                coff_import_symbols(self.path)


if __name__ == '__main__':
    unittest.main()
