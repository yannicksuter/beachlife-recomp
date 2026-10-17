# Using a native package

These packages contain the first graphical launcher. It selects and remembers
a game source; game-content inspection, language detection and gameplay remain
under development. Play is unavailable in this preview.

Download the archive matching your OS and CPU from a PR's Actions artifacts,
its promotion test prerelease, or a release candidate. Repository access is
required. Extract the entire archive into its own directory.

- Windows x86_64: open `beachlife.exe`. Keep the supplied runtime DLLs and
  `assets/launcher/` beside it.
- Linux x86_64: run `./beachlife`. Use the compatibility/Steam Deck package
  when the ordinary Linux package requires newer system libraries.
- macOS ARM64 or Intel: open `beachlife.app`, or run the provided `./beachlife`
  wrapper from Terminal. The application is
  ad-hoc signed, without Developer ID signing or notarization.

`--version` reports the CMake version and compiled commit; `--build-info` emits
JSON identifying the platform, build channel and workflow run. `--help` explains
the current capability. Unknown arguments return an error.

On **Game files**, select Disc image or Installed folder, then use Browse,
paste a path, or drop a file/folder onto the window. ISO selection checks that
the file is an ISO 9660 container. It does not yet verify Beach Life's game
content. Missing, unreadable and unsupported inputs show a specific status.

Click **Save changes** to remember the path and interface size, or **Revert**
to restore the saved values. Preferences live in SDL's user configuration
directory (BeachLife/BeachLife/launcher.cfg). `--config file` overrides that
location. Game sources are never modified, extracted or copied by the launcher.
Closing with unsaved changes offers save, discard and cancel.

Game-language selection stays unavailable until content inspection can establish
which translations the selected source actually contains. Installer language
lists do not establish that. This first launcher's interface is English.

Release archives have SHA256SUMS and each package contains SOURCE-MANIFEST.json.
No original executables, game DLLs, disc images or extracted assets are supplied. The
completed port will require a copy of the original game's data. Downloading a
launcher package does not provide that data or a playable game. The header's
public promotional artwork and font have separate notices in the package.

See [RELEASES](RELEASES.md) for candidate promotion and [BUILDING](BUILDING.md)
for building the same executable yourself.
