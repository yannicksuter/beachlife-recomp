import contextlib
import copy
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from types import SimpleNamespace
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
spec = importlib.util.spec_from_file_location('matching', Path(__file__).resolve().parents[1] / 'tools/matching.py')
matching = importlib.util.module_from_spec(spec)
spec.loader.exec_module(matching)

CONSTRUCTOR = '??0GeneratedItem@@QAE@UGeneratedRect@@PBD@Z'
DESTRUCTOR = '??1GeneratedBase@@UAE@XZ'


def coff_object(symbols):
    """A synthetic x86 COFF object: (name, section) pairs, section 0 undefined."""
    names, strings, entries = [], b'', b''
    for name, section in symbols:
        encoded = name.encode('ascii')
        if len(encoded) <= 8:
            field = encoded.ljust(8, b'\0')
        else:
            field = struct.pack('<II', 0, 4 + len(strings))
            strings += encoded + b'\0'
        entries += field + struct.pack('<IhHBB', 0, section, 0x20, 2, 0)
        names.append(name)
    code = b'\xc3'
    raw = 20 + 40
    header = struct.pack('<HHIIIHH', 0x14c, 1, 0, raw + len(code), len(symbols), 0, 0)
    section = struct.pack('<8sIIIIIIHHI', b'.text', 0, 0, len(code), raw, 0, 0, 0, 0, 0x60500020)
    return header + section + code + entries + struct.pack('<I', 4 + len(strings)) + strings


