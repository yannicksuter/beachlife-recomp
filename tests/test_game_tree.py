"""Generated Mach-O objects exercise trap discovery without a macOS SDK."""
import argparse
import contextlib
import io
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import game_tree


@unittest.skipUnless(all(shutil.which(tool) for tool in ('clang++', 'llvm-nm', 'llvm-objdump',
                                                       'llvm-cxxfilt')),
                     'Mach-O fixtures require Clang and LLVM inspection tools')
class MachOTraps(unittest.TestCase):
    def test_both_architectures(self):
        for arch in ('arm64', 'x86_64'):
            with self.subTest(arch=arch), tempfile.TemporaryDirectory(prefix='boot symbols ') as temp:
                root = Path(temp)
                src, compat = root/'src', root/'compat'
                src.mkdir()
                compat.mkdir()
                (compat/'msvc_prelude.h').write_text('')
                (compat/'cstdint').write_text('namespace std { typedef unsigned int uint32_t; }')
                (src/'missing.h').write_text('''
extern "C" void MissingHostFunction();
extern "C" void MissingCompat() __asm__("beachlife_compat_MissingCompat");
extern "C" void *malloc(unsigned long);
extern int missing_data;
void MissingGameFunction(int);
class NeedsBody {
public:
    virtual ~NeedsBody();
    virtual int tick(int);
    virtual int ready();
};
''')
                (src/'caller.cpp').write_text('''
#include "missing.h"
int NeedsBody::ready() { return 1; }
void *run() {
    MissingHostFunction(); MissingCompat(); MissingGameFunction(missing_data);
    NeedsBody object;
    object.tick(7);
    return malloc(8);
}
''')
                target = f'--target={arch}-apple-macos15'
                obj = root/'caller.o'
                subprocess.run(['clang++', target, '-c', str(src/'caller.cpp'), '-o', str(obj)], check=True)
                calls = game_tree.called([obj], 'Darwin')
                self.assertIn('_MissingHostFunction', calls)
                self.assertIn('__Z19MissingGameFunctioni', calls)
                self.assertNotIn('_missing_data', calls)
                (root/'objects.txt').write_text(str(obj)+'\n')
                stub = root/'runtime.tbd'
                stub.write_text('''--- !tapi-tbd
tbd-version: 4
targets: [ arm64-macos, x86_64-macos ]
install-name: '/usr/lib/libSystem.B.dylib'
exports:
  - targets: [ arm64-macos, x86_64-macos ]
    symbols: [ _malloc, __Unwind_Resume, ___gxx_personality_v0 ]
...
''')
                exports = game_tree.symbols(['llvm-nm', '-P', '--defined-only', f'--arch={arch}'], [stub])
                self.assertEqual(exports, {'_malloc', '__Unwind_Resume', '___gxx_personality_v0'})
                sdk = root/'sdk'
                libdir = sdk/'usr/lib'
                libdir.mkdir(parents=True)
                shutil.copy2(stub, libdir/'libSystem.tbd')
                shutil.copy2(stub, libdir/'libc++.tbd')
                original_output = subprocess.check_output
                def inspect(command, **kwargs):
                    if command[0] == 'clang++' and command[-1] == '--print-libgcc-file-name':
                        self.assertIn(target, command)
                        return str(stub) + '\n'
                    return original_output(command, **kwargs)
                args = argparse.Namespace(objects=root/'objects.txt', nm='llvm-nm', host=[],
                    compiler='clang++', target_system='Darwin', target_arch=arch, sysroot=str(sdk),
                    compiler_arg=[target], readelf='readelf', objdump='llvm-objdump',
                    tree=root, demangler='llvm-cxxfilt', inventory=None, strict=False, require_complete=False,
                    output=root/'traps.cpp', json=root/'traps.json')
                with patch.object(game_tree, 'COMPAT', compat), \
                     patch.object(game_tree.subprocess, 'check_output', side_effect=inspect), \
                     contextlib.redirect_stdout(io.StringIO()):
                    self.assertEqual(game_tree.traps(args), 0)
                import json
                traps = json.loads(args.json.read_text())
                self.assertEqual({entry['name'] for entry in traps['host_services']},
                                 {'MissingHostFunction', 'MissingCompat'})
                self.assertEqual(traps['unresolved'], [{'symbol': '_missing_data', 'name': 'missing_data'}])
                self.assertEqual({entry['name'] for entry in traps['functions']},
                                 {'MissingGameFunction(int)', 'NeedsBody::~NeedsBody', 'NeedsBody::tick'})
                generated = root/'traps.o'
                subprocess.run(['clang++', target, '-std=c++20', f'-I{src}', f'-I{compat}',
                                '-c', str(args.output), '-o', str(generated)], check=True)
                definitions = game_tree.symbols(['llvm-nm', '-P', '--defined-only'], [generated])
                self.assertIn('__Z19MissingGameFunctioni', definitions)
                self.assertIn('_MissingHostFunction', definitions)
                self.assertIn('beachlife_compat_MissingCompat', definitions)
                self.assertIn('__ZTV9NeedsBody', definitions)
                self.assertNotIn('__ZN9NeedsBody5readyEv', definitions)
                args.require_complete = True
                with patch.object(game_tree, 'COMPAT', compat), \
                     patch.object(game_tree.subprocess, 'check_output', side_effect=inspect), \
                     contextlib.redirect_stdout(io.StringIO()), \
                     contextlib.redirect_stderr(io.StringIO()) as errors:
                    self.assertEqual(game_tree.traps(args), 1)
                self.assertIn('Release game requires zero', errors.getvalue())
                self.assertFalse(args.output.exists())
                self.assertEqual(json.loads(args.json.read_text()), traps)
                # Resolving the actual missing bodies and data allows the same
                # release invocation to succeed, with no generated trap entries.
                (src/'complete.cpp').write_text('''
#include "missing.h"
void MissingHostFunction() {}
void MissingCompat() {}
int missing_data = 0;
void MissingGameFunction(int) {}
NeedsBody::~NeedsBody() {}
int NeedsBody::tick(int value) { return value; }
''')
                complete = root/'complete.o'
                subprocess.run(['clang++', target, '-c', str(src/'complete.cpp'), '-o', str(complete)], check=True)
                args.host = [str(complete)]
                with patch.object(game_tree, 'COMPAT', compat), \
                     patch.object(game_tree.subprocess, 'check_output', side_effect=inspect), \
                     contextlib.redirect_stdout(io.StringIO()):
                    self.assertEqual(game_tree.traps(args), 0)
                self.assertEqual(json.loads(args.json.read_text()),
                                 {'functions': [], 'host_services': [], 'unresolved': []})

    def test_failed_inspection_is_fatal(self):
        with tempfile.TemporaryDirectory() as temp:
            obj = Path(temp)/'broken.o'
            obj.write_text('not an object')
            with self.assertRaises(subprocess.CalledProcessError):
                game_tree.called([obj], 'Darwin')

    def test_ast_errors_are_fatal(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root/'src').mkdir()
            (root/'src/broken.h').write_text('class Broken {\nInvalidType member;\n};')
            with self.assertRaises(subprocess.CalledProcessError):
                game_tree.class_traps(root, 'Broken', set())


if __name__ == '__main__':
    unittest.main()
