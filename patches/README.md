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

The same tool can export `decomp/` from a full commit SHA already present in
this repository. For example, the first combined image-helper revision is:

```sh
DECOMP_REVISION=c5ac13dc11dc5cb7b4f35ebdebd188b3efd7fda6
python3 tools/prepare_sources.py --output build/native-decomp/prepared \
  --decomp-only --decomp-revision "$DECOMP_REVISION"
python3 tools/prepare_sources.py --output build/native-decomp/prepared \
  --decomp-only --decomp-revision "$DECOMP_REVISION" --verify
```

The result is `beachlife-decomp/` inside the output directory, with source paths
such as `beachlife-decomp/src/tga_rle.cpp`. Its adjacent `beachlife-decomp.json`
records the commit, subtree ID, committed file hashes, faithful patch series
and hashes, and prepared file hashes. Patch paths are relative to the exported
`decomp/` root. Only `beachlife-decomp/series` is applied; tweaks remain off.
The output must be under `build/` or outside the source repository.

Tracked and untracked edits under `decomp/` must be committed or removed first;
unrelated host edits are allowed. Ignored files are never exported. Selecting
an older commit exports its committed blobs, independently of the checked-out
revision. Git replacement refs are ignored. No original game inputs or compiler
binaries are needed. Verification
requires an existing complete export and reconstructs its expected patched
contents in temporary build storage without rewriting the prepared files.
Edited exports or manifests are rejected; remove both to regenerate. A changed
revision or patch series is stale and requires preparation again.

Without these flags, dependency preparation is unchanged. Omitting
`--decomp-only` adds the local export to the usual dependency preparation. This
command prepares source; native subsystem compilation is a separate target.
