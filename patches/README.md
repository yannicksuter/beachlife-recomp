# Source patches

Third-party source remains pinned and unmodified. Faithful retail
reconstruction lives in this repository's `decomp/` directory. Native
adaptations are patches in this folder, applied in each folder's `series`
order to an exported copy during the build. Each patch explains its reason.

- `beachlife-decomp/`: the faithful series. Only adapts the retail code to a
  modern compiler, 64-bit and the native platform.
- `beachlife-decomp-tweaks/`: every intended change. Each one optional, off by
  default, and taking the original path when off.

Other dependencies get their own folder here if they ever need changes.

The launcher builds pinned SDL3, Dear ImGui and stb exports. No patches are
needed for those pins yet, and no retail source is compiled. Configuration
exports clean Git pins into the build tree and applies any dependency series;
each build verifies the prepared files. Native packages record dependency pins
and applied patch identities in their source manifest. See
[architecture](../docs/ARCHITECTURE.md) and
[release packaging](../docs/RELEASES.md).
