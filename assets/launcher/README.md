# Launcher artwork, logo and font

`header.jpg` (1448 × 1086) is a remaster of Beach Life artwork (the
reaching-hand pose) without a logo, made by the project owner on 2026-10-17 as
the launcher background. The remastered PNG was converted to JPEG (quality 95)
without other edits. The launcher shows it behind its interface, preserving
the aspect ratio and cropping only at display time. The frosted panels use a
blurred, downscaled copy computed in memory at startup; the file itself is
unchanged.

- PNG SHA256 `49434fbde3c6d17b4c153e67cb425b0d1b71551c6b54fc7fc2d7176a9b5cd6e7`.
- JPEG SHA256 `9e93bb4d137c12e2e8058860a465eaec6846001bff5b76d7acbed803ec88d1a7`.

Rights in the underlying artwork remain with the respective owners; the
remaster does not change that. This artwork is not project-authored code.

`logo.png` is the unmodified 598 × 729 Beach Life logo (white on a transparent
background) shown at the top of the launcher's sidebar. The launcher resamples
it in memory to the exact size it is shown at, so its edges stay smooth; the
file itself is unchanged.

- Source: [LaunchBox Games Database, Virtual Resort: Spring Break](https://gamesdb.launchbox-app.com/games/images/106690-virtual-resort-spring-break),
  Clear Logo (Europe), [original PNG](https://images.launchbox-app.com/c0acdddc-bdc2-4318-9cd8-37aab542ae29.png).
- Retrieved: 2026-10-17.
- SHA256: `94a7938bcdc68cfec95368aeea39336f45843ee2250f2f28e673d88c6e649c61`.

It was downloaded from the public artwork gallery, not extracted from game
media. The logo and trademark belong to their respective owners; the gallery
does not state a redistribution license. It is not project-authored code.

The launcher uses Roboto Medium from the pinned Dear ImGui source's
`misc/fonts/Roboto-Medium.ttf`, unchanged. Its embedded notice is Copyright
2011 Google Inc. All Rights Reserved. The font is distributed under Apache 2.0;
the full text accompanies packages in `LICENSES/Apache-2.0.txt`.

CMake copies these resources into `assets/launcher/` next to the executable on
Windows/Linux, and into `Contents/Resources/assets/launcher/` in the macOS
bundle. Startup uses SDL's application resource directory, independently of
the working directory.
