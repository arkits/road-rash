# Web touch controls and landscape (November 1, 2026)

Task: browser build mobile UI (plan 23 web port): on-screen controls when a
phone/tablet is detected, and landscape in fullscreen. Baseline commit `1e40c93`.

## Changed files

- `web/index.html`: `#touch` button overlay, `#rotate-hint`, toolbar
  `#touch-toggle`, controls-panel note.
- `web/app.js`: pointer-tracked buttons send window key events with the default
  bindings' `code`s; coarse-pointer detection and `rr-touch` preference; mobile
  launch requests fullscreen then `screen.orientation.lock('landscape')`;
  rotate hint in portrait.
- `web/style.css`: overlay, held state, portrait layout, rotate hint.
- `docs/WEB.md`: "Phone and tablet controls" section.

No native source changed (the uncommitted `src/` input/frontend/settings edits
in the shared tree belong to another session and were not touched or staged).

## Evidence and checks

Browser-pane checks (emulated 375x812 and 844x390) and `tests/web_browser_smoke.cjs` passed; details in `docs/WEB.md`. Build dir:
`build-agent-web-touch-20261101` (private). No live processes held after the
session.

## Remaining

- Not observed on a physical phone: Android Chrome orientation lock and iOS
  Safari behaviour are from browser API behaviour, checked here only in an
  emulated mobile viewport.
- A web app manifest (`orientation: landscape`, installed-PWA launch) would
  need `tools/prepare_web_deploy.py`, `tools/build_web.sh` and Nginx changes;
  not done.
