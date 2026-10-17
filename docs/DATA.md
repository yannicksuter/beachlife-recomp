# Read-only game data

Data access is independent of the launcher and does not run the retail game or
installer. The native directory index is documented in [ISO.md](ISO.md).

The regular CMake presets also build the `beachlife-data` development tool:

```sh
./build/debug/beachlife-data list orig/Beachlife.iso
./build/debug/beachlife-data cabinets orig/Beachlife.iso
./build/debug/beachlife-data cat orig/Beachlife.iso Readme.htm
./build/debug/beachlife-data read orig/Beachlife.iso BeachLife.exe 0 64
```

`list` and `cabinets` emit JSON. `cat` and `read` stream raw bytes to stdout; redirect output
when saving a file. `read` takes a decimal byte offset and count and rejects a
range outside that file. Input images are opened read-only. Data commands need
no window, graphics device, installed game or launcher configuration.

## Source interface

`IsoSource` indexes a selected ISO and opens logical paths as owned `SourceFile`
objects. Each object exposes size, absolute seek, position and bounded reads.
Reads stop at the file's own extent; EOF returns zero. Seeking past EOF fails.
Independent handles have independent positions and outlive their source index.
Ambiguous case aliases, missing files, directory reads, changed image sizes and
truncated reads report errors. Keep the source image unchanged while using it.

Reading a loose disc file does not decompress an installer cabinet. Cabinet
metadata inspection is implemented; game-path mapping and compressed member
access remain separate capabilities. The development
tool is built locally/with CI; release packages currently contain the launcher.

## Cabinet dependencies

The shared presets build static unshield 1.6.2 and zlib 1.3.2 from immutable
gitlinks. `tools/setup_dependencies.py` fetches them; configuration exports
verified copies into the build tree. No dependency checkout is edited and no
system zlib/OpenSSL installation is used. Only the library sources are built.
The unshield, zlib and included RSA MD5 notices are retained under `LICENSES/`
and in native packages. Cabinet metadata uses an adapter over the ISO source
interface.

`cabinets` reads `data1.hdr` directly from the selected ISO and lists components,
groups, directory names and file descriptors. It distinguishes cabinet members,
external loose files and placeholders. The metadata command needs no cabinet
payload files and creates no extracted files. An immutable header snapshot is
validated before unshield reads it through bounded, read-only callbacks.

The supported layout is InstallShield 6 with a separate header, capped at
16 MiB and 100,000 directory/file entries. Invalid offsets, truncated tables,
unterminated names, invalid group ranges and linked-list cycles report errors.
This validates metadata access; compressed-data validation belongs to the
future member reader. Names retain their original bytes: JSON maps each byte
to the same Unicode code point (`name_encoding: byte-values`) until a source's
character encoding is established. This is not game-language detection.

The `cabinet_index` CTest uses generated metadata inside a generated ISO,
including malformed cases, 64-bit sizes, unusual name bytes and Unicode host
paths. It never requires a retail cabinet or extracted asset.

## Logical paths

`normalize_game_path` in `src/data/game_path.h` accepts relative UTF-8 names.
It converts slash/backslash separators, skips repeated separators and `.`
components, and retains the display spelling. Its lookup key folds ASCII
letters to lowercase, matching the paths observed in the game. Non-ASCII
characters remain exact; full Unicode Windows case folding is not implemented.

Absolute paths, drive names, `..`, alternate streams, wildcards, device names,
trailing dots/spaces, control characters and malformed UTF-8 are rejected.
Paths are limited to 4096 input bytes. These are logical source names, not
permission to join unchecked paths onto a native directory; the folder backend
must separately enforce containment, including symbolic links.

`game_paths` in CTest covers lookup equivalence, preserved Unicode/display
spelling and invalid paths using generated data.
`iso_file_reads` checks the independent command-line tool and C++ source
interface, including binary output, Unicode host paths, range limits, empty
files, streaming, source lifetime, case collisions and truncation.