class LifetimeIdentityTests(unittest.TestCase):
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
            'struct GeneratedRect { int left, top, right, bottom; };\n'
            'struct GeneratedBase\n{\n    explicit GeneratedBase(int mode);\n    virtual ~GeneratedBase();\n};\n'
            'struct GeneratedItem : GeneratedBase\n{\n'
            '    GeneratedItem(GeneratedRect initial, const char* name);\n    void Configure(int value);\n};\n')
        self.identity = {'address': '0x00401100', 'size': 48, 'name': 'GeneratedItem::GeneratedItem',
                         'symbol': CONSTRUCTOR, 'header': 'generated.h'}
        self.destructor = {'address': '0x00401200', 'size': 16, 'name': 'GeneratedBase::~GeneratedBase',
                           'symbol': DESTRUCTOR, 'header': 'generated.h'}
        self.target = {'functions': [{'name': 'GeneratedOwner::Parse', 'address': '0x00401000', 'size': 64}],
                       'comparison_lifetime_anchors': [self.identity, self.destructor]}
        (self.metadata / 'metadata.csv').write_text(
            'address,type,size,symbol\n'
            '0x00401000,function,64,?Parse@GeneratedOwner@@QAEXXZ\n'
            f'0x00401100,function,48,{CONSTRUCTOR}\n'
            f'0x00401200,function,16,{DESTRUCTOR}\n')

    def test_only_exact_constructor_and_destructor_decorations_are_lifetime_identities(self):
        kind = matching.lifetime_identity_kind
        self.assertEqual(kind('GeneratedItem::GeneratedItem', CONSTRUCTOR), 'constructor')
        self.assertEqual(kind('GeneratedItem::GeneratedItem', '??0GeneratedItem@@QAE@XZ'), 'constructor')
        self.assertEqual(kind('GeneratedBase::GeneratedBase', '??0GeneratedBase@@QAE@H@Z'), 'constructor')
        self.assertEqual(kind('GeneratedBase::~GeneratedBase', DESTRUCTOR), 'destructor')
        self.assertEqual(kind('GeneratedBase::~GeneratedBase', '??1GeneratedBase@@QAE@XZ'), 'destructor')
        for name, symbol in (
                ('GeneratedItem::GeneratedItem', '??0GeneratedBase@@QAE@XZ'),       # other class
                ('GeneratedItem::GeneratedItem', '??1GeneratedItem@@QAE@XZ'),       # destructor symbol
                ('GeneratedBase::~GeneratedBase', '??0GeneratedBase@@QAE@XZ'),      # constructor symbol
                ('GeneratedItem::Configure', '?Configure@GeneratedItem@@QAEXH@Z'),  # ordinary member
                ('GeneratedItem::GeneratedItem', '??0GeneratedItem@@UAE@XZ'),       # virtual constructor
                ('GeneratedBase::~GeneratedBase', '??1GeneratedBase@@UAE@H@Z'),     # destructor parameters
                ("GeneratedBase::`scalar deleting destructor'", '??_GGeneratedBase@@UAEPAXI@Z'),  # COMDAT
                ('GeneratedBase::~GeneratedBase', '??_GGeneratedBase@@UAEPAXI@Z'),
                ('GeneratedItem::GeneratedItem', '??0GeneratedItem@@QAE@'),         # truncated
                ('GeneratedItem::GeneratedItem', '??0GeneratedItem@@QAE@H@Z,x'),    # CSV injection
                ('Outer::GeneratedItem::GeneratedItem', '??0GeneratedItem@Outer@@QAE@XZ'),
                ('int::int', '??0int@@QAE@XZ'), (None, CONSTRUCTOR)):
            with self.subTest(name=name, symbol=symbol):
                self.assertIsNone(kind(name, symbol))

    def test_specs_need_one_exact_csv_identity_and_a_free_range(self):
        self.assertEqual(matching.comparison_lifetime_specs(self.metadata, self.target),
                         [self.identity, self.destructor])
        self.assertEqual(matching.comparison_lifetime_specs(self.metadata, {'functions': []}), [])
        anchor = {'address': '0x00401300', 'size': 8, 'name': 'GeneratedItem::Configure',
                  'symbol': '?Configure@GeneratedItem@@QAEXH@Z'}
        with (self.metadata / 'metadata.csv').open('a') as stream:
            stream.write('0x00401300,function,8,?Configure@GeneratedItem@@QAEXH@Z\n')
        for changes, message in (({'address': '0x00401010'}, 'distinct'),          # inside a credited body
                                 ({'address': '0x00401300', 'size': 8}, 'distinct'),  # an anchor's range
                                 ({'name': 'GeneratedOwner::Parse'}, 'Unsupported'),
                                 ({'size': 47}, 'CSV identity'),
                                 ({'symbol': '??0GeneratedItem@@QAE@XZ'}, 'CSV identity'),
                                 ({'header': '../generated.h'}, 'Unsupported'),
                                 ({'size': True}, 'Unsupported'),
                                 ({'extra': 1}, 'Unsupported')):
            with self.subTest(changes=changes):
                target = copy.deepcopy(self.target)
                target['comparison_lifetime_anchors'][0].update(changes)
                target['comparison_anchors'] = [anchor]
                with self.assertRaisesRegex(ValueError, message):
                    matching.comparison_lifetime_specs(self.metadata, target)
        target = copy.deepcopy(self.target)
        target['comparison_lifetime_anchors'].append(copy.deepcopy(self.identity))
        with self.assertRaisesRegex(ValueError, 'distinct'):
            matching.comparison_lifetime_specs(self.metadata, target)
        (self.metadata / 'metadata.csv').write_text(
            'address,type,size,symbol\n'
            f'0x00401100,global,48,{CONSTRUCTOR}\n0x00401200,function,16,{DESTRUCTOR}\n')
        with self.assertRaisesRegex(ValueError, 'CSV identity'):
            matching.comparison_lifetime_specs(self.metadata, self.target)

    def test_object_defines_only_the_decorated_symbol_as_a_trap(self):
        body = matching.lifetime_identity_object(0x00401100, CONSTRUCTOR)
        self.assertEqual(body, matching.lifetime_identity_object(0x00401100, CONSTRUCTOR))
        self.assertNotEqual(body, matching.lifetime_identity_object(0x00401200, CONSTRUCTOR))
        path = self.directory / 'identity.obj'
        path.write_bytes(body)
        matching.verify_comparison_anchor_object(path, CONSTRUCTOR)
        self.assertEqual(matching.coff_external_symbols(path), ({CONSTRUCTOR}, set()))
        size, raw = struct.unpack_from('<II', body, 20 + 16)
        # mov eax, <original address>; int3 x3: no return, no call, no vptr store.
        self.assertEqual(body[raw:raw + size], b'\xb8\x00\x11\x40\x00\xcc\xcc\xcc')
        self.assertEqual(struct.unpack_from('<I', body, 20 + 36)[0] & 0x1000, 0)  # not a COMDAT

    def test_generation_writes_ignored_unqualified_objects_only(self):
        specs = matching.comparison_lifetime_specs(self.metadata, self.target)
        paths, records = matching.generate_lifetime_identities(
            self.metadata, self.source, self.directory / 'output', specs)
        self.assertEqual([path.name for path in paths], ['lifetime_00401100.obj', 'lifetime_00401200.obj'])
        self.assertTrue(all(path.parent.name == 'comparison-lifetime' for path in paths))
        self.assertNotIn(paths[0], list(self.metadata.rglob('*')))
        self.assertEqual([record['kind'] for record in records], ['constructor', 'destructor'])
        self.assertEqual(records[0]['code_bytes'], 8)
        self.assertEqual(records[0]['sha256'], matching.digest(paths[0]))
        self.assertTrue(all(not record['qualified'] and not record['image_execution_permitted']
                            for record in records))
        self.assertEqual(matching.generate_lifetime_identities(
            self.metadata, self.source, self.directory / 'output', specs), (paths, records))
        paths[0].write_bytes(b'changed')
        with self.assertRaisesRegex(ValueError, 'object changed'):
            matching.generate_lifetime_identities(self.metadata, self.source, self.directory / 'output', specs)
        paths[0].with_name('unexpected.obj').write_bytes(b'')
        with self.assertRaisesRegex(ValueError, 'Unexpected files'):
            matching.generate_lifetime_identities(self.metadata, self.source, self.directory / 'output', specs)
        with self.assertRaisesRegex(ValueError, 'outside reconstructed source'):
            matching.generate_lifetime_identities(self.metadata, self.source, self.source, specs)
        self.assertEqual(matching.generate_lifetime_identities(self.metadata, self.source, self.directory, []),
                         ([], []))

    def test_prepared_bodies_and_undeclared_members_are_rejected(self):
        specs = matching.comparison_lifetime_specs(self.metadata, self.target)
        (self.source / 'existing.cpp').write_text(
            '#include "generated.h"\n\n// FUNCTION: BEACHLIFE 0x00401200\n'
            'GeneratedBase::~GeneratedBase()\n{\n}\n')
        with self.assertRaisesRegex(ValueError, 'CSV identity instead'):
            matching.generate_lifetime_identities(self.metadata, self.source, self.directory / 'body', specs)
        (self.source / 'existing.cpp').unlink()
        for header, message in (
                ('struct GeneratedItem { void Configure(int value); };\n'
                 'struct GeneratedBase { virtual ~GeneratedBase(); };\n', 'does not declare'),
                ('struct GeneratedItem { GeneratedItem(GeneratedRect, const char*); };\n'
                 'struct GeneratedBase { GeneratedBase(int); };\n', 'does not declare'),
                ('struct GeneratedItemList { GeneratedItem(); ~GeneratedBase(); };\n', 'does not declare')):
            with self.subTest(header=header):
                (self.source / 'generated.h').write_text(header)
                with self.assertRaisesRegex(ValueError, message):
                    matching.generate_lifetime_identities(
                        self.metadata, self.source, self.directory / 'declared', specs)
        target = copy.deepcopy(self.target)
        target['comparison_lifetime_anchors'][0]['header'] = 'missing.h'
        with self.assertRaisesRegex(ValueError, 'actual source input'):
            matching.generate_lifetime_identities(
                self.metadata, self.source, self.directory / 'missing',
                matching.comparison_lifetime_specs(self.metadata, target))
        self.assertFalse((self.directory / 'body').exists())

    def test_fragments_share_identities_and_imports_once(self):
        fragments = self.metadata / 'targets/beachlife-uk-1.1.d'
        fragments.mkdir(parents=True)
        for address in ('00402000', '00403000'):
            (fragments / (address + '.json')).write_text(json.dumps({
                'functions': [{'name': 'Parse' + address, 'address': '0x' + address, 'size': 4}],
                'comparison_lifetime_anchors': [self.identity],
                'runtime_cdecl_imports': ['modf', 'modf']}))
        target = {'functions': []}
        matching.merge_target_fragments(self.metadata, target)
        self.assertEqual(target['comparison_lifetime_anchors'], [self.identity])
        self.assertEqual(target['runtime_cdecl_imports'], ['modf'])
        (fragments / '00403000.json').write_text(json.dumps({
            'comparison_lifetime_anchors': [self.identity | {'size': 47}]}))
        target = {'functions': []}
        matching.merge_target_fragments(self.metadata, target)
        with self.assertRaisesRegex(ValueError, 'distinct'):
            matching.comparison_lifetime_specs(self.metadata, target)

    def test_a_matched_body_supersedes_its_lifetime_identity(self):
        target = copy.deepcopy(self.target)
        target['functions'].append({'name': 'GeneratedItem::GeneratedItem', 'address': '0x00401100', 'size': 48})
        matching.drop_superseded_anchors(self.metadata, target)
        self.assertEqual(target['comparison_lifetime_anchors'], [self.destructor])
        target = copy.deepcopy(self.target)
        target['functions'].append({'name': 'GeneratedItem::GeneratedItem', 'address': '0x00401100', 'size': 40})
        matching.drop_superseded_anchors(self.metadata, target)
        self.assertEqual(len(target['comparison_lifetime_anchors']), 2)

    def link_folder(self, caller_symbols, owner='lifetime_00401100.obj', linked=True):
        folder = self.directory / ('link_' + str(len(list(self.directory.glob('link_*')))))
        folder.mkdir()
        (folder / 'caller.obj').write_bytes(coff_object(caller_symbols))
        (folder / 'other.obj').write_bytes(coff_object([('_fixture_entry', 1)]))
        stub = 'Z:\\work\\game\\comparison-lifetime\\lifetime_00401100.obj'
        (folder / 'link.rsp').write_text('\n'.join(
            f'"{argument}"' for argument in ['/nologo', '/fixed:no', 'msvcrt-import.lib',
                                             *([stub] if linked else []), 'caller.obj', 'other.obj']) + '\n')
        (folder / 'BeachLife.map').write_text(
            '  Address         Publics by Value              Rva+Base     Lib:Object\n\n'
            f' 0001:00000000       {CONSTRUCTOR}  00401000 f   {owner}\n'
            ' 0001:00000010       ?Parse@GeneratedOwner@@QAEXXZ 00401010 f   caller.obj\n')
        record = {'symbol': CONSTRUCTOR, 'object': 'build/game/comparison-lifetime/lifetime_00401100.obj'}
        return folder, record

    def test_link_needs_a_caller_reference_and_only_the_identity_definition(self):
        folder, record = self.link_folder([('?Parse@GeneratedOwner@@QAEXXZ', 1), (CONSTRUCTOR, 0)])
        matching.verify_lifetime_identity_links(folder, [record], folder / 'BeachLife.map')
        matching.verify_lifetime_identity_links(folder, [], folder / 'absent.map')
        for symbols, owner, linked, message in (
                ([('?Parse@GeneratedOwner@@QAEXXZ', 1)], 'lifetime_00401100.obj', True, 'No compiled object'),
                ([(CONSTRUCTOR, 0), (CONSTRUCTOR + 'x', 1)], 'caller.obj', True, 'not defined only'),
                ([(CONSTRUCTOR, 0)], 'lifetime_00401100.obj', False, 'linked exactly once')):
            with self.subTest(message=message):
                folder, record = self.link_folder(symbols, owner, linked)
                with self.assertRaisesRegex(ValueError, message):
                    matching.verify_lifetime_identity_links(folder, [record], folder / 'BeachLife.map')
        folder, record = self.link_folder([(CONSTRUCTOR, 0)])
        (folder / 'other.obj').write_bytes(coff_object([(CONSTRUCTOR, 1)]))
        with self.assertRaisesRegex(ValueError, 'not defined only'):
            matching.verify_lifetime_identity_links(folder, [record], folder / 'BeachLife.map')

    def test_runtime_cdecl_imports_come_only_from_the_original_import_table(self):
        self.assertEqual(matching.game_runtime_cdecl_imports({}), [])
        self.assertEqual(matching.game_runtime_cdecl_imports({'runtime_cdecl_imports': ['modf', '_CIfmod']}),
                         ['_CIfmod', 'modf'])
        for value in ('modf', ['modf', 'modf'], ['_modf@12'], ['??2@YAPAXI@Z'], [''], ['mo df'], [1]):
            with self.subTest(value=value):
                with self.assertRaisesRegex(ValueError, 'runtime_cdecl_imports'):
                    matching.game_runtime_cdecl_imports({'runtime_cdecl_imports': value})
        imports = {'MSVCRT.dll': {'modf': '0x4f3134', 'floor': '0x4f3118'}, 'KERNEL32.dll': {'Sleep': '0x4f3000'}}
        with patch.object(matching, 'pe_imports', return_value=imports):
            self.assertEqual(matching.verify_runtime_cdecl_imports(Path('original.exe'), ['modf']),
                             {'modf': '0x4f3134'})
            with self.assertRaisesRegex(ValueError, 'does not import Sleep'):
                matching.verify_runtime_cdecl_imports(Path('original.exe'), ['Sleep'])
        with patch.object(matching, 'pe_imports', side_effect=AssertionError('unexpected read')):
            self.assertEqual(matching.verify_runtime_cdecl_imports(Path('original.exe'), []), {})
        definition = 'LIBRARY MSVCRT.dll\nEXPORTS\n    fopen\n    floor\n'
        self.assertEqual(matching.runtime_definition(definition, ['floor', 'modf']),
                         definition + '    modf\n')
        self.assertEqual(matching.runtime_definition(definition, ['floor']), definition)
        with self.assertRaisesRegex(ValueError, 'EXPORTS'):
            matching.runtime_definition('LIBRARY MSVCRT.dll\n', ['modf'])


