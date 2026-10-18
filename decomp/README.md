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
library. The pinned VC6 librarian builds a small import library from
`msvcrt.def`; it contains import metadata, not CRT code. The stdio fixture uses
four exports; the game comparison also uses `fread`, `free` and operator new.
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

`src/file_loader.cpp` reconstructs the 83-byte plain file loader at
`0x004ec1c0`, used by the TGA file-to-image wrapper. It obtains the size, returns
null for missing or empty files, allocates one extra byte, reopens and reads
the file, then appends a period. The allocation, second open, read and close
remain unchecked. Unlike the BLE loader it leaves the input bytes unchanged.
`LoadFile` is descriptive; the original symbol and allocation spelling are
unknown.

`src/ble_loader.cpp` reconstructs the 102-byte loader at `0x004ec220`. It asks
the size helper for the low 32-bit file length, allocates one additional byte,
reopens and reads the file, inverts its bytes with a signed index comparison,
and appends a period. It preserves the unchecked allocation, second open,
read and close, as well as the period instead of a NUL terminator. Missing and
empty files return null through the size helper. `LoadBleFile` is descriptive;
the original symbol and allocation spelling remain unknown. VC6 lowers this
byte-array allocation to the verified imported operator new.

`src/sound_name_hash.cpp` reconstructs the 29-byte filename hash at `0x004d2280`.
It multiplies a 32-bit unsigned accumulator by five and adds each signed byte
until NUL, wrapping at 32 bits. The signed conversion is explicit so native
compilers with unsigned plain `char` preserve the observed behavior.
`SoundNameHash` is descriptive; the original symbol and declaration are unknown.

`src/tga_palette.cpp` reconstructs the 29-byte palette pointer helper at
`0x004ea3e0`. It returns null when the color-map flag's low byte is zero;
otherwise it returns the buffer plus the 18-byte TGA header and the ID length's
low byte. It preserves unchecked pointer arithmetic and performs no file-size
validation. `GetTgaPaletteData` is descriptive; the original symbol and exact
declaration remain unknown.

`src/tga_pixel_data.cpp` reconstructs the 42-byte pixel-data pointer helper at
`0x004ea400`. It skips the 18-byte header and the ID length's low byte, then
adds three bytes per palette entry when the color-map flag's low byte is
nonzero. Palette length uses only its low 16 bits. It preserves the fixed
three-byte palette stride and unchecked pointer arithmetic. `GetTgaPixelData`
is descriptive; the original symbol and exact declaration remain unknown.

`src/tga_palette_copy.cpp` reconstructs the 56-byte palette copier at
`0x004ea430`. It writes BGR input triples as RGB into the palette pointer at
image offset `0x14` on x86. The signed count skips writes when nonpositive;
the image pointer is still accessed first. Source and destination remain
unchecked, and sequential byte reads/writes preserve the original behavior
for overlapping buffers. `CopyTgaPalette` and the observed `TgaImage` layout
are descriptive; original names and the full type declaration remain unknown.

`src/tga_image_constructor.cpp` reconstructs the 43-byte non-allocating image
constructor at `0x004ea2b0`. It preserves the supplied dimensions without
validation, sets 16 bits and two bytes per pixel, and clears the three buffer
pointers. It leaves the final four bytes of the 32-byte x86 object untouched.
Verified callers assign pixel storage afterward. `TgaImage` and its field
names are descriptive; the final field's meaning is still unknown.

`src/tga_pixel_copy.cpp` reconstructs the 328-byte pixel copier at
`0x004ea470`. It reverses rows for 8-, 24- and 32-bit images, converts BGR to
RGB for the latter two formats, and preserves 32-bit alpha. Unsupported depths
and signed nonpositive dimensions cause no pixel writes. The helper preserves
unchecked buffers and sequential channel reads/writes, including their effects
on overlapping storage. `CopyTgaPixels` is descriptive; the original symbol and
exact declaration remain unknown.

`src/tga_image_cleanup.cpp` reconstructs the 49-byte image destructor at
`0x004ea2e0`. It releases the pixel buffer unconditionally, then releases the
palette and converted palette when nonnull. It reads each pointer after the
previous release and leaves every field unchanged. VC6 lowers these trivial
array deletions to scalar operator delete. The original class name remains
unknown.

`src/tga_rle.cpp` reconstructs the 249-byte RLE expansion helper at
`0x004ea5c0`. It allocates a temporary image, expands raw and repeated-color
packets, passes the result to `CopyTgaPixels`, then releases the temporary.
Packet counts use the low seven header bits plus one; the high bit selects a
repeated color. It preserves unchecked sizes, source reads and output writes,
including the four-byte local color buffer and unclamped packet counts.
`ExpandTgaRle` is descriptive; the original symbol and declaration are unknown.

`support/operator_delete.cpp` supplies the separate 11-byte C++ runtime wrapper
at `0x004ee6d6`, forwarding to the verified MSVCRT `free` import. It is library
support, excluded from the game-method count. The matching build appends `/O1`
only for this file: size optimization reproduces the memory-operand push,
where `/O2` emits a longer load/push sequence. This is a measured setting for
the wrapper, not a recovered whole-library build recipe.

```sh
python3 tools/matching.py game --compiler /path/to/msvc6.4 \
  --reccmp build/matching/venv/bin/reccmp-reccmp
# Or, with the CMake cache variables above configured:
cmake --build --preset debug --target matching_game
```

All twelve game functions currently match after address normalization, totaling
1,123 bytes. The filename hash, four TGA helpers and image constructor also match
all 527 raw bytes exactly. Rebuilt constants/imports in the other functions
have different addresses, so this is not a byte-identical executable or an
established whole-program layout. The original has stripped relocations;
`metadata.csv` identifies the known float and binary-read-mode string and names
the verified operator-new and `free` import thunks as library code. The comparison
build keeps relocations so reccmp can identify rebuilt literals. The game
comparison adds `/MD /G6` to the fixture flags:
`/G6` reproduces the loader's loop scheduling, while `/G3`, `/G4` and `/G5`
produce a different instruction order. This is evidence for this small source
set, not a recovered whole-program compiler recipe. The tool reports raw
byte equality separately and fails if a registered target function is missing
or does not match. The 11-byte release wrapper also matches after address
normalization and is reported and checked separately under `library_functions`.

Accuracy is checked at the function level. The target executable and compiler
components are hash-verified, reconstructed C++ is compiled with the pinned
toolchain, and PDB symbols locate the rebuilt functions. The strict comparison
checks their disassembled instructions against the original addresses. Raw
byte equality is a separate result because calls and data references can have
different addresses in the small comparison executable. Those differing
operands are also inspected to confirm the referenced function, import or
constant; a normalized match alone does not establish a complete binary match.

Each reconstructed function has been checked with a deliberately incorrect
source variant. Changing the BLE terminator, filename-hash signedness or TGA
header offset, palette channel order, constructor pixel depth or pixel row
direction makes the comparison fail. Adding a pixel-null guard to the
destructor, changing the RLE packet flag or changing the release wrapper's
forwarded pointer also fails. Generated semantic tests additionally check the
pure helpers' boundary values, palette/pixel copy overlap, untouched constructor
storage, destructor release order and unchanged fields, and raw/run RLE packet
boundaries with temporary allocation/release pairing. These checks establish
the reported machine-code matches; they do not recover original variable names,
class declarations or the full game's compiler flags.

Reports containing original-derived comparisons stay in ignored
`orig/analysis/matching/`. Generated entry code only keeps reconstructed
functions in the comparison image; it is excluded from game progress and
does not implement game startup. There are twelve annotated game functions and
no established whole-game function denominator.
