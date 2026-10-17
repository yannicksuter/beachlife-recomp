# Launcher artwork and font

`header.jpg` is the unmodified 2048 × 1536 Beach Life artwork selected for the
launcher. The UI preserves its aspect ratio and crops it only while displaying
the header; the source file is unchanged.

- Source: [LaunchBox Games Database, Virtual Resort: Spring Break](https://gamesdb.launchbox-app.com/games/images/106690-virtual-resort-spring-break).
- Entry: Fanart – Background (Europe), image 1892103.
- [Original JPEG](https://images.launchbox-app.com/796bae1a-a793-4828-8472-4e0a914d065f.jpg).
- Retrieved: 2026-10-17.
- SHA256: `4b11456d71bbcca4dbe878723aa217457263a086a006b5d374ebc286bc180621`.

The original suggestion was [SteamGridDB grid 382962 by PhilippCross](https://www.steamgriddb.com/grid/382962),
listed at 920 × 430. The selected image is a different composition at a larger
resolution; no claim is made that it is a higher-resolution original of that
grid. It was downloaded from the public artwork gallery, not extracted from
game media. Rights remain with the respective owners; the gallery does not
state a redistribution license. This artwork is not project-authored code.

The launcher uses Roboto Medium from the pinned Dear ImGui source's
`misc/fonts/Roboto-Medium.ttf`, unchanged. Its embedded notice is Copyright
2011 Google Inc. All Rights Reserved. The font is distributed under Apache 2.0;
the full text accompanies packages in `LICENSES/Apache-2.0.txt`.

CMake copies both resources into `assets/launcher/` next to the executable on
Windows/Linux, and into `Contents/Resources/assets/launcher/` in the macOS
bundle. Startup uses SDL's application resource directory, independently of
the working directory.
