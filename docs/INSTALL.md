# Using a native package

These packages currently contain a command-line bootstrap. The launcher and
reconstructed game are under development; no game data can be loaded yet.

Download the archive matching your OS and CPU from a PR's Actions artifacts,
its promotion test prerelease, or a release candidate. Repository access is
required. Extract the entire archive into its own directory.

- Windows x86_64: keep `beachlife.exe` beside the supplied runtime DLLs. From a
  terminal run `.\beachlife.exe --help`.
- Linux x86_64: run `./beachlife --help`. Use the compatibility/Steam Deck package
  when the ordinary Linux package requires newer system libraries.
- macOS ARM64 or Intel: from Terminal run the provided `./beachlife --help`
  wrapper. It invokes the executable inside `beachlife.app`. The application is
  ad-hoc signed, without Developer ID signing or notarization.

`--version` reports the CMake version and compiled commit; `--build-info` emits
JSON identifying the platform, build channel and workflow run. `--help` explains
the current capability. Unknown arguments return an error.

Release archives have SHA256SUMS and each package contains SOURCE-MANIFEST.json.
No original executables, game DLLs, disc images or assets are supplied. The
completed port will require a copy of the original game's data. Downloading a
bootstrap package does not provide that data or a playable game.

See [RELEASES](RELEASES.md) for candidate promotion and [BUILDING](BUILDING.md)
for building the same executable yourself.
