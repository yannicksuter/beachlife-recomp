# Beach Life recomp

**Beach Life** (Deep Red Games / Eidos, 2002), known in North America as
**Virtual Resort: Spring Break**, recompiled as a native program for Windows,
Linux and macOS.

This repository compiles the reconstructed source of the retail game from
[beachlife-decomp](../beachlife-decomp) with modern compilers and supplies what
Windows and DirectX used to provide: window, graphics, sound, input and file
access. "Recomp" here means recompiling that reconstructed C++ source. It is
not a static recompilation that machine-translates the original executable,
and not a wrapper around it: the original executable never runs.

> [!IMPORTANT]
> **No original assets are part of this repository.** It contains no
> executables, DLLs, disc images, game data or extracted assets of the game,
> and none will ever be committed. Builds and releases of the port contain
> none either.
>
> **You need an original copy of the game**, Beach Life or Virtual Resort:
> Spring Break on CD. The port reads it from your installed game folder or
> from a dump of your CD. For development, put it in `orig/`, which Git
> ignores (see [orig/README.md](orig/README.md)).

## Status

Not started. The port begins once the decompilation is complete, apart from
the parts that do not depend on the game code (see
[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)). Plan: the
[decomp roadmap](../beachlife-decomp/docs/ROADMAP.md), phases 5 and 6.

## Layout

```
extern/       pinned dependencies as Git submodules: beachlife-decomp, SDL3, ...
patches/      every change to pinned source, as reviewed patch series
  beachlife-decomp/          faithful series: modern compiler, 64-bit, native platform
  beachlife-decomp-tweaks/   intended changes: widescreen, fixes, remaster assets
src/
  bootstrap/  program entry, settings, command line
  launcher/   the launcher: find the game data, settings, Play
  platform/   native replacements for Win32, DirectX, audio, input and files
  runtime/    starts the game module and hands over to the original WinMain
tools/        build helpers, e.g. preparing the patched source
assets/       the port's own files (icons, launcher art), never game data
docs/         architecture and, later, building and installing
```

## What belongs where

- **The retail game's code** lives only in beachlife-decomp. This repository
  never contains a copy of it, only patches against a pinned commit.
- **Launcher and platform code** live only here. Nothing in this repository
  is compiled with the original MSVC or compared against the retail binary.
