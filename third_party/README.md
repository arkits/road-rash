stb_image.h, stb_truetype.h (v1.26, SHA-256 ecd30b05e0dd4fea3a13c26810dd9e1992dc379049482c393d5a19e6b5090aab) and LICENSE from https://github.com/nothings/stb/tree/2c980bb59875b0d32144a71867fbdebb2f77cd20
Unmodified upstream image decoder and TrueType rasteriser. Used for the original JPEG-encoded RRI assets and for the TrueType fonts on the disc (TEXT/*.DLL).

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
The existing SDL and stb licences are copied into the application's Resources.

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
- TLS: OpenSSL 3 (Apache-2.0, `OpenSSL-LICENSE.txt`), the system or
  Homebrew library, linked statically on macOS.
MPL-2.0 source for these libraries is available at the URLs above; the port
does not modify it.
