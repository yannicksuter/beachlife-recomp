# Using a native package

These packages contain the first graphical launcher. It selects and remembers
a game source; game-content inspection, language detection and gameplay remain
under development. Play, at the bottom right of every page, stays unavailable
in this preview; its tooltip says why.

Download the archive matching your OS and CPU from the GitHub releases page: a
published release, or a pull request's test build (a prerelease). Extract the
entire archive into its own directory, which the launcher updates in place.

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

On **Game files**, choose the ISO image of your original CD: use Browse,
paste a path, or drop the file onto the window. ISO selection checks that
the file is an ISO 9660 container. It does not yet verify Beach Life's game
content. Missing, unreadable and unsupported inputs show a specific status.

**Display** holds the window mode, window size, display, scaling, VSync and,
on Windows, the graphics API (DirectX 12 or Vulkan). They are saved for the
native game and take effect once it runs; this preview does not start it.
**Advanced** has a Diagnostics & analytics section: a log window for the game
(also saved for later) and the settings file's location. Nothing is collected
or sent.

Click **Save changes** to remember the path, display, diagnostics, update and
interface settings, or **Revert** to restore the saved values. Preferences live
in SDL's user configuration directory (BeachLife/BeachLife/launcher.cfg).
`--config file` overrides that location. Game sources are never modified,
extracted or copied by the launcher.
Closing with unsaved changes offers save, discard and cancel.

Game-language selection stays unavailable until content inspection can establish
which translations the selected source actually contains. Installer language
lists do not establish that. This first launcher's interface is English.

## Updates

The footer's left side checks for updates: the refresh button checks now, the
**Release / Test** switch chooses between published releases and pull requests'
test builds (saved at once), and the status shows what the last check found.
**Update** (or **Install**, for a switch to a test build or back to a release)
downloads the package for your system, checks it against GitHub's SHA-256,
verifies every file against its `SOURCE-MANIFEST.json`, replaces only the
package's own files and restarts the launcher. Anything else in the folder
stays. A failed or interrupted replacement is rolled back or completed at the
next start. Copies that were not unpacked from a package (local builds) or a
read-only folder get **Download**, which opens the release page instead.

The launcher checks once at every start; **Advanced → Updates → Check at
start** turns that off. A check reads GitHub's public release list
(`api.github.com`); downloads come from GitHub. HTTPS uses WinHTTP on Windows
and the system's libcurl on Linux and macOS.

Release archives have SHA256SUMS and each package contains SOURCE-MANIFEST.json.
No original executables, game DLLs, disc images or extracted assets are supplied. The
completed port will require a copy of the original game's data. Downloading a
launcher package does not provide that data or a playable game. The header
artwork (a remaster of Beach Life art), the game's logo and the font have
separate notices in the package.

See [RELEASES](RELEASES.md) for candidate promotion and [BUILDING](BUILDING.md)
for building the same executable yourself.
