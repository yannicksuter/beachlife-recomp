"""Package only declared native outputs, documentation and runtime notices."""
import argparse
import hashlib
import io
import json
import os
import re
import stat
import struct
import subprocess
import tarfile
import tempfile
import zipfile
from pathlib import Path, PurePosixPath
from build_version import git_output
from project_version import ROOT, read_version
from prepare_sources import prepare

TARGETS = ("linux-x86_64", "steamdeck-x86_64", "windows-x86_64", "macos-arm64", "macos-x86_64")
DOCUMENTS = ("README.md", "docs/BUILDING.md", "docs/RELEASES.md", "docs/INSTALL.md", "LICENSES/README.md",
             "LICENSES/unshield.txt", "LICENSES/zlib.txt", "LICENSES/RSA-MD5.txt")
WINDOWS_DLLS = ("libc++.dll", "libunwind.dll", "libwinpthread-1.dll")
WINDOWS_NOTICES = ("LICENSE.TXT", "x86_64-w64-mingw32/share/mingw32/COPYING.winpthreads.txt",
                   "x86_64-w64-mingw32/share/mingw32/COPYING.MinGW-w64-runtime.txt")
MAC_FILES = ("beachlife.app/Contents/MacOS/beachlife", "beachlife.app/Contents/Info.plist",
             "beachlife.app/Contents/Resources/beachlife.icns")
MAC_SIGNATURE = "beachlife.app/Contents/_CodeSignature/CodeResources"
LAUNCHER_RESOURCES = ("header.jpg", "logo.png", "Roboto-Medium.ttf")
LAUNCHER_NOTICES = ("assets/launcher/README.md", "LICENSES/Apache-2.0.txt",
                    "LICENSES/SDL.txt", "LICENSES/ImGui.txt", "LICENSES/stb.txt", "LICENSES/FreeType.txt",
                    "LICENSES/FreeType-BDF.txt", "LICENSES/FreeType-PCF.txt")
MAC_WRAPPER = b'#!/bin/sh\nexec "$(dirname "$0")/beachlife.app/Contents/MacOS/beachlife" "$@"\n'
STEAM_LIBRARIES = {"libc.so.6", "libm.so.6", "libdl.so.2", "libpthread.so.0", "librt.so.1",
                   "libstdc++.so.6", "libgcc_s.so.1", "ld-linux-x86-64.so.2"}
STEAM_LIMITS = {"GLIBC": (2, 40), "GLIBCXX": (3, 4, 33), "CXXABI": (1, 3, 15), "GCC": (14, 0, 0)}


def digest(data):
    return hashlib.sha256(data).hexdigest()


def regular_file(root, relative):
    root = Path(root).resolve()
    path = root / relative
    if not path.is_file() or path.is_symlink() or not path.resolve().is_relative_to(root):
        raise ValueError(f"Missing or unsafe package input: {relative}")
    # Reject symlinked parent directories too, even if they resolve within root.
    if any(parent.is_symlink() for parent in path.parents if parent != root and parent.is_relative_to(root)):
        raise ValueError(f"Symlinked package input: {relative}")
    return path


def check_binary(data, system, architecture):
    try:
        if system == "linux":
            valid = data[:6] == b'\x7fELF\x02\x01' and struct.unpack_from('<H', data, 18)[0] == 62
        elif system == "windows":
            offset = struct.unpack_from('<I', data, 60)[0]
            valid = data[:2] == b'MZ' and data[offset:offset+4] == b'PE\0\0' and struct.unpack_from('<H', data, offset+4)[0] == 0x8664
        elif system == "macos":
            cpu = 0x100000C if architecture == "arm64" else 0x1000007
            valid = data[:4] == b'\xcf\xfa\xed\xfe' and struct.unpack_from('<I', data, 4)[0] == cpu
        else:
            valid = False
    except struct.error:
        valid = False
    if not valid:
        raise ValueError(f"Binary does not match {system}/{architecture}")


