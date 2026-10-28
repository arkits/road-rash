# Browser error tracking and analytics

Production https://road-rash.archit.xyz uses:

- Sentry project `archit-khode/road-rash`, JavaScript browser SDK 11.9.0.
  The SDK posts through the same-origin tunnel `/t/e` (`tunnel` option).
  `deploy/nginx.conf` forwards POSTs there to this project's fixed envelope
  endpoint only (no relay to other hosts or projects). It strips cookies,
  Authorization and client-IP headers, verifies upstream TLS, resolves DNS per
  request, and caps the path at one global 10/s bucket (burst 100, then 429).
  The bucket is global because behind the host TLS proxy and Dokku's proxy every
  request reaches the container from one address. GET and other methods get 403.
  https://archit-khode.sentry.io/issues/?project=4512320659914752
- PostHog project `Road Rash` (688403), browser SDK 1.438.21, US region, sent
  through the PostHog managed reverse proxy `https://nazar.archit.xyz`
  (`api_host`; `ui_host` is `https://us.posthog.com`). A first-party host is
  blocked less often than `us.i.posthog.com`.
  https://us.posthog.com/project/688403/activity/events

`web/telemetry.js` uses public ingestion keys. No management token belongs in
browser code or the repository. Dependencies and build tool versions are pinned
in `web/package-lock.json`; `tools/build_web.sh` runs `npm ci` and bundles them
into one fingerprinted same-origin script. Node 22+ and npm are required.
Sentry release is `road-rash@<build Git SHA>`; environment is production.
The app runs with no-op hooks if telemetry fails to load, and SDK calls cannot
prevent playing. Localhost and other hostnames never initialize either SDK.

## Events

| Event | Properties / meaning |
| --- | --- |
| `$pageview` | One per page load |
| `launcher_ready` | `source`: hosted or local disc |
| `game_start` | `mode`: main_game, quick_race, or online_host / online_browse / online_join / online_watch (no room code is sent); quick race `course`: 0–4 and `character`: 1–8 |
| `game_exit` | Normal main() return; mode and duration_ms |
| `launcher_exit` | Exit to launcher button |
| `connection_retry` | Retry connection button |
| `disc_import_start` | source: iso or folder |
| `disc_import_complete`, `disc_import_cancelled` | Import outcome |
| `fullscreen` | Successful fullscreen entry |
| `web_error` | phase only; details go to Sentry and PostHog `$exception` |
| `$exception` | PostHog error tracking: every handled `rrTelemetry.error` (with `phase`) plus uncaught errors and rejections; `release` and `engine_log` attached |

Every event includes release. A start event means the user requested launch,
not that the race rendered or finished. No per-frame or simulation hooks added.

Sentry and PostHog error tracking both capture uncaught exceptions/rejections
plus handled initialization, import, fullscreen, game execution and Emscripten
abort failures with phase tags. Wasm traps (for example `memory access out of
bounds`) escaping SDL's frame callbacks are uncaught errors and reach both.
PostHog uses the slim SDK, so `telemetry.js` passes `ErrorTrackingExtensions`
and bundles `posthog-js/dist/exception-autocapture` itself; without them the
slim SDK has no exception capture, and the lazy CDN load is blocked by
`disable_external_dependency_loading` and the CSP. Console errors are not
captured. Each error carries `engine_log`: the last 40 engine stderr lines
(SDL/C++ diagnostics, at most 300 characters each), never user input.

A tab killed without a JavaScript error (mobile Safari reloading a page that
used too much memory, a renderer crash) cannot report at that moment. While a
game runs, localStorage holds `rr-game-session`; a clean return or `pagehide`
removes it. A page load that still finds it reports
`Previous game session ended without a clean exit (<mode>)` with phase
`unclean_exit`.
JS stack locations remain available; this does not add C++/Wasm source maps or
promise native symbolication. Recovered cache misses are intentionally not errors.

PostHog uses a pseudonymous localStorage ID without identified person profiles.
Autocapture, replay, surveys, external extensions and feature flags are
disabled; exception capture is on (above). Explicit event properties are allowlisted. Disc bytes,
saves, keystrokes and user file objects are never passed to tracking.
SDK browser/device/session metadata is still present. Query strings and URL
fragments are stripped from analytics URL fields, Sentry request URLs and stack
frame URLs. Sentry default PII and console/network/interaction breadcrumbs are
disabled; error messages and stack traces still describe the failure.

CSP connect-src is `'self'` (which covers the Sentry tunnel) plus the PostHog proxy
`nazar.archit.xyz`; no third-party ingestion host remains. A blocker that
matches Sentry envelope payloads or same-origin SDK bundles can still drop
reports, but host-based lists no longer do. Scripts
remain same-origin. Ad blockers may suppress delivery; gameplay still works.

## Verification

`tests/web_telemetry.cjs` exercises the real bundled SDKs in Chromium with both
services intercepted: handled/uncaught errors and a real Wasm `unreachable` trap
thrown from `requestAnimationFrame` in both Sentry envelopes and PostHog
`$exception` events, `engine_log`, the unclean-exit report, phase tags, PostHog events and
properties, URL scrubbing, CSP, disabled localhost and blocked endpoints.
PostHog filters webdriver traffic, so this payload test simulates a human UA.
`tests/web_hosted_smoke.cjs` intercepts telemetry at the context level so its
intentional manifest/download faults never pollute production data. Its upload
check allows POST only to the tunnel and the PostHog proxy. Game assets remain GET-only.
Both run in the deployment workflow; stale-cache and full hosted gameplay gates
remain required. Normal PostHog bot filtering stays enabled in production.

References: https://posthog.com/docs/libraries/js and the pinned SDK type/source
options; https://docs.sentry.io/platforms/javascript/configuration/options/.
