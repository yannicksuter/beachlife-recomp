# Pull requests and releases

`main` is development. `stable` is the default release branch and receives
promotions through PRs from this repository's `main`. The repository stays
private. Actions artifacts and releases require repository access.

## One version source

The root `CMakeLists.txt` declares the port's version:

```cmake
project(beachlife_recomp VERSION 0.0.1 LANGUAGES C CXX)
```

This version is independent of the retail game's version. Update it on `main`:

```sh
python3 tools/bump_version.py patch
# Also accepts minor, major or an explicit increasing x.y.z.
```

The pipeline reads this declaration and checks it against every package.
Executable and package identities append `+g<12-character commit>`. Local edits
append `.dirty`; dirty builds cannot become test/release packages. CI does not
bump versions or commit generated version files.

## Development PRs

PRs targeting `main` or `stable` run `.github/workflows/build.yml`. Every PR
builds its exact head SHA and produces these package artifacts:

| Target | Archive | Build/startup validation |
| --- | --- | --- |
| Linux x86_64 | tar.gz | GCC build, CTest, extracted CLI/window smoke |
| Steam Deck x86_64 | tar.gz | GCC build, CTest, runtime ABI bounds, CLI/window smoke |
| Windows x86_64 | ZIP | Pinned LLVM-MinGW build/import checks, CLI/window smoke on Windows |
| macOS ARM64 | tar.gz with app | Native build, CTest, architecture/signature checks, CLI/window smoke |
| macOS x86_64 | tar.gz with app | Cross build, architecture/signature checks, Rosetta CLI/window smoke |

PR artifacts expire after seven days; stable/manual artifacts after fourteen.
A test or draft release retains attached archives independently of that Actions
retention. Fork PRs receive build checks/artifacts without release privileges.
Manual dispatch produces development artifacts and does not publish a release.

## Promote main to stable

1. Synchronize `main` with `stable`, finish the changes and raise the CMake version
   if a new candidate is needed. Commit and push `main`.
2. Open a PR with base `stable` and head `main`. The promotion policy rejects
   other source branches/repositories. Keep the branch up to date with `stable`.
3. Wait for every build/test and `Native build gate`. The gate also checks the
   complete five-package set and each archive's version, SHA, channel and run ID.
4. Actions publishes `test-pr<N>-<sha>` as a prerelease, never the latest release.
   Each successful new head replaces older test builds for that PR. Download and
   inspect the build before merging.
5. Merge using a merge commit. The push to `stable` runs the same matrix again
   at the actual merged SHA; it does not rename the PR artifacts.
6. Actions creates draft `v<version>`, attaches that stable run's packages and
   SHA256SUMS, and generates release notes. Review this candidate, then publish
   the draft when ready. Publishing uses the existing assets without rebuilding.
7. Synchronize development after promotion: fetch, fast-forward `main` to stable
   when possible, and push it. If main has advanced, merge stable into main and
   resolve the changes before the next promotion.

The launcher updates itself from this repository's public release list: the
Release channel reads published `v<version>` releases, the Test channel the
`test-pr<N>-<sha>` prereleases. It downloads the
`beachlife-<version>-<platform>-<arch>` archive for its own package, checks it
against GitHub's SHA-256 digest (or SHA256SUMS) and every file against
SOURCE-MANIFEST.json. Keep these names, SHA256SUMS and the manifest format
stable. Drafts stay invisible to the launcher until they are published.

A candidate is a **draft GitHub release**, not an automatically numbered RC.
Numbered prereleases would need an explicit additional CMake version field and
promotion policy. The owner controls final publication.

Existing tags, published releases and drafts are preserved. Another stable
commit with the same CMake version still builds artifacts but does not replace
the existing candidate. Increase the version for a new candidate.
The pipeline checks the paginated release inventory, including drafts, as well
as existing tags; GitHub's release-by-tag endpoint alone does not find drafts.

`.github/workflows/test-builds.yml` removes only the closed PR's test releases
and tags after close/merge. It shares a publication concurrency group and uses
maintained default-branch code. Publication also rechecks the PR state/head to
avoid leaving a test release from a closed or superseded build.

## Required gates and repository configuration

Protect stable against deletion, force pushes and direct updates. Require a PR,
an up-to-date branch and these checks from GitHub Actions:

- `Promotion policy`
- `linux-x86_64`, `steamdeck-x86_64`, `windows-x86_64`
- `macos-arm64`, `macos-x86_64`
- `Windows package smoke`
- `Native build gate`

Keep the default Actions token read-only. Only publication/cleanup jobs receive
contents-write. The release jobs depend on the complete aggregate gate,
including the Windows execution checks. Preserve repository privacy. A personal
repository uses its available owner/admin roles rather than an organization
team copied from another repository.

## Package contract

Packages contain only declared native outputs, documentation and runtime
notices. SOURCE-MANIFEST.json records build identity, dependency information and
payload hashes. SHA256SUMS covers the release archives. The current launcher
has no game module or bundled retail data. It includes the declared header,
font and dependency notices. Window smoke uses software rendering and requires
no game files or GPU. Future libraries/resources must be
added explicitly to the packaging contract and its checks.

For local reproduction see [BUILDING](BUILDING.md); for startup see
[INSTALL](INSTALL.md). The initial candidate verifies distribution of the
launcher, not a playable game or qualified game graphics backend.
