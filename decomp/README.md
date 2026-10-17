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
