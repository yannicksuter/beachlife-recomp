# Source patches

The port never edits pinned source directly. Every change to it is a patch in
this folder, applied in the order of each folder's `series` file to a copy of
the pinned source during the build. Each patch explains its reason in its
header.

- `beachlife-decomp/`: the faithful series. Only adapts the retail code to a
  modern compiler, 64-bit and the native platform.
- `beachlife-decomp-tweaks/`: every intended change. Each one optional, off by
  default, and taking the original path when off.

Other dependencies get their own folder here if they ever need changes.

The initial bootstrap has no decomp or graphics dependencies, so these series
are currently empty and no retail source is compiled. Introduce a dependency
through an immutable pin and prepared export when its first usable target is
added. Native packages must record the pin and applied patch identities in
their source manifest. See [architecture](../docs/ARCHITECTURE.md) and
[release packaging](../docs/RELEASES.md).
