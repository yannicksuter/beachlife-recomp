# Runtime notices

Windows packages include the pinned LLVM-MinGW runtime DLLs needed by the native
launcher. Their upstream notices are copied from that exact toolchain archive
into `LICENSES/llvm-mingw/`: LLVM's `LICENSE.TXT`, the winpthreads notice and the
MinGW-w64 runtime notice. The source archive URL and SHA256 are recorded in
`tools/windows-toolchain.json` and the package provenance.

All packages statically link SDL3 (zlib), Dear ImGui (MIT) and stb's JPEG decoder
(MIT/public-domain dual license). Their complete notices are copied from the
exact prepared pins as `SDL.txt`, `ImGui.txt` and `stb.txt`. Dependency commits
and patch identities are recorded in SOURCE-MANIFEST.json.

Cabinet access builds pinned unshield and zlib libraries. Their notices are
retained in `unshield.txt` and `zlib.txt`; `RSA-MD5.txt` identifies unshield's
included RSA Data Security, Inc. MD5 Message-Digest Algorithm. The dependency
checkouts are exported without edits before compilation.

Roboto Medium is copied from the Dear ImGui pin, with the Apache 2.0 text in
`Apache-2.0.txt`. The launcher artwork has separate provenance and rights notes
in `assets/launcher/README.md`. These resources retain their owners' rights.

Linux and macOS packages use system runtimes. No retail game libraries or
extracted game files are distributed. New bundled dependencies must carry their
applicable notices when they enter the package.
