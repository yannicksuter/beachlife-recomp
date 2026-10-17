# Read-only game data

Data access is independent of the launcher and does not run the retail game or
installer. The native directory index is documented in [ISO.md](ISO.md).

The regular CMake presets also build the `beachlife-data` development tool:

```sh
./build/debug/beachlife-data list orig/Beachlife.iso
./build/debug/beachlife-data cabinets orig/Beachlife.iso
./build/debug/beachlife-data game-files orig/Beachlife.iso
./build/debug/beachlife-data cat orig/Beachlife.iso Readme.htm
./build/debug/beachlife-data read orig/Beachlife.iso BeachLife.exe 0 64
```

`list`, `cabinets` and `game-files` emit JSON. `cat` and `read` stream raw bytes to stdout; redirect output
when saving a file. `read` takes a decimal byte offset and count and rejects a
range outside that file. Input images are opened read-only. Data commands need
no window, graphics device, installed game or launcher configuration.

## Prepare once, read locally

Prepare the ISO into persistent local storage without running its installer:

```sh
./build/debug/beachlife-data prepare orig/Beachlife.iso
```

The result is JSON with `root`, `identity`, file/byte totals and the number of
reused/written files. Progress goes to stderr. An optional second path selects
the cache parent; keep development extractions under ignored `orig/` or outside
the repository. The defaults are:

| System | Cache parent |
| --- | --- |
| Windows | `%LOCALAPPDATA%/BeachLife/content` |
| macOS | `~/Library/Application Support/BeachLife/content` |
| Linux | `$XDG_DATA_HOME/beachlife/content`, or `~/.local/share/beachlife/content` |

Each ISO/import-format/language selection gets its own identity directory.
Identity uses the full ISO SHA-256, independent of its filename and application
version. Format changes can deliberately select a new directory. Inside it,
`files/` contains the game's relative paths and `manifest.json` records the
source identity, selected layout/language, sizes and SHA-256 checksums.
Saves, screenshots and launcher settings belong outside this read-only tree.

Use the returned `root` with these commands:

```sh
beachlife-data vfs-list <root>
beachlife-data vfs-cat <root> Data/Script/GameParameters.pis
beachlife-data vfs-read <root> Data/Script/GameParameters.pis 0 64
beachlife-data verify <root>
```

A prepared set works with the ISO disconnected. Mounting checks the manifest,
identity, paths, file presence and sizes. It does not hash every asset at each
launch; `verify` performs the full SHA-256 check. A same-size modification is
therefore detected by `verify` or the next preparation, rather than ordinary
reads. Game assets stay in their original formats; decompression does not
convert sprites, scripts or other game formats.

Preparation checks free space, streams bounded chunks into staging files and
checks each original MD5 and expanded size before flushing and atomically
publishing it. The ISO fingerprint is checked again before the final manifest
is published. Missing/corrupt volumes, changed input or failed writes cannot
publish a newly ready set. Limits are 16 GiB of selected data and 2 GiB minus
one byte per asset/volume for the supported cabinet route.

Ctrl+C cancels preparation. Rerunning the same command checks existing files
against the source checksums, reuses verified files and repairs missing or
damaged ones. Only a complete manifest is mountable. A damaged set loses its
ready marker before repair; other identities remain intact. OS locks reject
concurrent preparation of the same identity, and prevent repair while it is
mounted. Locks release automatically after a process exits or crashes.
Symbolic links, hard links, reparse points and special files within managed
content are rejected. An existing unmarked, nonempty identity directory is
never adopted. This storage is owned by the current user, not a security
boundary against another process running as that same user.

## Launcher and runtime handoff

The launcher uses `launcher::ContentService` for background readiness checks.
Source edits, Browse/drop and Revert invalidate pending results. Play saves the
selection and opens the game window immediately. Its `runtime::ContentStartup`
steps run the shared synchronous `launcher::open_content` check/preparation API
on the existing startup worker: validate the cache, prepare or repair if needed,
mount `PreparedSource`, save the offline binding, then read `BeachLife.ini`.
The loading screen shows the actual preparation phases and their byte progress.
Each preparation phase has an equal share within that startup step; percentages
measure completed work, not estimated remaining time.

**Prepare files** and **Verify and repair** use the same loading screen, force
verification from the ISO, and return to the launcher without starting the game
loop. Esc or closing the window cancels the worker; incomplete setup never saves
readiness. Completed files remain reusable. The mount survives the loading steps
for the game session and is released on return, allowing later repair. A graphics
failure happens before preparation starts. The independent data tool remains
available for preparation without a graphics device.

