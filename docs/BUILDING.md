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

## Reconstructed subsystem tests

The optional targets compile reconstructed image and file helpers using the
native compiler. They consume a committed source export with faithful patches
through the same preparation tool as dependencies. Enable them explicitly:

```sh
cmake --preset release -B build/reconstructed-tga \
  -DBEACHLIFE_BUILD_RECONSTRUCTED_TESTS=ON
cmake --build build/reconstructed-tga \
  --target reconstructed_tga_tests reconstructed_files_tests --parallel
ctest --test-dir build/reconstructed-tga -R '^reconstructed_(tga|files)$' --output-on-failure
```

`BUILD_TESTING` must be enabled. `patches/beachlife-decomp/source.json` owns the
full baseline commit pin shared with matching; that object must exist locally,
so shallow checkouts may need more history before configuration. Configuration
never fetches it. New reconstructed increments are patches listed in
`reconstruction-series`; native portability patches follow in `series`.
Keep the baseline intact. Uncommitted edits under `decomp/` are rejected;
unrelated host edits do not affect the export.

Configuration prepares `build/reconstructed-tga/prepared/beachlife-decomp/`
and its adjacent JSON manifest. Every library build, including an otherwise
up-to-date build, verifies the revision, source hashes, ordered patch series
and prepared files. Update reconstruction or portability patches and reconfigure; never edit prepared
files. The matching-only VC6 release wrapper is excluded in favor of the host
CRT. The default build leaves these targets disabled.

The generated fixtures check palette/pixel offsets, channel conversion, row
orientation, raw and run-length packets, empty images and buffer ownership.
They require no ISO or extracted data. The target is a native test executable;
the production launcher still uses its existing decoder. A passing test does
not establish a complete TGA parser, original ABI matching or gameplay.

The file target runs the matched size, plain-file and BLE loaders through an
explicit VFS session. Its portability patch preserves 32-bit size/count behavior,
the period terminator and signed byte inversion on 64-bit hosts. Scoped bindings
belong to one thread, restore nested sessions and keep open handles' mounts alive.
Reads use the supplied `FileSource`; they never fall back to the process directory
or reopen the ISO. Generated tests cover real folder reads, aliases, short reads,
stdio error results, seek/position behavior, ownership and repeated sessions.
`BEACHLIFE_RECONSTRUCTED_FIXTURE_ROOT` selects the generated fixture directory;
it defaults to the build tree. For a Windows cross-build run through Wine, use
a path on its C: drive if Wine exposes the Unix build path as a reparse point.

The unchecked retail loaders require stable files with a successful second open
and sizes from 1 through `INT32_MAX - 1`; BLE inversion additionally requires a
complete read. Tests cover empty/missing files and size-only 32-bit truncation
separately, without exercising invalid allocation/index domains or reading
uninitialized tails. This target qualifies file services for reconstructed
startup; it does not connect a complete game driver to Play.

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
offline reuse and repair, and runs the hidden loop including the startup VFS
read. Without one, it checks that the GPU error occurs before any cache or
readiness is created and reports the skipped game-loading integration. The
`launcher_content` test covers cancellation, source switches and mount lifetime;
`startup_work` covers the order, progress, cancellation and errors of the work
behind the game window's loading screen. `content_startup` runs its real
check/prepare/mount/read worker without a GPU, including cancelled extraction,
resumption, binding persistence, offline reuse and repair.
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
`game_runtime` integration tests run it hidden on a generated ISO and are skipped
where no display or GPU device exists, as on hosted CI runners.

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
