# Application icon

`roadrash.ico` is the original icon group 102 (four images) extracted from the
unmodified disc `ROADRASH.EXE`, SHA-256
`73d7f9744b5b1a9482b3909950cdf3fe34119fe13d023b031e9c6739a60f18a2`.
`roadrash.png` is its 48×48 image with the original transparency.

The owner explicitly requested committing and using this icon. This is a narrow
exception to the repository's rule against packaging disc-derived artwork; no
other game data is included. The artwork remains the property of its original
rights holders.

Run `tools/extract_app_icon.py /path/to/original/ROADRASH.EXE` with Python,
pefile and Pillow to regenerate these files, the Android density resources and
`web/favicon.ico`. Launcher scaling preserves the pixel art; the favicon contains
16, 32 and 48 pixel images.
