import * as Sentry from '@sentry/browser';
import posthog from 'posthog-js/dist/module.slim';
import {ErrorTrackingExtensions} from 'posthog-js/dist/extension-bundles';
// Bundled locally: the slim SDK would otherwise fetch this from PostHog's CDN,
// which external dependency loading and the CSP both forbid.
import 'posthog-js/dist/exception-autocapture';

// Public ingestion keys, never management/authentication credentials.
const enabled = location.hostname === 'road-rash.archit.xyz';
const release = typeof RR_WEB_RELEASE === 'string' ? RR_WEB_RELEASE : 'development';
const safe = operation => { try { return operation(); } catch (error) { console.warn('Tracking unavailable:', error); } };
const cleanUrl = value => {
    try { const url = new URL(value); return url.origin + url.pathname; } catch (_) { return undefined; }
};
// Last engine stderr lines (SDL/C++ diagnostics, never user input), attached to errors only.
const engineLog = [];
const engineTail = () => engineLog.join('\n');
// Set while a game runs; a page kill (e.g. a mobile out-of-memory reload) leaves it behind.
const sessionKey = 'rr-game-session';
// Per-tab storage survives a renderer reload without treating another active
// game tab as a crashed session, or letting its pagehide erase this tab's marker.
const storage = operation => { try { return operation(sessionStorage); } catch (_) { return undefined; } };
if (enabled) {
    safe(() => Sentry.init({
        dsn: 'https://72f1b0670d21ecd90bc4908ebe82d35e@o425745.ingest.us.sentry.io/4512320659914752',
        environment: 'production', release, sendDefaultPii: false,
        // Same-origin tunnel (deploy/nginx.conf) so blockers do not drop reports.
        tunnel: '/t/e',
        // No console logs, replay, network breadcrumbs or interaction text.
        integrations: defaults => defaults.filter(i => i.name !== 'Breadcrumbs'),
        beforeSend(event) {
            delete event.user;
            if (engineLog.length) event.extra = {...event.extra, engine_log: engineTail()};
            if (event.request) {
                event.request.url = cleanUrl(event.request.url);
                delete event.request.headers; delete event.request.cookies; delete event.request.data;
            }
            for (const exception of event.exception?.values || []) {
                for (const frame of exception.stacktrace?.frames || []) {
                    if (/^https?:/.test(frame.filename)) frame.filename = cleanUrl(frame.filename);
                }
            }
            return event;
        }
    }));
    safe(() => posthog.init('phc_wnzWZVe4yh2bL979v7dyLTWZP7kYomNGC7myFqroyqmB', {
        // PostHog managed reverse proxy (first-party host, fewer blocker drops).
        api_host: 'https://nazar.archit.xyz', ui_host: 'https://us.posthog.com', defaults: '2026-08-30',
        autocapture: false, capture_pageview: false, capture_pageleave: false,
        // Error tracking: uncaught errors and rejections, including Wasm traps
        // escaping SDL's frame callbacks. Console errors stay private.
        __extensionClasses: ErrorTrackingExtensions,
        capture_exceptions: {capture_unhandled_errors: true, capture_unhandled_rejections: true, capture_console_errors: false},
        disable_session_recording: true,
        disable_surveys: true, advanced_disable_flags: true,
        disable_external_dependency_loading: true,
        person_profiles: 'never', persistence: 'localStorage',
        save_campaign_params: false, save_referrer: false,
        property_denylist: ['$initial_referrer', '$initial_referring_domain'],
        before_send(event) {
            if (!event) return null;
            for (const key of ['$current_url', '$referrer', '$session_entry_url', '$session_entry_referrer', '$initial_current_url']) {
                if (event.properties[key]) event.properties[key] = cleanUrl(event.properties[key]);
            }
            if (event.event === '$exception') {
                event.properties.release = release;
                if (engineLog.length) event.properties.engine_log = engineTail();
                for (const exception of event.properties.$exception_list || []) {
                    for (const frame of exception.stacktrace?.frames || []) {
                        for (const key of ['filename', 'abs_path']) {
                            if (/^https?:/.test(frame[key])) frame[key] = cleanUrl(frame[key]);
                        }
                    }
                }
            }
            return event;
        }
    }));
}
const allowed = new Set(['$pageview', 'launcher_ready', 'game_start', 'game_exit', 'launcher_exit',
    'connection_retry', 'disc_import_start', 'disc_import_complete', 'disc_import_cancelled', 'web_error', 'fullscreen']);
window.rrTelemetry = Object.freeze({
    track(name, properties = {}) {
        if (!enabled || !allowed.has(name)) return;
        // Deliberate allowlist: never send disc bytes, saves, filenames or input.
        const data = {release};
        for (const key of ['mode', 'course', 'character', 'source', 'phase', 'duration_ms']) {
            if (['string', 'number'].includes(typeof properties[key])) data[key] = properties[key];
        }
        safe(() => posthog.capture(name, data));
    },
    error(error, phase) {
        if (!enabled) return;
        const exception = error instanceof Error ? error : new Error(error?.message || String(error));
        if (!(error instanceof Error) && error?.name) exception.name = error.name;
        safe(() => Sentry.captureException(exception, {tags: {phase}}));
        safe(() => posthog.captureException(exception, {phase}));
        this.track('web_error', {phase});
    },
    log(line) {
        engineLog.push(String(line).slice(0, 300));
        if (engineLog.length > 40) engineLog.shift();
    },
    gameStarted(mode) { storage(s => s.setItem(sessionKey, JSON.stringify({mode, started: Date.now()}))); },
    gameStopped() { storage(s => s.removeItem(sessionKey)); }
});
window.rrTelemetry.track('$pageview');
// A previous game that never reached gameStopped or pagehide: the tab was killed
// (out of memory on mobile Safari, a renderer crash) and no error could be sent then.
const previous = storage(s => s.getItem(sessionKey));
if (previous) {
    window.rrTelemetry.gameStopped();
    let mode;
    try { mode = JSON.parse(previous).mode; } catch (_) {}
    window.rrTelemetry.error(new Error('Previous game session ended without a clean exit (' + (mode || 'unknown') + ')'), 'unclean_exit');
}
addEventListener('pagehide', () => window.rrTelemetry.gameStopped());
