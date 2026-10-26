# Using a native package

These packages contain the first graphical launcher and the game window. The
launcher selects and remembers a game source; Play opens the game window and
runs the experimental decompiled game. The game, game-content inspection and
language detection remain under development.

Download the archive matching your OS and CPU from the GitHub releases page: a
published release, or a pull request's test build (a prerelease). Extract the
entire archive into its own directory, which the launcher updates in place.

- Windows x86_64: open `beachlife.exe`. Keep the supplied runtime DLLs and
  `assets/` beside it.
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
the file is an ISO 9660 container. **Prepare files**, or **Play**, checks the
supported game layout and copies verified game data into local user storage.
This work runs in the game window's loading screen. Esc or closing that window
cancels setup and returns to the launcher. Cancelled setup can resume; previously
verified files are reused. **Prepare files** returns to Game files when done;
**Play** continues into the game. Missing, unreadable and unsupported
inputs show a specific status. The original installer is never run.

**Display** holds the window mode, window size, display, scaling, VSync and,
on Windows, the graphics API (DirectX 12 or Vulkan). Play's game window uses
them.
**Advanced** has a Diagnostics & analytics section: a log window for the game
(saved for later) and the settings file's location. No usage data is collected
or sent.

Click **Save changes** to remember the path, display, diagnostics, update and
interface settings, or **Revert** to restore the saved values. Preferences live
in SDL's user configuration directory (BeachLife/BeachLife/launcher.cfg).
`--config file` overrides that location. The ISO is never modified.
Closing with unsaved changes offers save, discard and cancel.

The prepared game's language appears after content inspection. Currently the
English disc layout is supported; installer language lists do not establish
game translations. This launcher's interface is English.

After setup, the local files work with the ISO disconnected. **Verify and repair**
on Game files needs the original ISO and restores missing or damaged files in
the same loading screen, then returns to the launcher.
An image changed in place requires setup again. Settings and future saves live
outside the game-data cache. See [DATA](DATA.md) for cache locations and checks.

## Play

**Play**, at the bottom right of every page, is available once a readable ISO
image is selected or prepared files are ready; its tooltip says why otherwise.
It saves unsaved settings, closes the launcher and opens the game window with
the Display settings:
window or fullscreen, size, display, Fit or Stretch, VSync and the graphics API
(Direct3D 12 or Vulkan on Windows, Metal on macOS, Vulkan on Linux). A graphics
API that is not available is reported, never replaced by another one. Startup
checks, ISO preparation and VFS loading run behind the in-game loading screen;
its progress appears when the work takes longer than a moment. Normal Play
also loads the supported English game configuration before the loading screen
fades. Menu rendering and gameplay are still being reconstructed, so the window
shows an empty picture with a
small overlay naming the graphics API and frame rate. Esc or closing the window
returns to the launcher.

To view original artwork now, open **Game files → Preview title screen**. This
opens the native window, prepares data if needed and displays the title image
from your copy. Display settings apply, and Esc returns to Game files. This is
an image preview; the original menu buttons and gameplay are not implemented.

The Display page's **Performance overlay** (off by default) adds a compact row
with the frame rate and the system's GPU and CPU usage in the chosen corner of
the game window, also in fullscreen. A reading the system does not provide shows
N/A: GPU usage comes from Windows' performance counters, macOS IOKit, NVIDIA's
driver library or AMD/Intel kernel counters on Linux (Intel's need system-wide
performance monitoring access).

## Updates

The footer's left side checks for updates: the refresh button checks now, the
**Release / Test** switch chooses between published releases and pull requests'
test builds (saved at once), and the status shows what the last check found.
When the check at start finds a newer build, the **Update** dialog opens with
its release notes (or, for a test build, its pull request's description); the
refresh button and the switch also open it for a switch to a test build or back
to a release. **Later** closes it (after the switch it also restores the previous
choice); the footer's button opens it again. Nothing is installed without asking.
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
No original executables, game DLLs, disc images or gameplay data are supplied. The
completed port will require a copy of the original game's data. Downloading a
launcher package does not provide that data or a playable game. The header
artwork (a remaster of Beach Life art), the game's logo, original application
icon and font have separate notices in the package. Windows uses the original
icon in the executable; macOS uses it in the app bundle. Linux window icons
depend on desktop support. The source icon is 32×32 pixels.

See [RELEASES](RELEASES.md) for candidate promotion and [BUILDING](BUILDING.md)
for building the same executable yourself.
