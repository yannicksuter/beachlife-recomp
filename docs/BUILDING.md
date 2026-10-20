# Building

The current executable is the graphical launcher and native runtime. Gameplay
and broader content-language detection are not implemented yet. Building requires
no original game files or matching compiler.

## Requirements and presets

Use CMake 3.25+, Ninja, Python 3.10+, Git and a C++20 compiler. Build from a Git
checkout so generated identity can record the source commit. Configure outside
the source directory. `CMakeUserPresets.json` can hold local overrides.

```sh
python3 tools/setup_dependencies.py
cmake --workflow --preset debug
cmake --workflow --preset release
cmake --workflow --preset launcher
```

Each workflow configures, builds and runs CTest. `debug` is for development;
`release` is the distributable configuration; `launcher` currently builds the
same launcher. Individual commands are available:

```sh
cmake --preset release
cmake --build --preset release --parallel
ctest --preset release
```

Build identity refreshes on every build. The version comes from CMake; the
commit and dirty marker come from Git. `--build-info` emits the complete JSON
record. Local builds use channel `dev`; Actions sets `test` or `release` and
records its PR/run IDs. Test/release packages require clean committed sources.

SDL3, Dear ImGui, stb, FreeType, unshield and zlib are immutable Git submodule pins.
Dependency setup is an explicit network step; CMake never fetches source.
Configuration exports each clean pin into the build directory, applies any ordered
`patches/<name>/series`,
and records file hashes. Every build rejects edited checkouts, edited prepared
files or stale patch identities. Put changes in patch files, never in `extern/`.

## Source formatting

All C and C++ sources in `src/`, `tests/` and `decomp/` use clang-format at the
version pinned in `tools/requirements-format.txt`. Patch releases can format
differently, so the tool refuses any other version. Install the pinned wheel
once, then format the work tree:

```sh
python3 -m venv build/format-venv
build/format-venv/bin/pip install -r tools/requirements-format.txt
python3 tools/format_sources.py
python3 tools/format_sources.py --check
```

Paths limit either command to files or folders. Tracked and new files are
included; submodules, ignored files and `orig/` are not. `--check` prints the
differences and fails without changing anything; the Linux CI job runs it.

The root `.clang-format` is the native C++20 style. `decomp/.clang-format` keeps
include order and C++98 `> >` for the VC6 compiler. Formatting moves lines and
adds braces but does not change behavior. Only line numbers (`__LINE__`,
`std::source_location`) can differ in the output. Code that must keep its line
numbers goes between `// clang-format off` and `// clang-format on` with a
reason. Format a reconstructed file before writing patches for it, so the
patches apply to formatted source.

## Matching compiler options

The prepared matching target JSON can declare `source_compile_options` for a
qualified source file. It maps a `.cpp` basename, unique across game and support
sources, to a list containing `/GX`, `/Ob1`, or both. Unknown files, directory
paths, ambiguous basenames, duplicate flags and other options are rejected.
The comparison retention fixture cannot receive these options. Unlisted files
retain the default compiler settings; the runtime release wrapper retains its
existing `/O1` setting even when additional qualified options apply. Add target
changes through a reconstruction patch and verify that all retained methods still
match. These options do not change the native CMake build.

A `/GX` entry adds a generated COFF definition of the absolute `__except_list=0`
ABI symbol required for VC6's `FS:[0]` exception registration. The object has no
code, data or sections. Actual CRT imports and comparison metadata must still be
provided by the target; the tool does not synthesize exception handlers. Matching
results record the selected source options and generated object's identity.

Matching targets can also declare `comparison_anchors` for unresolved member
call identities. The tool generates fingerprinted definitions in ignored build
output, outside the prepared source export, and excludes them from credited
methods. These definitions establish comparison addresses only. An image using
them must never be executed; native builds require the actual dependency source.

## Reconstructed configuration libraries

Build the prepared configuration and VFS file libraries without test executables:

```sh
cmake --preset release -B build/reconstructed-config \
  -DBUILD_TESTING=OFF
cmake --build build/reconstructed-config \
  --target beachlife_reconstructed_startup_config --parallel
```

Consumers link `beachlife_reconstructed_startup_config`; its public dependencies
provide the file, scanner, token, allocation, parser and loading libraries.
Normal Play now requires these libraries, so the former
`BEACHLIFE_BUILD_RECONSTRUCTED_CONFIG` switch is no longer needed. They use the
same pinned export and per-build verification as the tests below. The historical
commit in `patches/beachlife-decomp/source.json` must be available locally; CI's
compiling jobs fetch full history. Configuration never fetches source itself.
Packaging verifies the native reconstructed export again and records its baseline
revision, source tree and ordered reconstruction/portability patch hashes in
`SOURCE-MANIFEST.json`, alongside third-party dependency identities.
Image, input, cursor and surface subsystem targets remain optional.

The game's `beachlife_startup_configuration` host library validates the audited
1,242-byte English configuration, then constructs the real owner on the game
thread before the loading fade. It retains the immutable input snapshot and
the shared `beachlife_retail_files` binding through teardown. Unsupported input
fails before the owner is published. During faithful construction, a temporary
process-wide default allocation handler terminates on allocation failure;
snapshot/preflight errors remain recoverable. The original parser is unchanged.
`startup_configuration` runs generated owner/lifetime/rejection checks with
both default and injected allocators, plus subprocess allocation-failure probes.
Its wrapper requires both successful exit and an explicit completion marker,
so an original zero-status fatal exit cannot pass the test.

## Reconstructed subsystem tests

The optional targets compile reconstructed image, file, configuration, input,
surface, pool-lock, screen-list, screen-item and screen-registry helpers using the native compiler.
They consume a committed source export with faithful patches through the same
preparation tool as dependencies. Enable them explicitly:

```sh
cmake --preset release -B build/reconstructed-tga \
  -DBEACHLIFE_BUILD_RECONSTRUCTED_TESTS=ON
cmake --build build/reconstructed-tga \
  --target reconstructed_tga_tests reconstructed_files_tests \
    reconstructed_config_tests reconstructed_config_tokens_tests \
    reconstructed_config_allocation_tests reconstructed_config_parser_tests \
    reconstructed_config_loading_tests reconstructed_startup_config_tests \
    startup_configuration_tests startup_configuration_default_tests \
    reconstructed_input_tests reconstructed_cursor_tests \
    reconstructed_surfaces_tests reconstructed_surface_pixels_tests \
    reconstructed_pool_lock_tests reconstructed_screen_list_tests \
    reconstructed_screen_item_tests reconstructed_screen_registry_tests reconstructed_screen_placements_tests --parallel
ctest --test-dir build/reconstructed-tga -L reconstructed --output-on-failure
```

