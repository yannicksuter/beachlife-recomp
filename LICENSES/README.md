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

The game window's text is rasterized by FreeType, used under the FreeType
License (selected from its FTL/GPLv2 choice) and copied from the pin as
`FreeType.txt`. Its contributed BDF and PCF drivers carry the additional notices
in `FreeType-BDF.txt` and `FreeType-PCF.txt`.

Portions of this software are copyright © 2026 The FreeType Project
(https://freetype.org). All rights reserved.

Cabinet access builds pinned unshield and zlib libraries. Their notices are
retained in `unshield.txt` and `zlib.txt`; `RSA-MD5.txt` identifies unshield's
included RSA Data Security, Inc. MD5 Message-Digest Algorithm. The dependency
checkouts are exported without edits before compilation.

The decompiled game's audio and movie adapters link dr_libs and PL_MPEG.
Game packages include the pinned dr_libs license as `dr-libs.txt` and PL_MPEG's
MIT attribution and license text as `pl-mpeg.txt`. These adapters decode the
player's data; no retail media or original middleware is included.

Roboto Medium is copied from the Dear ImGui pin, with the Apache 2.0 text in
`Apache-2.0.txt`. The launcher artwork has separate provenance and rights notes
in `assets/launcher/README.md`. The original application icon's provenance is
in `assets/app/README.md`. These resources retain their owners' rights.

Linux and macOS packages use system runtimes. No retail game libraries or
gameplay data is distributed. New bundled dependencies must carry their
applicable notices when they enter the package.
