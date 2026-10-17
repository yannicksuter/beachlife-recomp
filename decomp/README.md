# Matching reconstruction

This directory holds the faithful reconstruction and matching configuration
inside the Beach Life project. New launcher, data and platform code remains in
`../src/`. Matching uses the original x86 toolchain; native builds use modern
C++20. Neither build runs the retail executable.

## Compiler evidence

The supplied English 1.1 executable strongly indicates **Visual C++ 6.0 SP4**:

| Evidence | Value |
| --- | --- |
| PE architecture / linker | 32-bit x86 / 6.00 |
| Dominant Rich-header C++ contribution | Product 0x0b, build 8799, count 146 |
| Candidate code generator | C2 12.0.8799.0 |
| Candidate linker | 6.0.8447.0 |
| Compiler driver / C++ front end | 12.0.8804.0 / 12.0.8867.0 |

The verified local toolchain generates `@comp.id = 0x000b225f`, matching that
dominant Rich-header stamp. Its PDB also reports code-generator version
12.00.8799.0. Other Rich records can come from linked libraries; they do not
establish a different main compiler. Original compiler flags remain unknown.
The fixture uses `/O2 /Ob0 /Zi /Gd /Gy` as an experiment, not a recovered recipe.
Component hashes are pinned in [msvc6-sp4.json](msvc6-sp4.json); compiler
binaries, SDK files and original game files are never committed.

## Verify the target

[The target record](targets/beachlife-uk-1.1.json) fixes size, SHA1 and SHA256.
Independent retail provenance remains unverified.

```sh
python3 tools/matching.py verify --original orig/analysis/disc/BeachLife.exe
```

If only the ISO is available, the native data tool can obtain this one input:

```sh
mkdir -p orig/matching
./build/debug/beachlife-data cat orig/Beachlife.iso BeachLife.exe > orig/matching/BeachLife.exe
python3 tools/matching.py verify --original orig/matching/BeachLife.exe
```

## Prove the comparison workflow

Use a legally obtained VC6 SP4 toolchain at a local path. On Linux, Wine runs
the compiler/linker and PDB reader. The generated fixture requires no SDK or
CRT libraries. Install the pinned comparison tool in a local environment:

```sh
python3 -m venv build/matching/venv
build/matching/venv/bin/python -m pip install -r decomp/requirements.txt
WINEPREFIX="$PWD/build/matching/wine" WINEDEBUG=-all wineboot --init
python3 tools/matching.py fixture --compiler /path/to/msvc6.4 \
  --reccmp build/matching/venv/bin/reccmp-reccmp
```

The same task is available through the common CMake build:

```sh
cmake --preset debug -DBEACHLIFE_MATCHING_COMPILER=/path/to/msvc6.4 \
  -DBEACHLIFE_RECCMP="$PWD/build/matching/venv/bin/reccmp-reccmp"
cmake --build --preset debug --target matching_fixture
```

This opt-in target does not run during ordinary native builds or CI. The
fixture must produce repeatable `.text`, two 100% self-matches, and a lower
score after a deliberate source change. PE/PDB timestamps can differ. Reports
and generated binaries stay under `build/`; fixture results are not game
completion percentages. See [reccmp](https://github.com/isledecomp/reccmp) for
annotation and comparison semantics.

The stdio fixture checks the next link boundary without requiring an SDK import
library. The pinned VC6 librarian builds a small import library from the four
declared exports in `msvcrt.def`; it contains import metadata, not CRT code.
The fixture uses the pinned VC6 `STDIO.H`, requires eight-byte `fpos_t`, and
verifies the generated executable's actual import names and addresses:

```sh
python3 tools/matching.py stdio-fixture --compiler /path/to/msvc6.4 \
  --reccmp build/matching/venv/bin/reccmp-reccmp
# Or: cmake --build --preset debug --target matching_stdio_fixture
```

This experiment uses `/MD` for the dynamic CRT declarations. It establishes
the stdio ABI needed for a small helper, not a complete SDK/CRT environment or
the original game's entire compiler configuration. The fixture is compiled and
inspected; it does not execute retail code.

## Reconstructed functions

`src/version.cpp` reconstructs the seven-byte version getter at `0x004e3cb0`.
`GetGameVersion` is a descriptive name; the original symbol/class and exact
return declaration remain provisional. The caller uses its x87 return for
the menu's version display.

`src/file_size.cpp` reconstructs the 106-byte helper at `0x004ec150`. It opens
the named file in binary mode, seeks to the end, reads an eight-byte file
position and returns only its low 32 bits. Open/seek/position failures return
zero, as does an empty file; close errors are ignored. `GetFileSize` is a
descriptive name and the original signed return declaration is unknown.

```sh
python3 tools/matching.py game --compiler /path/to/msvc6.4 \
  --reccmp build/matching/venv/bin/reccmp-reccmp
# Or, with the CMake cache variables above configured:
cmake --build --preset debug --target matching_game
```

Both functions currently match after address normalization, totaling 113
bytes. Rebuilt constants/imports have different addresses, so this is not a
byte-identical executable or an established whole-program layout. The original
has stripped relocations; `metadata.csv` identifies the known float and the
binary-read-mode string. The comparison build keeps relocations so reccmp can
identify rebuilt literals. The tool reports raw
byte equality separately and fails if an annotated function does not match.

Reports containing original-derived comparisons stay in ignored
`orig/analysis/matching/`. Generated entry code only keeps reconstructed
functions in the comparison image; it is excluded from game progress and
does not implement game startup. There are two annotated functions and no
established whole-game function denominator.