The test option requires
`BUILD_TESTING`. `patches/beachlife-decomp/source.json` owns the
full baseline commit pin shared with matching; that object must exist locally,
so shallow checkouts may need more history before configuration. Configuration
never fetches it. New reconstructed increments are patches listed in
`reconstruction-series`; native portability patches follow in `series`.
Keep the baseline intact. Uncommitted edits under `decomp/` are rejected;
unrelated host edits do not affect the export.

Configuration prepares `build/reconstructed-tga/prepared/beachlife-decomp/`
and its adjacent JSON manifest. The export's `src/` directory contains ordinary
C++ translation units and headers, including files added by reconstruction
patches. Open that directory to read the combined source; the patches record
changes relative to the pinned baseline.

Every library build, including an otherwise up-to-date build, verifies the
revision, source hashes, ordered patch series
and prepared files. Update reconstruction or portability patches and reconfigure; never edit prepared
files. The matching-only VC6 release wrapper is excluded in favor of the host
CRT. Subsystem test executables remain opt-in; configuration and file libraries
are part of normal Play.

### Experimental reconstructed code

Fully implemented reconstructions can be qualified with native behavior tests
while original-compiler byte matching continues. Enable this source selection
explicitly in a separate build directory:

```sh
cmake --preset release -B build/experimental \
  -DBEACHLIFE_ENABLE_EXPERIMENTAL_GAME=ON \
  -DBEACHLIFE_BUILD_RECONSTRUCTED_TESTS=ON
cmake --build build/experimental --parallel
ctest --test-dir build/experimental -L reconstructed --output-on-failure
```

The `release` preset selects compiler optimization; the build channel remains
`dev`. The option defaults to `OFF` and is rejected with the `release` channel.
It selects the `native-experimental` prepared-source profile, applying
`reconstruction-series`, then `experimental-series`, then the native portability
`series`. Ordinary native and matching builds ignore `experimental-series`.
The preparation CLI also accepts `--decomp-profile native-experimental`.

Build metadata records `decomp_profile`; packaging verifies that exact profile
and records its ordered patch hashes. Packages with different source profiles
cannot form one release matrix. Enabling the option does not complete a missing
game controller or menu. Each experimental subsystem still needs real source,
behavior tests and an explicit runtime connection. A native test pass does not
count as a byte match or establish production gameplay.

The pool-lock target compiles the unchanged acquisition and cleanup methods
against a test-only platform provider. It checks that both methods use the same
32-bit word, preserves adaptive state across fast and fallback acquisition, and
verifies the wait sequence and cleanup on normal and exceptional host lifetimes.
Its deterministic single-threaded provider exercises no operating-system
synchronization. Concurrent behavior, original parent unwind machinery and
production use remain separate qualification steps.

The screen-list target combines the actual node builder, insertion, Begin/End,
chunk allocator, recycling and shared lock code. Its native patch sizes slots
for the compiled node and establishes storage and recycled-object lifetimes.
A scoped scalar-new replacement lives only in the test executable, leaving
the prepared methods unchanged. A fixed real-storage ledger checks allocation
sizes and order; an independent sequence checks both list links, payloads,
sentinels, reuse, guards and cleanup after labelled host allocation exceptions.
Checked fixture teardown releases its own backing storage. It does not implement
the missing retail list destructor. The target disables LTO and uses the same
deterministic platform provider as the pool-lock test. Null-return scavenging,
retry after allocation failure, original CRT failure behavior, operating-system
concurrency and production screen ownership remain outside its tested domain.

The screen-item target exercises the actual constructor, ordinary destructor,
rectangle callbacks and retained list/search helpers with generated live objects.
It checks initial state, repeated positioning, bounded movement, virtual calls,
owned-text release and unchanged surrounding storage. Paired scalar allocation
and deletion providers live only in the test executable and use the host CRT.
Name lookup uses the same active-entry and value-node declarations as insertion,
with genuine generated Dialog objects. The optional archive publishes
`BEACHLIFE_DECOMP_TEST_OBJECTS=1` to its consumers so every translation unit sees
the same conditional inherited-constructor declaration. That native facility is
off by default and supplies no original Dialog constructor or cleanup. The
actual Dialog preparation and positioning methods supply their generated
virtual-table entries; the positioning override is linked from its own source.
The target disables LTO and links no VC6 release support. It requires no ISO;
complete derived lifetimes, original allocation-failure behavior and production
screen rendering remain separate work.

The screen-registry target connects actual insertion and name lookup in one
archive, with one owner for each iterator, pool and screen method. Generated
live Dialog objects use the same test-only construction facility. Literal
expectations cover empty and missing names, duplicates, ordering, bounded
subranges, pointer identity and recycled-node reuse; lookup preserves the graph
and object state. Paired scalar allocation providers and the deterministic
uncontended lock provider belong only to the test executable. The actual Dialog
positioning override is linked for the generated virtual table; this fixture
does not exercise that callback. No original Dialog or list lifetime, allocation
failure, operating-system concurrency, menu flow or production use is claimed.

The surface-pixel target compiles the actual pixel-access method and error
handler. Generated lock providers check descriptor initialization, arguments,
signed success results and state changes during the callback. The same fixture
covers software pixels, null results, valid table aliases and unchanged guards.
Native layouts follow the compiled pointer types; these tests require no graphics
device and do not qualify original x86 object layouts or a rendering backend.

The generated fixtures check palette/pixel offsets, channel conversion, row
orientation, raw and run-length packets, empty images and buffer ownership.
They require no ISO or extracted data. The target is a native test executable;
the production launcher still uses its existing decoder. A passing test does
not establish a complete TGA parser, original ABI matching or gameplay.

