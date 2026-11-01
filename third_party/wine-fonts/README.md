Wine's Wingdings replacement, unmodified from https://gitlab.winehq.org/wine/wine
at tag `wine-11.18`: `fonts/wingding.sfd` (FontForge source) and
`fonts/wingding.ttf` (SHA-256
`cf5784b53e365ecfad1661b8b23d133effa1d3b54fb7a51137c8a9548f0db08e`, identical to
the file Wine 11.18 Staging installs). Copyright (C) 2013 Dmitry Timoshkov,
LGPL-2.1-or-later (`../Wine-Wingdings-LGPL.txt`).

`cmake/wine_wingdings.cmake` compiles the .ttf into rr-resource. The game uses
it only when the host has no Wingdings (Linux, Android, browser): the original
draws its focus and gap arrows with the system Wingdings, and on such hosts
Wine draws this font. To use a modified font, replace `wingding.ttf` and rebuild.