class LifetimeGameRunTests(unittest.TestCase):
    """game() links the identity objects, records them and extends only the import library."""

    def setUp(self):
        build = matching.ROOT / 'build'
        build.mkdir(exist_ok=True)
        temporary = tempfile.TemporaryDirectory(dir=build)
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name).resolve()
        self.source = self.root / 'source'
        self.source.mkdir()
        (self.source / 'generated.h').write_text(
            'struct GeneratedRect { int left, top, right, bottom; };\n'
            'struct GeneratedItem\n{\n    GeneratedItem(GeneratedRect initial, const char* name);\n};\n')
        (self.source / 'parse.cpp').write_text('// FUNCTION: BEACHLIFE 0x00401000\nvoid Parse() {}\n')
        self.metadata = self.root / 'metadata'
        for name in ('fixtures', 'support', 'targets'):
            (self.metadata / name).mkdir(parents=True)
        (self.metadata / 'fixtures/game.cpp').write_text('void fixture_entry() {}\n')
        (self.metadata / 'support/operator_delete.cpp').write_text('// generated fixture\n')
        (self.metadata / 'msvcrt.def').write_text('LIBRARY MSVCRT.dll\nEXPORTS\n    fopen\n')
        (self.metadata / 'metadata.csv').write_text(
            'address,type,size,symbol\n0x00401000,function,4,?Parse@@YAXXZ\n'
            f'0x00401100,function,48,{CONSTRUCTOR}\n')
        self.original = self.root / 'generated.bin'
        data = b'generated identity only; no executable code'
        self.original.write_bytes(data)
        self.target = {'id': 'generated', 'size': len(data), 'sha1': hashlib.sha1(data).hexdigest(),
                       'sha256': hashlib.sha256(data).hexdigest(),
                       'functions': [{'name': 'Parse', 'address': '0x00401000', 'size': 4}],
                       'comparison_lifetime_anchors': [
                           {'address': '0x00401100', 'size': 48, 'name': 'GeneratedItem::GeneratedItem',
                            'symbol': CONSTRUCTOR, 'header': 'generated.h'}],
                       'runtime_cdecl_imports': ['modf']}
        (self.metadata / 'targets/beachlife-uk-1.1.json').write_text(json.dumps(self.target))
        stlport = self.root / 'vendor'
        header = stlport / 'stlport/vector'
        header.parent.mkdir(parents=True)
        header.write_text('// synthetic vendor input\n')
        pin = self.root / 'stlport.json'
        pin.write_text(json.dumps({'header_files': 1, 'headers_sha256': hashlib.sha256(
            f'vector\0{matching.digest(header)}\n'.encode()).hexdigest()}))
        stlport_patch = patch.object(matching, 'STLPORT_PIN', pin)
        stlport_patch.start()
        self.addCleanup(stlport_patch.stop)
        sdk = self.root / 'compiler/Include/FIXTURE.H'
        sdk.parent.mkdir(parents=True)
        sdk.write_bytes(b'// generated SDK declaration fixture\n')
        (self.metadata / 'win32-imports.json').write_text(json.dumps({
            'schema': 1, 'architecture': 'x86', 'calling_convention': 'stdcall',
            'headers_sha256': {'FIXTURE.H': matching.digest(sdk)}}))
        self.args = SimpleNamespace(source=self.source, original=self.original, output=self.root / 'output',
                                    reports=self.root / 'reports', stlport=stlport)
        self.exports, self.link = None, None
        self.tool = SimpleNamespace(decomp_root=self.metadata, output=self.args.output,
                                    compiler=self.root / 'compiler/Bin', reccmp='generated-comparator',
                                    native=str, runtime_library=self.runtime_library,
                                    build=self.build, run=self.compare)

    def runtime_library(self, folder, exports=()):
        self.exports = list(exports)
        return 'generated-import.lib'

    def build(self, folder, sources, name, extra_link=(), extra_compile=(), source_flags=None,
              source_includes=None):
        self.link = list(extra_link)
        stub = next(argument for argument in extra_link if argument.endswith('.obj'))
        (folder / 'parse.obj').write_bytes(coff_object([('?Parse@@YAXXZ', 1), (CONSTRUCTOR, 0)]))
        (folder / 'link.rsp').write_text(''.join(f'"{argument}"\n' for argument in [*extra_link, 'parse.obj']))
        (folder / 'generated.map').write_text(
            f' 0001:00000000       {CONSTRUCTOR}  00401000 f   {Path(stub).name}\n'
            ' 0001:00000010       ?Parse@@YAXXZ  00401010 f   parse.obj\n')
        return folder / 'generated.exe', folder / 'generated.pdb'

    def compare(self, arguments, folder):
        report = Path(arguments[arguments.index('--json') + 1])
        report.write_text(json.dumps({'data': [{'address': '0x00401000', 'recomp': '0x00401010',
                                                'matching': 1.0}]}))
        return ''

    def run_game(self, imports):
        with (patch.object(matching, 'read_va', return_value=b'code'),
              patch.object(matching, 'pe_imports', side_effect=imports),
              contextlib.redirect_stdout(io.StringIO())):
            matching.game(self.tool, self.args)
        return json.loads((self.args.reports / 'result.json').read_text())

    def test_identity_objects_reach_link_and_result_without_credit(self):
        original = {'MSVCRT.dll': {'modf': '0x4f3134', 'fopen': '0x4f317c'}}
        rebuilt = {'MSVCRT.dll': {'fopen': '0x4050a0'}}
        result = self.run_game(lambda path: original if path == self.original else rebuilt)
        stub = self.args.output / 'game/comparison-lifetime/lifetime_00401100.obj'
        self.assertEqual(self.link, ['/fixed:no', 'generated-import.lib', str(stub)])
        self.assertEqual(self.exports, ['modf'])
        self.assertEqual([item['name'] for item in result['functions']], ['Parse'])
        record, = result['comparison_lifetime_anchors']
        self.assertEqual((record['address'], record['kind'], record['qualified'],
                          record['image_execution_permitted']), ('0x00401100', 'constructor', False, False))
        self.assertFalse(result['image_execution_permitted'])
        self.assertEqual(result['runtime_cdecl_imports'], {
            'original': {'modf': '0x4f3134'}, 'compiled': {'linked': {}, 'omitted': ['modf']}})
        self.assertIn(self.root.relative_to(matching.ROOT).as_posix() +
                      '/output/game/comparison-lifetime/lifetime_00401100.obj', result['input_sha256'])
        project = json.loads((self.args.output / 'game/reccmp-project.yml').read_text())
        self.assertEqual(project['targets']['BEACHLIFE']['report']['ignore-functions'], ['fixture_entry', CONSTRUCTOR])

    def test_an_anchor_never_hides_a_credited_overload_in_the_report(self):
        (self.source / 'generated.h').write_text(
            (self.source / 'generated.h').read_text() +
            'void Parse(const unsigned short* text);\nvoid Other(int value);\n')
        anchors = [{'address': '0x00401200', 'size': 16, 'name': 'Parse', 'symbol': '?Parse@@YAXPBG@Z',
                    'header': 'generated.h', 'kind': 'void-function', 'return_type': 'void',
                    'parameters': ['const unsigned short*']},
                   {'address': '0x00401300', 'size': 16, 'name': 'Other', 'symbol': '?Other@@YAXH@Z',
                    'header': 'generated.h', 'kind': 'void-function', 'return_type': 'void', 'parameters': ['int']}]
        self.target['functions'][0]['name'] = 'Parse(const char*)'
        self.target['comparison_anchors'] = anchors
        (self.metadata / 'targets/beachlife-uk-1.1.json').write_text(json.dumps(self.target))
        with (self.metadata / 'metadata.csv').open('a') as stream:
            stream.write('0x00401200,function,16,?Parse@@YAXPBG@Z\n0x00401300,function,16,?Other@@YAXH@Z\n')
        with patch.object(self.tool, 'build', self.build_without_anchor_check):
            self.run_game(lambda path: {'MSVCRT.dll': {'modf': '0x4f3134'}})
        project = json.loads((self.args.output / 'game/reccmp-project.yml').read_text())
        self.assertEqual(project['targets']['BEACHLIFE']['report']['ignore-functions'],
                         ['fixture_entry', 'Other', CONSTRUCTOR])

    def build_without_anchor_check(self, folder, sources, name, extra_link=(), extra_compile=(), source_flags=None,
                                   source_includes=None, anchor_symbols=None):
        return self.build(folder, sources, name, extra_link, extra_compile, source_flags, source_includes)

    def test_rebuilt_import_outside_the_original_table_fails(self):
        original = {'MSVCRT.dll': {'modf': '0x4f3134'}}
        rebuilt = {'MSVCRT.dll': {'modf': '0x4050a0', 'cosh': '0x4050a4'}}
        with self.assertRaisesRegex(ValueError, 'unverified identity'):
            self.run_game(lambda path: original if path == self.original else rebuilt)
        with self.assertRaisesRegex(ValueError, 'does not import modf'):
            self.run_game(lambda path: {'MSVCRT.dll': {}})


if __name__ == '__main__':
    unittest.main()