The file target runs the matched size, plain-file, BLE and startup configuration
readers through an explicit VFS session. Its portability patches preserve 32-bit size/count behavior,
the period terminator and signed byte inversion on 64-bit hosts. Scoped bindings
belong to one thread, restore nested sessions and keep open handles' mounts alive.
Reads use the supplied `FileSource`; they never fall back to the process directory
or reopen the ISO. Generated tests cover real folder reads, aliases, short reads,
stdio error results, seek/position behavior, ownership and repeated sessions.
The configuration reader writes into caller-owned storage without adding a
terminator. It returns one whenever opening succeeds, even after a short or
failed read, preserving the original behavior. Generated tests check unchanged
buffer tails and guards, error paths and buffer lifetime after the session ends.
`BEACHLIFE_RECONSTRUCTED_FIXTURE_ROOT` selects the generated fixture directory;
it defaults to the build tree. For a Windows cross-build run through Wine, use
a path on its C: drive if Wine exposes the Unix build path as a reparse point.

The unchecked retail loaders require stable files with a successful second open
and sizes from 1 through `INT32_MAX - 1`; BLE inversion additionally requires a
complete read. Tests cover empty/missing files and size-only 32-bit truncation
separately, without exercising invalid allocation/index domains or reading
uninitialized tails. This target qualifies file services for reconstructed
startup; it does not connect a complete game driver to Play.

The configuration target runs eight matched scanner helpers over generated byte
arrays. It checks separator rules, apostrophe counting and quoted/unquoted
extents, quote seeking and token counts, including embedded NULs, high-bit bytes
and nonpositive sizes. The value helper requires one readable byte even for a
nonpositive size; quote seeking needs an extra readable byte after a positive
extent. Tests retain the earlier exact-boundary checks and screen composed
counter inputs with an independent read-bound model before running unchecked
code. Malformed cases outside that domain are excluded explicitly. These
helpers are also used by the separate token-parser target. Value classification
checks the numeric prefix before counting quotes and requires readable byte zero
even for nonpositive sizes. The separate quoted-value helper returns the extent
through the second double quote, wherever it occurs; it does not validate string
syntax. Generated tests preserve both contracts and their exact read bounds.

The token lifecycle target runs matched initialization and cleanup through the
host C++ allocator. Generated fixtures verify borrowed data remains unchanged,
owned scalar and array allocations are released once in the original order,
and owner fields and surrounding guards are unchanged.

The allocation target adds a native `std::nothrow` adaptation for the legacy
default allocator's null-on-failure result. Generated fixtures check bounded
table sizes, zero-size allocation, initialized records and injected failures.
They preserve the overwritten old pointer and unchanged count on failure;
test-owned storage is recovered separately. Custom allocation handlers and
oversized or wrapping counts remain unqualified.

The token-parser target adds matched name/value assignment, decimal-prefix
conversion and token parsing. Generated fixtures check copied bytes, terminators,
record order and retained old allocations against an independent byte model.
Callers provide initialized records with enough capacity and one readable
lookahead byte; the parser does not enforce capacity or update the stored count.
Unsafe composed inputs are screened before executing unchecked source. Numeric
fixtures use exact integers of magnitude at most 1,000,000 with at most one zero
fractional digit, including signed zero. General fractional parity with the
original x87 arithmetic, allocation failure and invalid pointer/extent domains
remain unqualified. The setters use ordinary successful host allocations here;
the allocation target's failure adaptation does not extend to these methods.
This target does not load configuration files or connect a game startup driver.

The configuration-loading target runs the matched file orchestration through the
existing VFS reader, scanner, allocator and parser. It preserves the probe, size
and read opens, low-32-bit size arithmetic, ignored I/O results and final NUL.
Generated fixtures cover complete bounded files, repeated partial reads, a file
growing between opens, virtual sizes above 4 GiB and checked token-table allocation
failure. A previous table is overwritten without being freed; tests recover that
storage separately. Failure can leave a null table with a nonzero old count,
which the unchecked cleanup cannot safely destroy; fixtures observe that state
before resetting their own count for teardown. The temporary allocation uses the legacy nonthrowing result
without adding a null check. Temporary and payload allocations must succeed in
the executed domain.

The loader's final NUL is inside its allocation, with no extra lookahead padding.
Composed records are generated to stay within that extent. Missing read opens or
failed reads are exercised only when the size stage produced a one-byte buffer,
which the final NUL fully initializes. Positive-size incomplete reads, position
failures that write no result, invalid size arithmetic and general fractional
parity remain unqualified. Tests also load generated files from a real folder and
check that parsed allocations outlive the VFS session.
`BEACHLIFE_RECONSTRUCTED_CONFIG_FIXTURE_ROOT` selects their separate output
directory, defaulting to the build tree; Windows cross tests can use a Wine C:
path. Existing subsystem fixtures create owners with no VFS bound before their
own observations. Additional loading cases construct the real owner while a
source is mounted: prefilled storage and a first-open observer check that the
table pointer and count are initialized before I/O. Generated records remain
valid after the session ends and are released by the actual destructor.
Construction cases retain the same bounded failure domains and exercise real
folder mounts, comments and exact integers from -2048 through 2048. The selected
literal `2.5` is separately checked against float32 bits `0x40200000`, directly
and through construction. Other nonzero fractions and changed floating-point
modes remain outside this qualification.

The startup-configuration target constructs the actual outer owner through the
same VFS and token-loading library. It checks publication of the owner and a null
list head before the first open, ordered typed records, duplicate names, copied
strings, case-sensitive lowercase `true`, appended records and actual cleanup
with global reset. Generated configurations are nonempty, with names and text
of at most 255 bytes. Embedded NULs retain the original C-string prefix behavior.
Numeric cases cover every integer from -2048 through 2048 and sample exact
integers up to magnitude 1,000,000, with at most one zero fractional digit,
signed zero and the separately qualified `2.5` literal. The original destructor
assumes a nonempty list; empty-owner destruction, general fatal-error paths,
allocation failure and exception unwinding remain unqualified. The fixture's
completion guard rejects an early zero-status fatal exit while preserving
CTest's normal exit-code checks. `BEACHLIFE_RECONSTRUCTED_STARTUP_FIXTURE_ROOT` selects its separate
generated folder, defaulting to the build tree. Normal Play uses the separate
host boundary described above; these broad subsystem fixtures do not establish
a complete startup driver or menu.