`runtime::read_game_files` creates the loading-screen step with shared ownership
of its `FileSource`. It reports the INI's actual byte count, reads bounded chunks,
and checks cancellation between reads. Missing, truncated and oversized files
fail startup. The current small INI read usually finishes within the screen's
grace period; no artificial delay or whole-cache preload is added. Future startup
assets can join this queue when their loading requirements are reconstructed.
The `content_startup` test exercises the complete loading worker without a
graphics device: preparation, cancellation/resume, saved/offline reads, repair,
mount lifetime, progress and storage failures.

Launcher settings format 3 remembers the prepared identity and the absolute ISO
path, size and modification time; format 4 adds the performance overlay, and
formats 1 to 3 still load. Normal startup
checks that stamp and the prepared manifest/files, or uses the remembered set
when the ISO is absent. This quick source check is not a content hash: a change
that preserves both size and timestamp needs explicit **Verify and repair** or
reselection. Reselecting even the same path clears remembered readiness. Full
preparation/repair fingerprints the ISO and verifies the assets. The binding
and prepared contents stay outside application packages and updates.

`beachlife --play` uses the same setup path with saved settings. The development
option `--cache-directory folder` selects another cache parent; use the same
override on later launches. It does not change the persisted default location.

Link `beachlife_cabinet` and include `data/content_prepare.h` for setup and
`data/content_manifest.h` for mounting:

```cpp
// On a setup worker; callbacks run synchronously on that worker.
auto prepared = beachlife::data::prepare_content(iso, cache_parent, options);
// Pass prepared.root to the runtime when setup completes.
auto content = std::make_shared<beachlife::data::PreparedSource>(prepared.root);
auto script = content->open("Data\\Script\\GameParameters.pis");
```

`PrepareOptions` provides progress and cancellation callbacks.
`PrepareProgress` reports phase (`fingerprint`, `checking`, `extracting`,
`confirming`, `ready`), current path and per-phase byte/file counts.
The startup worker translates callbacks into loading-screen status snapshots
and handles `PreparationCancelled` separately from errors. Callbacks must not
throw after `ready`: publication
has already completed. `PrepareResult` supplies the prepared root, manifest,
language/profile and reuse counts. The default profile currently supports the
observed English layout; installer translations are not game languages.

The runtime can accept the mounted source through `FileSource`, independently
of window creation and the graphics API. Keep the mount for the runtime's
lifetime; open streams also retain its shared lock. A file has its own cursor;
use separate handles for independent readers and synchronize access if sharing
one handle between threads. A ready data set does not establish
that reconstructed gameplay is available.

## Source interface

`FileSource` exposes logical-path `open`. `IsoSource` indexes a selected ISO,
`FolderSource` opens an explicit list of prepared files, and `PreparedSource`
mounts a complete managed manifest. They return owned `SourceFile`
objects. Each object exposes size, absolute seek, position and bounded reads.
Reads stop at the file's own extent; EOF returns zero. Seeking past EOF fails.
Independent handles have independent positions and outlive their source index.
Ambiguous case aliases, missing files, directory reads, changed image sizes and
truncated reads report errors. Keep the source image unchanged while using it.

Reading a loose disc file does not decompress an installer cabinet.
`game-files` maps the English game's Data/ tree and BeachLife.ini from the
supported groups, excluding installer support files, executables and
placeholders. `game-cat image.iso logical/path` streams one mapped asset,
including decompression and original size/MD5 checks. It is a diagnostic
command; a failure can leave partial stdout, so consumers must check its exit
status. Preparation stages output before making it available. The development
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
Names retain their original bytes: JSON maps each byte
to the same Unicode code point (`name_encoding: byte-values`) until a source's
character encoding is established. This is not game-language detection.
The mapper accepts only the observed ASCII path encoding and English group
layout. Shared payload links are resolved with bounds, cycle and checksum
consistency checks. The streaming reader rejects split and obfuscated members;
unknown editions/layouts require qualification before support is added.

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
`game_content_paths`, `game_content_reads`, `prepared_folder_reads`,
`content_manifests` and `prepared_content` cover generated compressed, loose
and empty assets, malformed inputs, offline reads, source identity, cancellation,
resumption, repairs, lock lifetime/recovery and unsafe paths. CI runs these on
Linux/macOS and tests the cross-built data executables on native Windows.
