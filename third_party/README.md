stb_image.h, stb_truetype.h (v1.26, SHA-256 ecd30b05e0dd4fea3a13c26810dd9e1992dc379049482c393d5a19e6b5090aab) and LICENSE from https://github.com/nothings/stb/tree/2c980bb59875b0d32144a71867fbdebb2f77cd20
Unmodified upstream image decoder and TrueType rasteriser. stb_truetype renders the import screen's system fonts; stb_image now serves only the research viewer.

The disc's TrueType fonts (`TEXT/*.DLL`) are rendered by FreeType 2.13.3, the
release Wine bundles for GDI text, fetched by `cmake/freetype.cmake` from
https://download.savannah.gnu.org/releases/freetype/freetype-2.13.3.tar.xz
(SourceForge mirror as fallback; SHA-256
`0550350666d427c74daeb85d5ac7bb353acba5f76956395995311a9c6f063289`) and built
static without zlib/bzip2/png/harfbuzz/brotli. Unmodified source, used under the
FreeType License (`FreeType-FTL.txt`, from the archive's `docs/FTL.TXT`), which
ships in every package. Portions of this software are copyright © 2024 The
FreeType Project (www.freetype.org). All rights reserved.

The original draws its focus and dashboard arrows in Wingdings and its standings
in Courier New Bold, Windows system fonts. The game loads them from the host
(`system_font_paths` in `src/text.cpp`). Without a host Wingdings it uses Wine's
own replacement, `wine-fonts/wingding.ttf` from Wine 11.18 (unmodified, with its
`wingding.sfd` source; LGPL-2.1-or-later, `Wine-Wingdings-LGPL.txt`, shipped in
every package), compiled in by `cmake/wine_wingdings.cmake`.

Menu pictures (`IMAGES/*.RRI`, JPEG) are decoded by libjpeg-turbo 3.1.4.1,
fetched by `cmake/jpeg_scalar.cmake` from
https://github.com/libjpeg-turbo/libjpeg-turbo/archive/refs/tags/3.1.4.1.tar.gz
(SHA-256 `a7da42b640377c2a9a9665e2c4b0ea60cd5599afb48c2521e6df0c9dc9d15a25`)
and built static with SIMD off (`WITH_SIMD=OFF`): the original masks the IJG
sample-range table, which only the scalar path consults. Unmodified source;
licences `JPEG-LICENSE.md` and `JPEG-README.ijg` (IJG + BSD-3-Clause) ship in
the macOS bundle and the APK notices. This software is based in part on the
work of the Independent JPEG Group.

SDL Android runtime: https://github.com/libsdl-org/SDL/releases/tag/release-3.4.8
The official `SDL3-devel-3.4.8-android.zip` archive has SHA-256
`c4c23cec585cb99ea626625dc06b421933dd78733b3841628d86be1c13ab16c6`.
Its unmodified `SDL3-3.4.8.aar` is in `android/app/libs/` and the license is `SDL-LICENSE.txt`.

Static SDL builds (`RR_STATIC_SDL`: the standalone macOS app, the browser
build, Linux/Windows packages, and any host whose system SDL is older than
3.4) fetch the official SDL3-3.4.18.tar.gz source archive
(https://github.com/libsdl-org/SDL/releases/tag/release-3.4.18, October 2,
2026), SHA-256 `9c75cf16330322c217dedd2e0609f1124f1b54b8633e763467b4684d0f4334a3`.
`cmake/sdl3_static.cmake` verifies that digest and builds SDL statically.
3.4.18 is newer than 3.4.12, which added `SDL_HINT_ENABLE_STEAM_SCREEN_KEYBOARD`
(plan 33a). Android stays on the 3.4.8 AAR above until its device work bumps
it (plan 33 Task A7 records the split).
The SDL, stb, libjpeg-turbo, FreeType and Wine Wingdings licences are copied into the application's Resources.

Online multiplayer (plan 34) networking, fetched by `cmake/net.cmake` when
`RR_NET_TRANSPORT` is on (native desktop builds, rr-netd, net bots):
- libdatachannel v0.24.6, commit `6b1e2e620f1e37f0eafeee702eaea0043cb305fd`
  (https://github.com/paullouisageneau/libdatachannel), MPL-2.0,
  `libdatachannel-LICENSE.txt`. Built without media (`NO_MEDIA`), examples
  or tests. One modification (plan 33b Task 6), applied at configure time by
  `cmake/net.cmake` to the fetched `src/impl/websocket.cpp`: on Windows,
  wss:// certificate verification stays on when the caller supplies a CA
  file (the game passes the Windows ROOT store). The pinned code otherwise
  turns verification off on Windows. The replacement text in
  `cmake/net.cmake` is the modified source, as MPL-2.0 requires.
- Its submodules at that commit: libjuice v1.7.4 `b89c792e` (MPL-2.0,
  `libjuice-LICENSE.txt`), usrsctp `fec583d5` (BSD-3-Clause,
  `usrsctp-LICENSE.md`), plog `94899e0b` (MIT, `plog-LICENSE.txt`). The
  json and libsrtp submodules are not fetched (examples/media only).
- TLS: OpenSSL 3 (Apache-2.0, `OpenSSL-LICENSE.txt`): the system or
  Homebrew library, linked statically on macOS; vcpkg's static build on
  Windows; and on Linux packages OpenSSL 3.5.9 built static from the release
  tarball (SHA-256 `603f5602e2eef00d77fbd429d34dcd5822bb301757a1bc9cdb24c670f1eb859a`)
  by `tools/build_linux_package.sh`.
MPL-2.0 source for these libraries is available at the URLs above. The only
change to it is the libdatachannel modification described above.

Linux packages (plan 33a) also carry DejaVu Sans 2.37 (`fonts/`, Bitstream
Vera / public-domain DejaVu changes licence, `DejaVu-LICENSE.txt` from the
release tarball `dejavu-fonts-ttf-2.37.tar.bz2`, SHA-256
`fa9ca4d13871dd122f61258a80d01751d603b4d3ee14095d65453b4e846e17d7`), used when
the system has none of the fonts in `src/system_fonts.cpp`. The font files
are downloaded at package time, not committed.