The input target checks two matched byte-state queries, transition reset and
two signed-integer getters, plus the first-button transition getter.
Generated fixtures cover all 256 valid indices and byte values, require exact
zero-or-one results, and compare the whole declared object representation and
surrounding guards. Reset clears only the two observed words and two bytes;
tests check their widths, unchanged fields and repeated-call idempotence.
Getter fixtures compare exact signed returns against independent supplied
values over the complete signed 16-bit domain, 32-bit boundaries, walking-bit
patterns and deterministic generated pairs. Both calls must leave the whole
object and its guards unchanged; reported call counts are fixture entry calls.
The first-button getter has 1,033 generated cases covering unsigned 32-bit
boundaries and every value in each byte lane. Its neighboring button word differs
before reset; exact returns and whole-object guards are checked before and after the
real transition reset. These are bounded value/sequence tests, not device events
or a claim about which physical button the first word represents.
The declarations describe an observed prefix, not the complete retail input
object. The current target requires the recovered resource-pointer prefix and
both observed signed-integer fields. It checks relative field positions in the
actual native layout rather than imposing x86 offsets on a 64-bit build.
Pointer fields receive null or borrowed fixture addresses; signed fields receive
typed boundary and representative values before their representations enter the
object. Both input and cursor fixtures also accept the observed typed keyboard
pointer prefix using relative native offsets. Its nonnull values are aligned
fixture identities used only for preservation checks, never COM devices or
callable API providers. All typed values, padding and surrounding bytes remain part of the
unchanged-representation checks. Cursor methods have their own target;
these getter checks do not establish the fields' original meaning.
Out-of-range queries, state
production, device input and integration with the controller or game loop remain
unqualified.

The cursor target runs the reconstructed setter and current-cursor wrapper
through an explicit thread-local provider. The native patch replaces Win32
integer-resource/API spelling with typed numeric-resource calls; it preserves
resource IDs, image type, dimensions, flags, cached-state gates and publication
after callbacks. Generated providers record exact event order and arguments,
null or live handle results, callback-visible state and whole-object guards.
Failed loads still remember the requested identifier. The current-cursor
wrapper passes that cached identifier and therefore does not force a reload.
Tests cover ignored arguments and set-call returns, invalid identifiers, nested
bindings and thread isolation. Handles are borrowed fixture values; no game
assets, Win32 resources or platform cursor backend are used. A missing provider
or invalid session lifetime violates the host boundary's preconditions and
terminates; this is separate from the retail API's null-result behavior.

The surface target compiles the real position setter, member cleanup and indexed
cleanup from that same export. Generated guarded objects and live primitive
arrays exercise exact selected-slot writes, signed coordinates, table aliases,
inactive repeats and software/Surface7 ownership. A typed Surface7 provider
checks the receiver and state visible before release, then mutates fields or
replaces table bindings; the independent final-state oracle verifies publication
on the originally selected object and preservation of unrelated bytes. Return
values from Release are ignored. Pointer indices stay within the generated
table, including negative indices relative to an interior table pointer.
Hardware objects are borrowed providers; no DirectDraw backend or GPU is used.
Invalid pointers, provider exceptions, reentrancy and concurrent mutation are
outside this fixture's domain. A null software buffer has valid final-state
checks, but modern C++ may omit its deallocator call; the test does not claim the
original VC6 null-delete call timing. The optional image, input, cursor and
surface libraries are not linked into production gameplay.

For a separate Linux sanitizer build, add
`-DCMAKE_CXX_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer'` and
`-DCMAKE_EXE_LINKER_FLAGS='-fsanitize=address,undefined'` at configuration.
Windows cross builds need an explicit execution environment to run CTest,
as described below. Hosted CI and hardware qualification remain separate checks.

## Linux

The primary CI target uses Ubuntu 26.04 and GCC 15. The compatibility/Steam Deck
package uses Ubuntu 24.04 and GCC 14. The packager checks its imported libraries
and symbol versions against the SteamOS 3.7 runtime baseline. This check is not
a claim that GPU operation or gameplay has been qualified on the device.

Install SDL's development prerequisites before configuring on Ubuntu:

```sh
sudo apt-get install build-essential cmake ninja-build python3 git pkg-config \
  libasound2-dev libpulse-dev libx11-dev libxext-dev libxrandr-dev \
  libxcursor-dev libxi-dev libxfixes-dev libxss-dev libxtst-dev libx11-xcb-dev libegl-dev \
  libwayland-dev libxkbcommon-dev wayland-protocols libdecor-0-dev \
  libudev-dev libdbus-1-dev libvulkan-dev
```

```sh
./build/release/beachlife --help
python3 tools/package_build.py --build-dir build/release --output-dir build/artifacts
python3 tools/check_package.py build/artifacts/*.tar.gz
```

Use `-DBEACHLIFE_PACKAGE_PLATFORM=steamdeck` only with a suitable Linux toolchain.
A newer local system may legitimately fail the compatibility ABI check.

## macOS

CI uses Apple Silicon with Xcode 26.3, an explicit macOS 15.0 deployment target,
and separate ARM64/x86_64 packages. Intel command-line tests run through Rosetta.

```sh
cmake --preset release -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0
cmake --build --preset release
ctest --preset release
codesign --force --sign - build/release/beachlife.app
python3 tools/package_build.py --build-dir build/release --output-dir build/artifacts
python3 tools/check_package.py build/artifacts/*.tar.gz
```

Use `x86_64` for the Intel build. A package contains `beachlife.app` and a command-
line wrapper. Ad-hoc signing enables the bundle's local signature checks; it is
not Developer ID signing or notarization.

## Windows

Release packages follow the reference port's Linux-hosted LLVM-MinGW route.
`tools/windows-toolchain.json` fixes the compiler archive and SHA256. On Linux:

```sh
python3 tools/setup_windows_toolchain.py build/toolchain
cmake --preset release -B build/windows \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/llvm-mingw-x86_64.cmake \
  -DLLVM_MINGW_ROOT="$PWD/build/toolchain/llvm-mingw-20261006-ucrt-ubuntu-22.04-x86_64"
cmake --build build/windows
python3 tools/package_build.py --build-dir build/windows --output-dir build/artifacts \
  --toolchain build/toolchain/llvm-mingw-20261006-ucrt-ubuntu-22.04-x86_64
```

The ZIP contains the executable, required LLVM-MinGW runtime DLLs and their
notices. CI inspects imports, then runs the extracted package and Python host
checks on Windows. Do not run cross-compiled CTest executables on Linux without
an explicitly configured execution environment.

Local native MSVC/Ninja builds can use the ordinary presets from an x64 developer
shell. The distributable Windows packager currently requires the pinned
LLVM-MinGW runtime, so use the documented cross build for release packages.

