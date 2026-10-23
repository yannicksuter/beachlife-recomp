# Application icon

The original Beach Life application icon, extracted from the English retail
executable's icon group 104 (language 2057), without running the executable.

- `icon.ico` retains both original 32×32 resource images: 4-bit and 8-bit color.
  SHA256: `21aded5790374244ea9b7be5c51bb97e9a1db29c8e515a2980d2246533c4b79a`.
- `icon.png` is a lossless RGBA conversion of the 8-bit frame, including its
  transparency mask. It supplies the launcher and game window icons.
- The macOS build uses Apple's `sips` and `iconutil` to make an app-bundle icon
  from the PNG. Larger sizes are scaled from the original 32×32 artwork;
  no higher-resolution original was present in the executable.

This artwork belongs to its original rights holders. It is not covered by the
licenses of the project's source code or dependencies. Inclusion does not
imply endorsement or permission to redistribute other original game assets.
