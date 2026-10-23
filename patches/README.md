# Source patches

New reconstructed retail methods and changes belong in `beachlife-decomp/`.
The existing `decomp/` tree is a frozen baseline; matching and native builds
consume prepared exports without editing it or dependency checkouts.
`src/` contains new host code only.

`beachlife-decomp/source.json` pins the full local baseline commit. Both export
profiles apply `reconstruction-series` first, preserving original behavior and
compiler/ABI declarations. These patches can add methods, headers, target
metadata and comparison-fixture references. Each completed method has its own
patch and commit, with strict comparison and a changed-behavior control.

The default `native` profile then applies `beachlife-decomp/series` for faithful
modern-compiler, 64-bit and platform adaptation. The `matching` profile omits
that portability series. Intentional behavior changes belong in
`beachlife-decomp-tweaks/`; they are optional, off by default, and excluded from
both profiles. Preserve retail timing and bugs in faithful patches.

The `native-game` profile adds `game-series` last: the boot build's merged game
tree, written by `tools/game_tree.py patches` (see BUILDING, "Boot build").

Patch paths are relative to the exported baseline root, for example
`src/file_loader.cpp`. Format the reconstructed source before generating its
patch. The comparison retention fixture is never executed and is not a game
driver. Third-party dependencies keep their own `patches/<name>/series` when
needed; their preparation is unchanged.

## Prepare and verify

The baseline commit must exist locally. Configuration never downloads it.
The matching command reads the shared pin and prepares the matching profile:

```sh
python3 tools/matching.py game --compiler /path/to/msvc6.4 \
  --reccmp build/matching/venv/bin/reccmp-reccmp
```

The [native subsystem targets](../docs/BUILDING.md#reconstructed-subsystem-tests)
read the same pin and select the native profile. For an explicit export, use the
full revision from `source.json`:

```sh
DECOMP_REVISION=3ca8a928a6cd71327d687189448f72d9e9b3dc07
python3 tools/prepare_sources.py --output build/native-decomp/prepared \
  --decomp-only --decomp-revision "$DECOMP_REVISION" --decomp-profile native
python3 tools/prepare_sources.py --output build/native-decomp/prepared \
  --decomp-only --decomp-revision "$DECOMP_REVISION" --decomp-profile native --verify
```

Use a separate output with `--decomp-profile matching` to inspect original-ABI
source. Output must be under `build/` or outside the source repository. The
`beachlife-decomp/` output has an adjacent JSON manifest containing the baseline
commit/tree, original file hashes, profile, ordered applicable lists and patch
hashes, and final file hashes. Matching results fingerprint these inputs and
reverify the export after comparison.

Uncommitted edits under `decomp/` are rejected; unrelated host edits are allowed.
Ignored files are never exported and Git replacement refs are ignored. Verify
reconstructs the expected patched contents without rewriting the prepared files.
Edited outputs or manifests fail; remove both to regenerate them. A changed pin,
profile or applicable series requires preparation again. No original game data,
executables or compiler binaries belong in patches or source exports.

Ordinary dependency preparation still uses immutable submodule pins. Native
packages record dependency identities in their source manifest. See
[architecture](../docs/ARCHITECTURE.md) and [release packaging](../docs/RELEASES.md).