## Tests and scope

The presets also build `beachlife-data` and its generated ISO, cabinet and
prepared-content fixtures. See [DATA](DATA.md) for headless preparation,
verification, VFS reads and the launcher/runtime API. No retail inputs are
needed for these tests. Windows CI runs the data checks in a separate test
artifact; product packages still contain the launcher/runtime executable.

CTest checks help/version/build-info, settings persistence, source errors and
a window smoke that renders all six pages with SDL's dummy video driver and
software renderer. Package smoke runs the same window check after extraction,
proving that the artwork, icon and fonts travel with it. With a usable GPU,
package smoke also opens the game window, prepares generated data, checks
offline reuse and repair, verifies production rejection of unsupported
configuration, and exercises the image preview. A separate generated runtime
fixture uses the same launcher/runtime libraries and loading path with an
internal configuration factory to test the successful game loop. Without a
usable GPU, the checks ensure its error occurs before any cache or
readiness is created and reports the skipped game-loading integration. The
`launcher_content` test covers cancellation, source switches and mount lifetime;
`startup_work` covers the order, progress, cancellation and errors of the work
behind the game window's loading screen. `content_startup` runs its real
check/prepare/mount/read worker without a GPU, including cancelled extraction,
resumption, binding persistence, offline reuse and repair. It also checks the
joined result through the actual thread-local file bridge: worker isolation,
nested binding restoration, cancellation/failure, retained handles and teardown.
The production call site constructs its configuration owner only after successful
normal-Play work and an explicit join, before the loading fade. Headless coverage
does not establish GPU presentation or a complete reconstructed startup driver.
Generated fixtures cover
pins, patch ordering, tampering, version changes, archive payloads, provenance,
ABI bounds and release decisions.
Run the Python checks alone with:

```sh
python3 -B -m unittest discover -s tests -p 'test_*.py'
```

For an actual desktop screenshot (then exit), use:

```sh
./build/debug/beachlife --screenshot build/launcher.png --config build/preview.cfg
```

`--page files|display|appearance|advanced|about` selects another page. `--source path.iso`
preselects a disc image without saving it automatically. Screenshot and smoke
runs never check for updates; `--update-preview STATE` shows a made-up update
state instead (`uptodate`, `checking`, `available`, `test`, `back`, `blocked`,
`downloading`, `failed` or `installfailed`).

`--play` opens the game window with saved settings (or `--config` and `--source`),
then prepares or reuses game data behind its loading screen, as Play does. `--cache-directory folder`
overrides the user cache for development. `--frames N` stops after N frames, and
`--screenshot file.png` then saves the last one. The `game_startup` and
`game_runtime` integration tests exercise both the production input-rejection
path and the positive generated runtime fixture. Preview checks still use the
production binary. GPU checks are skipped where no display or usable device
exists; the headless owner and allocation tests run independently.

View a supported original TGA through the same worker and VFS:

```sh
./build/debug/beachlife --play --preview-screen Data/Screens/FrontScreen.tga
```

The selected ISO or prepared cache supplies the image. This explicit preview
also appears as **Preview title screen** on the launcher's Game files page;
it does not execute a reconstructed menu. `tga_images` tests generated decoder
fixtures, `content_startup` checks image progress/cancellation and offline mount
lifetime, and `game_runtime` presents a generated image when a GPU is available.

The `launcher_updates` test writes packages the way `tools/package_build.py`
does, plus damaged and hostile archives, and checks the update rules,
unpacking, installation, rollback and the completion of an interrupted update
without network access.

The launcher prefers SDL's DX12 renderer on Windows, Metal on macOS and Vulkan
on Linux, with a software fallback. Hosted software smoke does not qualify GPU
hardware or reconstructed gameplay. The pipeline does not download or
execute original game code. See [RELEASES](RELEASES.md).

Windows embeds `assets/app/icon.ico` using the native resource compiler. All
windows load the matching PNG from package resources. macOS builds the ICNS
bundle resource using the system `sips` and `iconutil`; these tools are required
on the macOS build host. No original ISO is needed to build the app icons.

The optional `reconstructed_screen_placements_tests` target links one eleven-source
archive containing the actual saved-placement node builder, insertion, writer,
manager save wrapper, lookup, iterators and pool methods. Generated caller-owned
records retain the
list/query checks for duplicate signed groups, full-width coordinates, unchanged
miss outputs, read-only graphs, node identity, same-type slot reuse and repeated
cleanup. The writer additionally creates real 12-byte records and updates only
the first matching record, including owned/borrowed duplicates and identical
writes. Literal expected allocation sequences distinguish record storage from
node-pool storage and require updates to allocate nothing. Generated teardown
returns nodes, deletes owned records once and leaves borrowed records alive.
Additional generated manager/game prefixes exercise both mode-6 suppression
paths and route saves through the actual game global to two live stores. Literal
coordinates/groups and whole-object guards check first-match updates, untouched
rectangle right/bottom fields, other-store preservation and allocation-free
suppressed or identical saves. The original list/writer scenarios stay intact;
direct wrapper calls are counted separately from their nested writer behavior.
The native profile preserves the eight-byte pool quantum while giving each
host-sized node and free link its actual C++ lifetime. Test-only paired scalar
allocation and deterministic lock providers cover successful, uncontended use.
This does not provide the original manager/game/store/list constructor or
destructor, allocation-failure behavior, concurrency, cross-type slot reuse,
Dialog movement or production linkage.

The optional `reconstructed_gui_integer_tests` target exercises the actual GUI
integer argument reader with generated bytes and guarded live prefixes. Literal
results and constructed-length oracles cover both delimiter flags, repeated
calls, whitespace, and the original arithmetic treatment of malformed signs,
interior control bytes and high-bit bytes. The whole reader, output, input and
borrowed manager prefixes are checked for unintended writes. All reached bytes
are accessible and every signed intermediate stays representable; missing
bounds checks and arithmetic quirks are preserved. This qualifies the bounded
reader, not original parser/manager lifetimes, widget construction or gameplay.


The optional `reconstructed_gui_whitespace_tests` target selects the real
whitespace and integer helpers from that same archive. Constructed run lengths,
all 256 boundary-byte values, repeated calls and literal sequential inputs check
cursor-only whitespace writes and the integer handoff. Four whitespace bytes
advance; the other 252 values, including NUL and high bytes, remain unconsumed.
The existing integer fixture and its expectations remain unchanged. Both tests
use the same prepared parser declaration and guarded live prefixes, without
supplying original parser/manager lifetimes or qualifying GUI construction.


