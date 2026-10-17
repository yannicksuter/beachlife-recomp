# Read-only game data

Data access is independent of the launcher and does not run the retail game or
installer. The native directory index is documented in [ISO.md](ISO.md).

## Logical paths

`normalize_game_path` in `src/data/game_path.h` accepts relative UTF-8 names.
It converts slash/backslash separators, skips repeated separators and `.`
components, and retains the display spelling. Its lookup key folds ASCII
letters to lowercase, matching the paths observed in the game. Non-ASCII
characters remain exact; full Unicode Windows case folding is not implemented.

Absolute paths, drive names, `..`, alternate streams, wildcards, device names,
trailing dots/spaces, control characters and malformed UTF-8 are rejected.
Paths are limited to 4096 input bytes. These are logical source names, not
permission to join unchecked paths onto a native directory; the folder backend
must separately enforce containment, including symbolic links.

`game_paths` in CTest covers lookup equivalence, preserved Unicode/display
spelling and invalid paths using generated data.
