#!/usr/bin/env python3
"""Packages for tests/launcher_updates.cpp, written the way tools/package_build.py writes them.

Usage: launcher_update_fixture.py TEST_BINARY OUTPUT_DIR

Creates an installed 0.0.1 package with other files beside it, 0.0.2 packages
(tar.gz with a top folder for Linux and the Steam Deck, zip with files at its
root for Windows), and damaged or hostile archives; then runs the C++ checks.
"""

import hashlib
import io
import json
import shutil
import subprocess
import sys
import tarfile
import zipfile
from pathlib import Path

OLD_VERSION = "0.0.1+gaaaaaaaaaaaa"
NEW_VERSION = "0.0.2+gbbbbbbbbbbbb"

OLD_FILES = {
    "beachlife": (b"old launcher\n", 0o755),
    "assets/launcher/header.jpg": (b"old header\n", 0o644),
    "docs/OLD.md": (b"only in the old package\n", 0o644),
}
NEW_FILES = {
    "beachlife": (b"new launcher\n", 0o755),
    "assets/launcher/header.jpg": (b"new header\n", 0o644),
    "assets/launcher/logo.png": (b"new logo\n" * 1000, 0o644),
    "docs/NEW.md": (b"only in the new package\n", 0o644),
    # Require a pax header while leaving room for Windows' staging directories.
    "LICENSES/dependencies/" + "a" * 35 + "/" + "b" * 35 + "/LICENSE": (b"license text\n", 0o644),
}
OTHER_FILES = {
    "my-notes.txt": b"kept by the player\n",
    "screenshots/beach.png": b"screenshot",
}


def manifest(version, platform, files):
    build = {"build_version": version, "platform": platform, "architecture": "x86_64",
             "system": "linux" if platform == "steamdeck" else platform}
    return json.dumps({
        "format": 1, "build": build, "dependencies": [], "patches": [],
        "files": {path: {"sha256": hashlib.sha256(data).hexdigest(), "bytes": len(data), "mode": mode}
                  for path, (data, mode) in files.items()},
    }, indent=2, sort_keys=True).encode() + b"\n"


def payload(version, platform, files):
    data = {name: body for name, (body, _) in files.items()}
    modes = {name: mode for name, (_, mode) in files.items()}
    data["SOURCE-MANIFEST.json"] = manifest(version, platform, files)
    modes["SOURCE-MANIFEST.json"] = 0o644
    return data, modes


def write_tar(archive, stem, data, modes):
    with tarfile.open(archive, "w:gz") as handle:
        for name, body in sorted(data.items()):
            entry = tarfile.TarInfo(stem + "/" + name)
            entry.size, entry.mode, entry.mtime = len(body), modes[name], 1792263357
            handle.addfile(entry, io.BytesIO(body))


def write_zip(archive, data):
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as handle:
        for name, body in sorted(data.items()):
            handle.writestr(name, body)


def tar_with(archive, members):
    """A tar.gz of explicit members: (TarInfo, bytes or None)."""
    with tarfile.open(archive, "w:gz") as handle:
        for info, data in members:
            handle.addfile(info, io.BytesIO(data) if data is not None else None)


def file_info(name, data, mode=0o644):
    info = tarfile.TarInfo(name)
    info.size, info.mode = len(data), mode
    return info


def main():
    binary, output = Path(sys.argv[1]), Path(sys.argv[2])
    shutil.rmtree(output, ignore_errors=True)
    output.mkdir(parents=True)

    installed = output / "installed"
    data, modes = payload(OLD_VERSION, "linux", OLD_FILES)
    for name, body in {**data, **OTHER_FILES}.items():
        (installed / name).parent.mkdir(parents=True, exist_ok=True)
        (installed / name).write_bytes(body)
        (installed / name).chmod(modes.get(name, 0o644))

    for platform in ("linux", "steamdeck"):
        stem = f"beachlife-{NEW_VERSION}-{platform}-x86_64"
        write_tar(output / f"{stem}.tar.gz", stem, *payload(NEW_VERSION, platform, NEW_FILES))
    write_zip(output / f"beachlife-{NEW_VERSION}-windows-x86_64.zip", payload(NEW_VERSION, "windows", NEW_FILES)[0])

    top = f"beachlife-{NEW_VERSION}-linux-x86_64"
    good_manifest = manifest(NEW_VERSION, "linux", {"beachlife": (b"x\n", 0o755)})
    tar_with(output / "bad-traversal.tar.gz", [
        (file_info(f"{top}/SOURCE-MANIFEST.json", good_manifest), good_manifest),
        (file_info(f"{top}/../../evil", b"escaped\n"), b"escaped\n"),
    ])
    link = tarfile.TarInfo(f"{top}/beachlife")
    link.type, link.linkname = tarfile.SYMTYPE, "/bin/sh"
    tar_with(output / "bad-symlink.tar.gz", [(file_info(f"{top}/SOURCE-MANIFEST.json", good_manifest), good_manifest),
                                             (link, None)])
    tar_with(output / "bad-hash.tar.gz", [
        (file_info(f"{top}/SOURCE-MANIFEST.json", good_manifest), good_manifest),
        (file_info(f"{top}/beachlife", b"y\n", 0o755), b"y\n"),
    ])
    tar_with(output / "bad-absolute.tar.gz", [(file_info("/tmp/beachlife-absolute", b"x\n"), b"x\n")])
    unlisted, _ = payload(NEW_VERSION, "windows", {"beachlife.exe": (b"x\n", 0o755)})
    unlisted["extra.dll"] = b"not in the manifest\n"
    write_zip(output / "bad-unlisted.zip", unlisted)
    complete = (output / f"{top}.tar.gz").read_bytes()
    (output / "bad-truncated.tar.gz").write_bytes(complete[: len(complete) // 2])

    return subprocess.run([str(binary), str(output)], check=False).returncode


if __name__ == "__main__":
    sys.exit(main())
