# Beach Life recomp

A native C++ port of **Beach Life** (Deep Red Games / Eidos, 2002), known in
North America as **Virtual Resort: Spring Break**, for Windows, Linux and macOS.

The game will be compiled from reconstructed source in
[beachlife-decomp](../beachlife-decomp). This repository supplies the new
launcher, runtime and native platform services. The original executable never
runs inside the port.

## Current status

The native bootstrap and build/release pipeline are implemented. The executable
supports `--help`, `--version` and `--build-info`. It does not yet load game data,
open a launcher window or run the game. Decompilation and native development
advance alongside one another.

The intended graphics backends are DX12/Vulkan on Windows, Metal on macOS, and
Vulkan on Linux. SDL3 and native Dawn integration are upcoming; the bootstrap
currently has no graphics dependencies.

## Build

Requires CMake 3.25+, Ninja, Python 3.10+, Git and a C++20 compiler:

```sh
cmake --workflow --preset debug
./build/debug/beachlife --version
```

See [BUILDING](docs/BUILDING.md) for release builds, macOS and Windows.

## Binaries and releases

PRs to `main` and `stable` produce native packages for Windows x86_64, Linux
x86_64, Steam Deck, macOS ARM64 and macOS x86_64. `main` is the development
branch; promotion to `stable` goes through a tested `main` → `stable` PR.
The repository remains private, and downloads require repository access.

Promotion PRs receive temporary test prereleases. Merging builds the stable
commit and creates a draft release candidate for the version in
`CMakeLists.txt`. The owner reviews and publishes the draft. Existing versions
are preserved; raise the CMake version for another candidate.

Read [RELEASES](docs/RELEASES.md) for the complete process and
[INSTALL](docs/INSTALL.md) for package contents and startup.

## Original game files

No original executables, DLLs, disc images, game data or extracted assets are
committed or included in packages. The completed port will require the user's
own game data. Development inputs belong under ignored `orig/` or outside the
repository; see [orig/README.md](orig/README.md). CI uses generated fixtures and
builds without game files.

## Layout

```text
CMakeLists.txt        native targets and the sole release version
CMakePresets.json     shared local/CI configure, build and test presets
cmake/               platform configuration and toolchains
src/bootstrap/       native entry point and command line
src/launcher/        future source picker and launcher
src/platform/        future native graphics, audio, input and file services
src/runtime/         future reconstructed-game integration
extern/              future pinned source dependencies
patches/             faithful and optional tweak patch series
assets/              port-owned resources only
tools/               versioning, packaging and release helpers
tests/               generated-fixture and native startup checks
docs/                architecture, building, installation and releases
```

Retail code lives only in decomp. The port will consume immutable source pins
and apply patches to exported build trees. New host code lives here. See
[ARCHITECTURE](docs/ARCHITECTURE.md) and [patch rules](patches/README.md).