The optional `reconstructed_gui_text_tests` target selects the actual text,
whitespace and integer readers from the same archive. Literal bytes, output
terminators and cursor positions cover text stops, whitespace, optional delimiter
consumption, repeated calls and sequential reader handoffs. Embedded NULs and
high bytes are compared as bytes; shorter results must preserve the old output
tail. Whole-object and buffer guards check that the readers change only their
specified outputs and cursor. Inputs and outputs are disjoint, every scanned
byte is accessible, and output capacity is sufficient. The existing integer and
whitespace fixtures remain unchanged. This covers bounded reader behavior;
original parser/manager lifetimes, unbounded or overlapping buffers, widget
construction and gameplay remain outside this target.


The optional `reconstructed_gui_rectangle_tests` target exercises the actual
rectangle reader, whitespace helper, rectangle getter and base screen lifetime
with generated inputs. Literal results cover flag ordering, unsigned centering,
optional parent bounds, degenerate parents and cursor/delimiter quirks. Three
separate fatal cases use the real fatal reporter and host C runtime; an exit
observer checks output, cursor, guards and the absence of a normal return. The
normal suite also rejects an unexpected early successful exit. The integer,
whitespace and text fixtures remain unchanged and use the same prepared parser
declaration. Inputs keep every reached byte and signed intermediate valid.
These bounded cases do not supply original parser, renderer or derived-widget
lifetimes, validate the diagnostic text, or establish gameplay.

With `BEACHLIFE_ENABLE_EXPERIMENTAL_GAME=ON` (default OFF), the optional
`reconstructed_gui_context_tests` target exercises the reconstructed context
push and child-link lifetime with generated live screen items. Literal histories
cover empty and existing chains, duplicate/null contexts, allocation-time field
reloads and two labelled host allocation exceptions. The executable's scoped
scalar allocator uses guarded real storage. A separate native portability patch
preserves the original post-allocation context reload while retaining both ordinary
new expressions. Cleanup clears every borrowed payload before the actual link
destructor runs, then ends the real generated/base item lifetimes.
Whole-state and storage guards check unrelated data. The source is exported through
the `native-experimental` profile; its compiler comparison remains a partial match
and is separate from the strict matching registry. This bounded target does
not establish owning-payload deletion, context pop, original allocation-failure
semantics, full parser/derived-screen lifetimes or gameplay.

```sh
cmake --preset release -B build/experimental-context -DBEACHLIFE_BUILD_RECONSTRUCTED_TESTS=ON -DBEACHLIFE_ENABLE_EXPERIMENTAL_GAME=ON
cmake --build build/experimental-context --target reconstructed_gui_context_tests
ctest --test-dir build/experimental-context -R '^reconstructed_gui_context$' --output-on-failure
```

With `BEACHLIFE_ENABLE_EXPERIMENTAL_GAME=ON`, the existing reconstructed TGA
archive also includes the allocating image constructor and complete buffer parser.
`reconstructed_tga_parser_tests` checks generated raw/RLE indexed, RGB and RGBA
images, including an 800×600 indexed screen. Literal outputs cover row order,
channel conversion, palette tails, alpha, origin handling, zero dimensions and
unsupported-type ownership quirks. Test-only allocation tracking observes the
original leaks before reclaiming its own storage; the existing helper fixture
remains unchanged. The constructor's partial compiler match remains separate
from these behavioral tests.

The raw parser has no input-length argument. These tests supply complete bounded
buffers and representable sizes; truncated data, oversized palette writes,
RLE packet overruns and signed arithmetic overflow are outside its safe domain.
This target does not implement filename/VFS loading, renderer palette conversion,
widget activation or a game frame.

```sh
cmake --preset release -B build/experimental-tga -DBEACHLIFE_BUILD_RECONSTRUCTED_TESTS=ON -DBEACHLIFE_ENABLE_EXPERIMENTAL_GAME=ON
cmake --build build/experimental-tga --target reconstructed_tga_tests reconstructed_tga_parser_tests
ctest --test-dir build/experimental-tga -R '^reconstructed_tga(_parser)?$' --output-on-failure
```

With experimental source enabled, `reconstructed_tga_files_tests` loads generated
TGA files through the actual folder VFS and reconstructed file, parser and image
cleanup methods. Nine calls cover an unbound session, missing and empty files,
header rejection, case/separator aliases and small indexed/RGB raw and RLE images.
The fixture checks that the temporary file array is released before returned
pixels are used, including normal parser rejection. Its paired array allocator
observes real host storage; the retail methods and VFS are not replaced.

These cases require stable complete files and bounded parser inputs. They do not
qualify short reads, malformed files, the original input leak when parsing throws,
renderer palette conversion, widget activation or a game frame.

```sh
cmake --preset release -B build/experimental-tga-files -DBEACHLIFE_BUILD_RECONSTRUCTED_TESTS=ON -DBEACHLIFE_ENABLE_EXPERIMENTAL_GAME=ON
cmake --build build/experimental-tga-files --target reconstructed_tga_files_tests
ctest --test-dir build/experimental-tga-files -R '^reconstructed_tga_files$' --output-on-failure
```

With `BEACHLIFE_ENABLE_EXPERIMENTAL_GAME=ON`,
`reconstructed_indexed_palette_blit_tests` exercises the reconstructed indexed
palette blit through its own one-source archive. Source rows use a byte stride;
destination rows use four-byte slots, and only the first halfword of each slot
is written. The generated fixture checks literal palette words, all 256 indices,
unequal strides and origins, zero extents, and preservation of every other buffer
word and guard. Palette index zero is copied normally.

The tested domain uses separate live buffers, bounded nonnegative dimensions,
positive strides and representable integer arithmetic. This subsystem does not
connect renderer storage or GPU presentation. Its partial compiler comparison
remains separate from native behavior qualification.

```sh
cmake --preset release -B build/experimental-indexed-blit -DBEACHLIFE_BUILD_RECONSTRUCTED_TESTS=ON -DBEACHLIFE_ENABLE_EXPERIMENTAL_GAME=ON
cmake --build build/experimental-indexed-blit --target reconstructed_indexed_palette_blit_tests
ctest --test-dir build/experimental-indexed-blit -R '^reconstructed_indexed_palette_blit$' --output-on-failure
```