def check_steam_abi(report):
    needed = set(re.findall(r'\(NEEDED\).*?\[([^]]+)\]', report))
    versions = set(re.findall(r'Name: (\S+)', report))
    if not needed or not versions or needed - STEAM_LIBRARIES:
        raise ValueError(f"Unsupported or absent Linux runtime requirements: {sorted(needed)}")
    for value in versions:
        if not re.fullmatch(r'[A-Za-z]+_[0-9.]+', value):
            raise ValueError(f"Unsupported runtime symbol: {value}")
        family, number = value.split('_', 1)
        if family not in STEAM_LIMITS or tuple(map(int, number.split('.'))) > STEAM_LIMITS[family]:
            raise ValueError(f"Runtime symbol exceeds Steam Deck baseline: {value}")
    return {"libraries": sorted(needed), "symbols": sorted(versions)}


def payload_names(info):
    names = set(DOCUMENTS)
    if info["system"] == "macos":
        names.update(MAC_FILES)
        names.add("beachlife")
    else:
        names.add("beachlife.exe" if info["system"] == "windows" else "beachlife")
    if info["system"] == "windows":
        names.update(WINDOWS_DLLS)
        names.update("LICENSES/llvm-mingw/" + Path(name).name for name in WINDOWS_NOTICES)
    if info.get("capability") == "launcher":
        prefix = "beachlife.app/Contents/Resources/" if info["system"] == "macos" else ""
        names.update(prefix + "assets/launcher/" + name for name in LAUNCHER_RESOURCES)
        names.update((prefix + "assets/app/icon.png", "assets/app/README.md"))
        names.update(LAUNCHER_NOTICES)
    return names


