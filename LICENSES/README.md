# Runtime notices

Windows packages include the pinned LLVM-MinGW runtime DLLs needed by the native
bootstrap. Their upstream notices are copied from that exact toolchain archive
into `LICENSES/llvm-mingw/`: LLVM's `LICENSE.TXT`, the winpthreads notice and the
MinGW-w64 runtime notice. The source archive URL and SHA256 are recorded in
`tools/windows-toolchain.json` and the package provenance.

Linux and macOS bootstrap packages use system runtimes. No game libraries or
original game files are distributed. New bundled dependencies must carry their
own applicable notices when they enter the package.