With experimental source enabled, `reconstructed_gui_pop_tests` exercises context
pop together with the real context push, child-link lifetime and generated items
using the reconstructed base lifetime. Twelve literal scenarios cover empty,
single and multiple history links, repeated/null borrowed payloads, and push/pop
round trips. Whole-state and allocation guards check published pointers,
surviving links and borrowed-item survival at deallocation. The existing context
push fixture remains unchanged.

Two separate fatal cases call the real `StartupFatal` and CRT termination route.
An exit observer verifies preserved state and fixture cleanup; returning from
that branch fails the test. These bounded finite histories do not establish
arbitrary pointer/cycle behavior, concurrent mutation, full parser/derived-screen
lifetimes or gameplay. The method's partial compiler comparison remains separate
from experimental native behavior qualification.

```sh
cmake --preset release -B build/experimental-context-pop -DBEACHLIFE_BUILD_RECONSTRUCTED_TESTS=ON -DBEACHLIFE_ENABLE_EXPERIMENTAL_GAME=ON
cmake --build build/experimental-context-pop --target reconstructed_gui_pop_tests
ctest --test-dir build/experimental-context-pop -R '^reconstructed_gui_pop($|_fatal_)' --output-on-failure
```

With experimental reconstructed tests enabled, `reconstructed_tga_palette_tests`
exercises the actual image constructor, RGB555/RGB565 palette conversion and image
destructor from the shared TGA archive. Its generated cases cover literal colors,
all channel byte values, mixed palettes, zero/nonzero flag selection, guarded
storage, repeated conversion and host allocation exceptions. The conversion
always allocates 256 halfwords and replaces the old converted-palette pointer.
The fixture observes the original overwrite leak and reclaims that old array
after the actual image destructor releases its current buffers.

Build with `cmake --build build/release --target reconstructed_tga_palette_tests`
and run `ctest --test-dir build/release -R '^reconstructed_tga_palette$' --output-on-failure`.
Both `BEACHLIFE_BUILD_RECONSTRUCTED_TESTS=ON` and
`BEACHLIFE_ENABLE_EXPERIMENTAL_GAME=ON` are required. These checks use generated
images and a paired host array-allocation provider; renderer integration and
original allocation-failure behavior require separate qualification.

With experimental reconstructed tests enabled,
`reconstructed_gui_registration_tests` exercises the actual registration method
and shared child-link and screen-item lifetimes. Literal cases cover named and
unnamed insertion into two distinct lists, append order, repeated borrowed
payloads, callback priority, receiver aliases and the final word read after a
callback. Generated callbacks observe the real virtual interface; they do not
implement missing retail attachment behavior.

The fixture preserves the observed replacement behavior when a live list head
has a null tail. It retains those orphaned links, clears all borrowed payloads,
and calls the actual link destructor and paired host deletion before ending item
lifetimes. These finite successful-allocation cases do not establish arbitrary
graph safety, allocation or callback failure handling, full parser ownership,
real attachment behavior or gameplay. Existing push and pop fixtures are unchanged.

Build with `cmake --build build/experimental-context --target reconstructed_gui_registration_tests`
and run `ctest --test-dir build/experimental-context -R '^reconstructed_gui_registration$' --output-on-failure`.
Both `BEACHLIFE_BUILD_RECONSTRUCTED_TESTS=ON` and
`BEACHLIFE_ENABLE_EXPERIMENTAL_GAME=ON` are required.

With experimental reconstructed tests enabled,
`reconstructed_renderer_blit_kernels_tests` exercises the transparent, packed and
masked indexed blitters through one three-source archive. All three skip zero
indices. Transparent and masked output preserve the upper halfword of each
four-byte destination slot; packed output uses adjacent halfwords, rounds its
byte pitch down to a halfword count, and ignores destination Y.

Twenty-two literal cases check palette words, mask bits, padded and overlapping
rows, zero extents and a live palette/output alias in each kernel. Whole typed
buffers and guards preserve every unlisted element. The tested arithmetic and
array spans are bounded; negative extents, arithmetic overflow, arbitrary
cross-type aliasing and renderer/GPU integration are separate. The existing
nontransparent indexed-blit target and source remain unchanged.

```sh
cmake --preset release -B build/experimental-blit-kernels -DBEACHLIFE_BUILD_RECONSTRUCTED_TESTS=ON -DBEACHLIFE_ENABLE_EXPERIMENTAL_GAME=ON
cmake --build build/experimental-blit-kernels --target reconstructed_renderer_blit_kernels_tests
ctest --test-dir build/experimental-blit-kernels -R '^reconstructed_renderer_blit_kernels$' --output-on-failure
```

With experimental reconstructed tests enabled,
`reconstructed_screen_activation_tests` exercises the real item activation,
manager notification and auxiliary clear methods with the reconstructed item
constructor and destructor. Literal cases and all 256 activation bytes check
routing, exact byte values, borrowed peer lifetimes and preserved object fields.
The generated manager and auxiliary prefixes do not qualify their complete
retail construction or widget/Intro lifetimes.

The affected item, registry, placement and GUI tests also consume this coherent
experimental declaration. Default native tests retain their existing declaration
and use the same fixtures through a test profile definition.

```sh
cmake --preset release -B build/experimental-activation -DBEACHLIFE_BUILD_RECONSTRUCTED_TESTS=ON -DBEACHLIFE_ENABLE_EXPERIMENTAL_GAME=ON
cmake --build build/experimental-activation --target reconstructed_screen_activation_tests
ctest --test-dir build/experimental-activation -R '^reconstructed_screen_activation$' --output-on-failure
```

With the experimental option enabled, `reconstructed_renderer_rectangle_queue`
exercises the reconstructed dirty-rectangle accumulator using a native
`std::deque` for value storage. Generated cases cover containment, swap-last
removal, first-overlap and recursive append order, edge contacts and the
original missed-overlap behavior. The surface and GUI rectangle tests use typed
renderer snapshots for this profile. These tests cover valid queues and
successful host allocation; original allocator, failure, locking and timing
behavior are not reproduced by the storage adaptation.

The experimental `reconstructed_renderer_clip` test exercises the real clipping
method followed by the native dirty-rectangle accumulator. Literal cases cover
the three defined scale modes, the active unscaled override, signed coordinates,
logical half extents, edge equality, mutable argument aliases and ordered queue
effects. It uses live native queue storage and preserves the original clipping
and rectangle-partition behavior. Unsupported modes with the unscaled flag clear,
overflow, allocation failure and full renderer/surface lifetime remain outside
this bounded test; it does not present an image.

