# Pull requests and releases

`main` is development. `stable` is the default release branch and receives
promotions through PRs from this repository's `main`. Published releases and
test prereleases are public so the launcher's updater can retrieve them.
Merging a promotion PR publishes the new version as the latest release.

## One version source

The root `CMakeLists.txt` declares the port's version:

```cmake
project(beachlife_recomp VERSION 0.0.4 LANGUAGES C CXX)
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

Every package uses the optimized `release` preset with
`BEACHLIFE_ENABLE_EXPERIMENTAL_GAME=ON`, `BEACHLIFE_ENABLE_BOOT_GAME=ON` and
`BEACHLIFE_ENABLE_TWEAKS=ON`. The launcher contains the decompiled game and its
optional gameplay settings; both settings remain off until enabled by the player.
The build fails if any generated game trap, host-service trap or unresolved
symbol remains. The package manifest records all three zero counts, and the
Native build gate requires `native-game-tweaks` and zero traps on all five targets.

CTest and extracted-package checks use generated fixtures without retail data.
They enter the real game's parser and verify its rejection of invalid input;
GPU-dependent checks skip cleanly when a runner has no usable GPU. Windows runs
the package smoke and component checks on its native runner. macOS validation is
headless CI coverage only; retail gameplay and Metal presentation still require
a Mac. Local Linux/Windows retail-data route checks complement these CI checks.

PR artifacts expire after seven days; stable/manual artifacts after fourteen.
A test or published release retains attached archives independently of that
Actions retention. Fork PRs receive build checks/artifacts without release privileges.
Manual dispatch produces development artifacts and does not publish a release.

## Promote main to stable

1. Synchronize `main` with `stable`, finish the changes and raise the CMake version
   if a new release is needed. Commit and push `main`.
2. Open a PR with base `stable` and head `main`. The promotion policy rejects
   other source branches/repositories. Keep the branch up to date with `stable`.
3. Wait for every build/test and `Native build gate`. The gate also checks the
   complete five-package set, game/tweaks profile, zero trap counts, and each
   archive's version, SHA, channel and run ID.
4. Actions publishes `test-pr<N>-<sha>` as a prerelease, never the latest release.
   Each successful new head replaces older test builds for that PR. Download and
   inspect the build before merging.
5. Merge using a merge commit. The push to `stable` runs the same matrix again
   at the actual merged SHA; it does not rename the PR artifacts.
6. Actions publishes `v<version>` as the latest release with that stable run's
   packages and SHA256SUMS. The release takes the merged PR's title and body, so
   write them as release notes before merging.
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

Existing tags, published releases and drafts are preserved. Another stable
commit with the same CMake version still builds artifacts but does not replace
the existing release. Increase the version for a new release.
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
including the Windows execution checks. A personal
repository uses its available owner/admin roles rather than an organization
team copied from another repository.

## macOS signing and notarization

The macOS jobs sign `beachlife.app` with `tools/sign_macos.py`. With these
repository secrets the app gets the Developer ID signature with the hardened
runtime, is notarized by Apple and has its ticket stapled; without them (fork
PRs, local builds) it is signed ad hoc as before:

| Secret | Value |
| --- | --- |
| `MACOS_CERTIFICATE` | base64 of the Developer ID Application certificate and key, exported as `.p12` |
| `MACOS_CERTIFICATE_PASSWORD` | the password chosen for that export |
| `MACOS_NOTARY_KEY` | the text of an App Store Connect API key (`AuthKey_<ID>.p8`) |
| `MACOS_NOTARY_KEY_ID` | that key's ID |
| `MACOS_NOTARY_ISSUER` | the issuer ID shown above the App Store Connect key list |

The certificate requires Apple Developer Program membership. Export it from
Keychain Access (My Certificates, the Developer ID Application entry with its
key) and store it with `base64 -i DeveloperID.p12 | gh secret set MACOS_CERTIFICATE`.
Create the API key under App Store Connect → Users and Access → Integrations →
App Store Connect API with Developer access; `gh secret set MACOS_NOTARY_KEY <
AuthKey_<ID>.p8` stores it. Notarization adds a few minutes to each macOS job.

## Package contract

Packages contain only declared native outputs, documentation, launcher resources
and runtime notices. The decompiled game is linked into the launcher executable;
no original executable, DLL, disc image, retail gameplay data, private reports or
local test profiles enter an archive. Playing requires the user's prepared ISO
data. SOURCE-MANIFEST.json records build identity, dependency and patch identities,
zero trap counts and payload hashes. SHA256SUMS covers the release archives.
Future libraries/resources must be added explicitly to the packaging contract
and its checks.

For local reproduction see [BUILDING](BUILDING.md); for startup and known limits
see [INSTALL](INSTALL.md). A successful pipeline verifies the playable build's
distribution and generated-data startup, not complete game parity on every platform.
