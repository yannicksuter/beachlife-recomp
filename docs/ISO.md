# ISO directory inspection

The native executable can list a disc image without mounting it, extracting
files or running the installer:

```sh
./build/debug/beachlife --inspect-iso /path/to/game.iso
```

The command returns JSON with the volume label, selected filesystem, image
size, file/folder counts and sorted entries. Each entry has a logical path,
byte offset, byte size and directory flag. Paths retain their display case;
ISO file-version suffixes such as `;1` are omitted. Errors go to stderr with a
nonzero exit status. No report is saved unless the caller redirects stdout.

The reader supports single-volume ISO 9660 with 2048-byte logical sectors and
prefers a recognized Joliet descriptor for Unicode filenames. It reads volume
and directory metadata only. It checks mirrored numeric fields, extent and
record boundaries, names, duplicate paths and directory cycles. Traversal is
bounded to 32 levels, 100,000 entries, 16 MiB per directory, 64 MiB of directory
metadata and 16 MiB of combined path text. Multi-extent and interleaved files,
associated-file records and multi-volume images produce explicit errors.
Record layouts follow [ECMA-119](https://www.ecma-international.org/wp-content/uploads/ECMA-119_4th_edition_june_2019.pdf);
[Joliet adds Unicode filename support](https://learn.microsoft.com/en-us/windows/win32/imapi/disc-formats).

This index describes files on the disc. It does not open installer cabinets,
identify available game translations or establish that gameplay can start.
Cabinet access and mapping to installed game paths are subsequent layers.

The C++ entry point is `beachlife::data::index_iso(path)` in
`src/data/iso9660.h`. It returns an owned `IsoIndex` or throws an error; no file
handle remains open after the call. Directory counts exclude the root. A UI
can run the synchronous scan outside its render loop, then display the result
only if the selected source still matches the request.

`iso_directory_index` in CTest exercises the native command using generated
ISO/Joliet images. Checks cover nested Unicode paths, padding, malformed names,
inconsistent fields, invalid extents, cycles and unsupported layouts. The same
checks run against each extracted native package, including Windows and both
macOS architectures, without any retail input.