The experimental `reconstructed_renderer_tga_draw_tests` target composes the
complete TGA drawing method with the real clipping/rectangle queue, four indexed
blitters, surface pixel access and allocated TGA image lifetime. Generated
literal cases cover pixels, clipping side effects and live COM Lock/Unlock
selection. This headless subsystem test does not establish surface creation,
GPU presentation or the original Intro screen.


The experimental `reconstructed_renderer_rectangle_fill_tests` target checks the
complete rectangle-fill method with real surface pixel access and error handling.
Generated cases cover full-buffer clipping, preserved upper pixel bits, dense
16-bit pairs and odd tails, empty spans, and live COM Lock/Unlock state changes.
Its guarded typed renderer includes the real native rectangle queue. This
headless test does not establish surface creation, GPU presentation or the
original Intro screen.


The experimental `reconstructed_frame_fill_styles_tests` target checks the real
colour converter and Frame drawing helpers for styles 2, 3 and 7 with actual
rectangle filling. Generated cases preserve the wide-red conversion quirk,
border overlap, clipping, captured renderer and live bounds during COM callbacks.
The fixture uses the existing renderer global and real native queue storage;
it does not construct a complete Frame or claim GPU presentation.


With `BEACHLIFE_ENABLE_EXPERIMENTAL_GAME=ON`, the optional
`reconstructed_font_measurement_tests` target exercises the reconstructed font
renderer constructor and its three text measurement methods. Generated typed
data covers kerning, wrapping, unsigned arithmetic, a valid text/data alias and
preservation of borrowed state. The bounded cases exclude nonterminating input
and scratch copies without room for a terminator. This does not implement font
loading, full font ownership or destruction, or screen text rendering.


The experimental `reconstructed_renderer_line_tests` target exercises all four
line rasterizers, the renderer line method and Frame style 6 using guarded pixel
buffers. Literal cases preserve direct-pixel upper bits, packed edge quirks,
endpoint and clipping behavior, and live COM/renderer state changes. The fixture
uses the actual colour converter, pixel access and renderer global. It does not
construct a complete Frame or renderer or claim GPU presentation.

With `BEACHLIFE_ENABLE_EXPERIMENTAL_GAME=ON`, the optional
`reconstructed_screen_localization_tests` target exercises the reconstructed
localization lookup, iterator result, and empty cleanup methods. Generated
borrowed arrays cover pointer identity, duplicate keys, skipped buckets, wraparound,
null queries and null text results. Windows uses the CRT `_stricmp`; other
supported hosts use `strcasecmp`, with comparison cases restricted to seven-bit
strings in the C locale. High-byte cases exercise hashing with every bucket
unavailable and do not claim wider locale parity. The target does not implement
localization loading, full ownership or destruction, or text drawing.

With `BEACHLIFE_ENABLE_EXPERIMENTAL_GAME=ON`, build
`reconstructed_auxiliary_text_tests` and run `ctest -R '^reconstructed_auxiliary_text$'`.
The generated cases exercise the reconstructed auxiliary `SetText` method with
the real localization lookup, font constructors and measurement methods. They
check borrowed text identity, the unsigned 160-unit branch, wrapping and metric
arithmetic using guarded typed data. The experimental manager declaration uses
the observed ten-pointer font table; its existing fixture consumers retain
their behavioral cases with typed initialization of the refined fields.

The option remains off by default. This target does not supply localization/font
loading, complete manager/auxiliary/font ownership, Frame lifetime or text drawing.
Malformed glyph indices, nonterminating wrapping inputs and unsafe scratch-copy
lengths remain outside its bounded domain. It does not start the original menu.

The experimental profile supplies the 21 observed base `ScreenItem` slots from
reconstructed source. `reconstructed_screen_item_complete_tests` exercises real
base lifetimes, byte states, hit tests, owned-text copies and finite timed
notifications through the actual manager, lookup and font methods. Its borrowed
auxiliary state takes the real Position early return. It constructs no Frame;
active Frame positioning and complete original subtype lifetimes remain separate.
Repeated text assignment retains the original leak, with test-owned receipts
reclaiming only the abandoned allocations afterward. Existing generated subtype
fixtures keep their behavior checks under the common experimental declaration.

```sh
cmake --build build/native-experimental --target reconstructed_screen_item_complete_tests
ctest --test-dir build/native-experimental -R '^reconstructed_screen_item_complete$' --output-on-failure
```

The experimental `reconstructed_tga_widget` test constructs a real TGA screen
item, loads generated indexed images through the mounted folder VFS, converts
their palettes and draws through the reconstructed renderer and rectangle
queue. It also checks cached activation, disabling and actual derived/base
destruction. Its bounded inputs exclude null-load dereferences, oversized paths
and unsupported image palettes. The guarded array observer checks cleanup and
one labelled host allocation failure; it does not reproduce the original CRT
failure contract. This is headless subsystem coverage, not the Intro or menu.

With `BEACHLIFE_ENABLE_EXPERIMENTAL_GAME=ON`, build
`reconstructed_resource_frame_metadata_tests` and run
`ctest -R '^reconstructed_resource_frame_metadata$'`. Generated binary streams
exercise the reconstructed byte/word/dword readers and resource-frame metadata
with the real constructor and destructor. Cases check nonterminated and embedded
NUL names, repeated reads, exact allocation/deletion ownership, untouched fields
and guarded storage. The fixture preserves the original overwritten-name leak
and releases its saved orphan separately after the actual frame lifetime.

The bounded domain requires complete reads, little-endian representation and
the observed scalar widths. Short reads, full resource-file ownership, encoded
frame loading and resource rendering remain separate. This optional headless
target does not start the original Intro or qualify the GUI FrameItem lifetime.

The experimental `reconstructed_resource_find_tests` target exercises resource
lookup through the actual Find, Lookup, wrapper constructor and destructor.
Generated borrowed records cover first-match lookup, C-locale ASCII case folding,
fallback, empty names, shared-record identity and unsigned reference-count wrap.
Owned wrapper/name storage is released through the real destructor and C++
allocation boundary. This does not implement the resource file producer, full
record ownership, Frame loading or menu presentation.