def package(build, output, source=ROOT, toolchain=None):
    build, output, source = Path(build).resolve(), Path(output).resolve(), Path(source).resolve()
    info = json.loads(regular_file(build, "generated/build-info.json").read_text())
    target = info["platform"] + "-" + info["architecture"]
    if target not in TARGETS or info["system"] != ("linux" if info["platform"] == "steamdeck" else info["platform"]):
        raise ValueError("Unsupported package target")
    if info["project_version"] != read_version(source) or info["commit"] != git_output(source, "rev-parse", "HEAD"):
        raise ValueError("Build identity is stale; rebuild before packaging")
    dirty = bool(git_output(source, "status", "--porcelain=v1", "--untracked-files=normal"))
    if dirty != info["dirty"]:
        raise ValueError("Source state changed; rebuild before packaging")
    if info["channel"] != "dev" and dirty:
        raise ValueError("Test/release packages require clean committed sources")
    data, modes = {}, {}

    def copy(root, relative, destination=None):
        path = regular_file(root, relative)
        name = destination or relative
        data[name] = path.read_bytes()
        modes[name] = 0o755 if path.stat().st_mode & 0o111 else 0o644

    dependencies = []
    for name in DOCUMENTS:
        copy(source, name)
    system = info["system"]
    if info.get("capability") == "launcher":
        pin = json.loads(regular_file(source, 'patches/beachlife-decomp/source.json').read_text())
        if (not isinstance(pin, dict) or set(pin) != {'revision'}
                or not isinstance(pin['revision'], str)
                or not re.fullmatch(r'(?:[0-9a-f]{40}|[0-9a-f]{64})', pin['revision'])):
            raise ValueError('The reconstructed source pin must contain only a full revision SHA.')
        dependencies.extend(prepare(source, build / 'prepared', verify=True,
                                    decomp_revision=pin['revision'], decomp_profile='native'))
        prefix = "beachlife.app/Contents/Resources/" if system == "macos" else ""
        for name in LAUNCHER_RESOURCES:
            copy(build, prefix + 'assets/launcher/' + name)
        copy(build, prefix + 'assets/app/icon.png')
        copy(source, 'assets/app/README.md')
        for name in LAUNCHER_NOTICES[:2]:
            copy(source, name)
        for dependency, filename, destination in [('sdl', 'LICENSE.txt', 'SDL.txt'),
                                                  ('imgui', 'LICENSE.txt', 'ImGui.txt'),
                                                  ('stb', 'LICENSE', 'stb.txt'),
                                                  ('freetype', 'docs/FTL.TXT', 'FreeType.txt'),
                                                  ('freetype', 'src/bdf/README', 'FreeType-BDF.txt'),
                                                  ('freetype', 'src/pcf/README', 'FreeType-PCF.txt')]:
            copy(build / 'prepared' / dependency, filename, 'LICENSES/' + destination)
    executable = MAC_FILES[0] if system == "macos" else "beachlife.exe" if system == "windows" else "beachlife"
    if system == "macos":
        actual = {p.relative_to(build).as_posix() for p in (build / "beachlife.app").rglob('*') if not p.is_dir()}
        allowed = {name for name in payload_names(info) if name.startswith('beachlife.app/')}
        if actual - allowed - {MAC_SIGNATURE}:
            raise ValueError("Unexpected files in the application bundle")
        for name in MAC_FILES:
            copy(build, name)
        if (build / MAC_SIGNATURE).exists():
            copy(build, MAC_SIGNATURE)
        data["beachlife"], modes["beachlife"] = MAC_WRAPPER, 0o755
    else:
        copy(build, executable)
    check_binary(data[executable], system, info["architecture"])
    if info["build_version"].encode() not in data[executable]:
        raise ValueError("Executable does not contain the current build version")
    if system != "windows" and modes[executable] != 0o755:
        raise ValueError("Native executable is missing its executable permission")
    if system == "windows":
        if toolchain is None:
            raise ValueError("Windows packaging requires --toolchain for runtime DLLs/notices")
        toolchain = Path(toolchain)
        for name in WINDOWS_DLLS:
            copy(toolchain / "x86_64-w64-mingw32/bin", name)
            check_binary(data[name], "windows", "x86_64")
        for name in WINDOWS_NOTICES:
            copy(toolchain, name, "LICENSES/llvm-mingw/" + Path(name).name)
        dependencies.append(json.loads(regular_file(source, "tools/windows-toolchain.json").read_text()))
        # All imported non-system DLLs must be included. Inspect transitive runtimes too.
        system_dlls = {"kernel32.dll", "ntdll.dll", "msvcrt.dll", "ucrtbase.dll", "advapi32.dll",
                       "user32.dll", "bcrypt.dll", "bcryptprimitives.dll", "userenv.dll",
                       "ole32.dll", "shell32.dll", "version.dll", "winmm.dll",
                       "cfgmgr32.dll", "comdlg32.dll", "gdi32.dll", "imm32.dll",
                       "oleaut32.dll", "setupapi.dll", "shlwapi.dll", "rpcrt4.dll",
                       "dinput8.dll", "dxgi.dll", "d3d11.dll", "d3d12.dll", "hid.dll", "winhttp.dll", "pdh.dll"}
        for name, path in [(executable, build / executable)] + [(n, toolchain / "x86_64-w64-mingw32/bin" / n) for n in WINDOWS_DLLS]:
            report = subprocess.check_output([str(toolchain / "bin/llvm-objdump"), "-p", str(path)], text=True)
            imports = re.findall(r'DLL Name:\s*(\S+)', report)
            if not imports:
                raise ValueError(f"No import information for {name}")
            missing = [n for n in imports if n.lower() not in system_dlls | set(WINDOWS_DLLS)
                       and not n.lower().startswith(("api-ms-win-", "ext-ms-win-"))]
            if missing:
                raise ValueError(f"Unpackaged Windows imports: {missing}")
    abi = None
    if info["platform"] == "steamdeck":
        report = subprocess.check_output(["readelf", "--dynamic", "--version-info", "--wide", str(build / executable)], text=True)
        abi = check_steam_abi(report)
    manifest = {"format": 1, "build": info, "dependencies": dependencies, "patches": [],
                "files": {name: {"sha256": digest(body), "bytes": len(body), "mode": modes[name]}
                          for name, body in sorted(data.items())}}
    if abi:
        manifest["runtime_requirements"] = abi
    data["SOURCE-MANIFEST.json"] = (json.dumps(manifest, indent=2, sort_keys=True) + "\n").encode()
    modes["SOURCE-MANIFEST.json"] = 0o644
    stem = f'beachlife-{info["build_version"]}-{target}'
    output.mkdir(parents=True, exist_ok=True)
    destination = output / (stem + (".zip" if system == "windows" else ".tar.gz"))
    with tempfile.TemporaryDirectory(dir=output) as temp:
        archive = Path(temp) / destination.name
        if system == "windows":
            with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as handle:
                for name, body in sorted(data.items()):
                    handle.writestr(name, body)
        else:
            with tarfile.open(archive, "w:gz") as handle:
                for name, body in sorted(data.items()):
                    entry = tarfile.TarInfo(stem + "/" + name)
                    entry.size, entry.mode, entry.mtime = len(body), modes[name], info["commit_time"]
                    handle.addfile(entry, io.BytesIO(body))
        os.replace(archive, destination)
    return destination


