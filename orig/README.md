# Original files (not in Git)

Put your own copy of the game here for development: a dump of your CD
(`.iso`, or `.cue` with its `.bin` tracks) or an installed game folder.

Everything in this folder except this README is ignored by Git and must never
be committed. No original assets are part of this repository.

Native bootstrap builds, PR artifacts and stable candidates require none of
these files. CI uses generated fixtures. Package creation copies only declared
native outputs and notices; this directory is never a package input. See
[building](../docs/BUILDING.md) and [releases](../docs/RELEASES.md).