def inspect_package(path):
    """Validate members before extraction or publication, including their digests."""
    path = Path(path)
    files = {}

    def add(name, body, mode):
        parts = PurePosixPath(name)
        if not name or parts.is_absolute() or '..' in parts.parts or '\\' in name or ':' in name or name in files:
            raise ValueError(f"Unsafe/duplicate archive member: {name}")
        if parts.as_posix() != name:
            raise ValueError(f"Noncanonical archive member: {name}")
        files[name] = (body, mode)

    if path.suffix == '.zip':
        with zipfile.ZipFile(path) as handle:
            for entry in handle.infolist():
                if entry.is_dir() or stat.S_ISLNK(entry.external_attr >> 16):
                    raise ValueError("Only regular archive files are allowed")
                add(entry.filename, handle.read(entry), 0o644)
    else:
        stem = path.name.removesuffix('.tar.gz')
        with tarfile.open(path, 'r:gz') as handle:
            for entry in handle:
                if not entry.isfile() or not entry.name.startswith(stem + '/'):
                    raise ValueError("Unexpected archive root or non-regular member")
                add(entry.name[len(stem) + 1:], handle.extractfile(entry).read(), entry.mode)
    if "SOURCE-MANIFEST.json" not in files:
        raise ValueError("Missing package manifest")
    manifest = json.loads(files["SOURCE-MANIFEST.json"][0])
    info = manifest["build"]
    if manifest.get("format") != 1 or info["platform"] + '-' + info["architecture"] not in TARGETS:
        raise ValueError("Unsupported package format or target")
    if info["system"] != ("linux" if info["platform"] == "steamdeck" else info["platform"]):
        raise ValueError("Package target system mismatch")
    expected = payload_names(info)
    if info["system"] == "macos" and MAC_SIGNATURE in files:
        expected.add(MAC_SIGNATURE)
    if set(files) != expected | {"SOURCE-MANIFEST.json"} or set(manifest["files"]) != expected:
        raise ValueError("Archive does not match the declared payload")
    for name in expected:
        body, mode = files[name]
        record = manifest["files"][name]
        if record["sha256"] != digest(body) or record["bytes"] != len(body):
            raise ValueError(f"Package digest mismatch: {name}")
        if info["system"] != "windows" and record["mode"] != mode:
            raise ValueError(f"Package permissions mismatch: {name}")
    return manifest, files


def verify_set(directory, version, sha, channel, run_id=None):
    paths = sorted(p for p in Path(directory).iterdir() if p.name.endswith(('.zip', '.tar.gz')))
    found = set()
    for path in paths:
        manifest, _ = inspect_package(path)
        info = manifest["build"]
        target = info["platform"] + '-' + info["architecture"]
        expected_version = version + '+g' + sha[:12]
        extension = '.zip' if info["system"] == 'windows' else '.tar.gz'
        if target in found or target not in TARGETS:
            raise ValueError("Duplicate or unexpected package target")
        if (info["project_version"], info["commit"], info["channel"], info["dirty"], info["build_version"]) != (version, sha, channel, False, expected_version):
            raise ValueError("Package version/source/channel mismatch")
        if run_id is not None and info["run_id"] != int(run_id):
            raise ValueError("Package belongs to another workflow run")
        if path.name != f'beachlife-{expected_version}-{target}{extension}':
            raise ValueError("Package filename does not match its identity")
        found.add(target)
    if found != set(TARGETS):
        raise ValueError(f"Incomplete package matrix: {sorted(found)}")
    checksums = Path(directory) / 'SHA256SUMS'
    checksums.write_text(''.join(f'{digest(p.read_bytes())}  {p.name}\n' for p in paths))
    return [*paths, checksums]


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--toolchain', type=Path)
    args = parser.parse_args()
    print(package(args.build_dir, args.output_dir, toolchain=args.toolchain))
