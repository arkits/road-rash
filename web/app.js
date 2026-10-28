'use strict';
// Hosted deployments fetch verified assets on demand; local disc import remains available.
const telemetry = window.rrTelemetry || {track() {}, error() {}, log() {}, gameStarted() {}, gameStopped() {}};
const $ = id => document.getElementById(id);
let engine, storageRoot, assetDirectory, references, importing = false, cancelled = false, running = false;
let loadingStage = '', downloadingAssets = 0;
let persistentSaves = false, storageWarning = '';
// Retry transient network/HTTP failures, including reading a truncated body.
// Immutable assets are still size/hash checked by their caller.
async function fetchData(url, decode, options) {
    for (let attempt = 0; ; attempt++) {
        try {
            const response = await fetch(url, options);
            if (!response.ok && response.status !== 404) {
                const error = new Error('Game server returned HTTP ' + response.status);
                error.retryable = response.status >= 500 || response.status === 429;
                throw error;
            }
            return await decode(response);
        } catch (error) {
            if (attempt >= 2 || error.retryable === false) throw error;
            await new Promise(resolve => setTimeout(resolve, 250 * (attempt + 1)));
        }
    }
}
function updateLoading() {
    $('loading-message').textContent = loadingStage || 'Downloading game data…';
    $('loading').hidden = !running || (!loadingStage && !downloadingAssets);
}
function showGameMessage(message, title = 'Game stopped') {
    $('loading-title').textContent = title;
    $('loading-message').textContent = message;
    document.querySelector('.loading-track').hidden = true;
    $('loading').hidden = false;
}
const status = (message, error = false) => { $('status').textContent = message; $('status').classList.toggle('error', error); };
// The original TEXT/*.DLL font files are TrueType data. Use the same
// verified files as TextRenderer.
let discFonts;
async function loadDiscFonts(directory) {
    if (!discFonts) {
        discFonts = Promise.all([
            ['RoadRashBadloc', 'TEXT/BADLOC.DLL'],
            ['RoadRashFutura', 'TEXT/FUTR.DLL'],
            ['RoadRashFuturaBold', 'TEXT/FUTB.DLL'],
            ['RoadRashFuturaDemi', 'TEXT/FUTD.DLL']
        ].map(async ([family, path]) => {
            const file = await (await directory.getFileHandle(encodeURIComponent(path))).getFile();
            const face = await new FontFace(family, await file.arrayBuffer()).load();
            return face;
        })).then(faces => { faces.forEach(face => document.fonts.add(face)); });
    }
    try { await discFonts; }
    catch (error) { discFonts = undefined; throw error; }
}
async function restoreHostedAssets() {
    const manifest = await fetchData('game-assets/manifest.json', response =>
        response.status === 404 ? null : response.json(), {cache: 'no-cache'});
    // An asset-free local build still supports the original disc picker.
    if (!manifest) return false;
    if (manifest.version !== 1 || !Array.isArray(manifest.files) || manifest.files.length !== references.length)
        throw new Error('Game server manifest is incomplete. Reload to retry.');
    const records = new Map(manifest.files.map(file => [file.path, file]));
    for (const reference of references) {
        const record = records.get(reference.path);
        if (!record || record.hash !== reference.hash || !Number.isSafeInteger(record.size) || record.size < 0)
            throw new Error('Game server files need updating. Reload to retry.');
    }
    let cache;
    try { cache = await storageRoot?.getDirectoryHandle('hosted-v1', {create: true}); }
    catch (error) { console.warn('Game cache unavailable:', error); }
    const pending = new Map(), verified = new Set();
    const digest = async bytes => Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256', bytes)),
        byte => byte.toString(16).padStart(2, '0')).join('');
    const read = async path => {
        const record = records.get(path);
        if (!record) throw new Error('Unknown game file: ' + path);
        if (pending.has(path)) return pending.get(path);
        const request = (async () => {
            try { if (cache) {
                const file = await (await cache.getFileHandle(record.hash)).getFile();
                if (file.size === record.size && (verified.has(record.hash) || await digest(await file.arrayBuffer()) === record.hash)) {
                    verified.add(record.hash);
                    return file;
                }
            } } catch (error) {
                if (error.name !== 'NotFoundError') console.warn('Game cache unavailable:', error);
            }
            downloadingAssets++; updateLoading();
            try {
                const bytes = await fetchData('game-assets/' + record.hash, result => {
                    if (result.status === 404) {
                        const error = new Error('Game file unavailable: ' + path);
                        error.retryable = false;
                        throw error;
                    }
                    return result.arrayBuffer();
                });
                if (bytes.byteLength !== record.size || await digest(bytes) !== record.hash)
                    throw new Error('Game file failed verification: ' + path);
                // A full/evicted cache must not prevent playing downloaded data.
                try { if (cache) { await writeFile(cache, record.hash, bytes); verified.add(record.hash); } }
                catch (error) { console.warn('Game file could not be cached:', error); }
                return new File([bytes], path);
            } finally { downloadingAssets--; updateLoading(); }
        })();
        pending.set(path, request);
        try { return await request; } finally { pending.delete(path); }
    };
    // This directory-shaped adapter also supplies the four original fonts.
    assetDirectory = {getFileHandle: async name => ({getFile: () => read(decodeURIComponent(name))})};
    engine.localAssets = {names: new Set(records.keys()), read};
    document.querySelector('.import-actions').hidden = true;
    $('setup-description').textContent = 'Choose how you want to play.';
    $('state').textContent = 'Game ready';
    try { await loadDiscFonts(assetDirectory); }
    catch (error) { console.warn('Game fonts unavailable:', error); }
    $('cache-note').hidden = false;
    if (!cache || storageWarning) $('cache-note').textContent =
        (!cache ? 'Game files will download as you play; caching is unavailable. ' : '') + storageWarning;
    $('ready').hidden = false;
    riderReady();
    startRoomPolling();
    status('Ready to play.');
    telemetry.track('launcher_ready', {source: 'hosted'});
    return true;
}
const sync = populate => new Promise((resolve, reject) => engine.FS.syncfs(populate, e => e ? reject(e) : resolve()));
async function setupStorage() {
    engine.FS.mkdirTree('/persist');
    let mounted = false;
    // A transient IndexedDB failure gets one retry before session-only saves.
    for (let attempt = 0; attempt < 2; attempt++) {
        try {
            engine.FS.mount(engine.FS.filesystems.IDBFS, {autoPersist: true}, '/persist');
            mounted = true;
            await sync(true);
            persistentSaves = true;
            break;
        } catch (error) {
            if (mounted) engine.FS.unmount('/persist');
            mounted = false;
            console.warn('Persistent saves unavailable:', error);
        }
    }
    if (!persistentSaves) storageWarning = 'Saves and settings last only for this session. Allow browser storage to keep them.';
    for (let attempt = 0; attempt < 2; attempt++) {
        try {
            storageRoot = await (await navigator.storage?.getDirectory()).getDirectoryHandle('road-rash', {create: true});
            break;
        } catch (error) { console.warn('Disc cache unavailable:', error); }
    }
}
const call = (name, type = 'number', types = [], values = []) => engine.ccall(name, type, types, values);
async function writeFile(directory, name, bytes) {
    const handle = await directory.getFileHandle(name, {create: true});
    const stream = await handle.createWritable();
    try { await stream.write(bytes); await stream.close(); }
    catch (error) { await stream.abort().catch(() => {}); throw error; }
}
async function readJson(directory, name) {
    return JSON.parse(await (await (await directory.getFileHandle(name)).getFile()).text());
}
function storageError(error) {
    if (error.name === 'QuotaExceededError') return 'Browser storage is full. Free at least 600 MB on this device, then try again.';
    if (error.name === 'SecurityError' || error.name === 'NotAllowedError') return 'Browser storage access is blocked. Allow site storage or use a regular browsing window, then retry.';
    return error.message || String(error);
}
async function restoreDisc() {
    let hasMarker = false;
    try {
        const marker = await readJson(storageRoot, 'active.json');
        hasMarker = true;
        if (marker.version !== 1 || marker.files.length !== references.length) throw new Error('Disc import is incomplete');
        const dir = await storageRoot.getDirectoryHandle(marker.directory);
        const records = new Map(marker.files.map(file => [file.path, file]));
        for (const reference of references) {
            const record = records.get(reference.path);
            if (!record || record.hash !== reference.hash) throw new Error('Disc import needs updating');
            const file = await (await dir.getFileHandle(encodeURIComponent(reference.path))).getFile();
            if (file.size !== record.size) throw new Error('A stored game file is incomplete');
        }
        // Font support must not turn a verified disc into an import failure.
        // Unsupported browsers retain readable fallbacks; supported browsers
        // use the exact same font files as the game before launch is offered.
        try { await loadDiscFonts(dir); }
        catch (error) { console.warn('Local game fonts unavailable:', error); }
        assetDirectory = dir;
        engine.localAssets.names = new Set(references.map(r => r.path));
        $('ready').hidden = false;
        riderReady();
        startRoomPolling();
        $('state').textContent = 'Disc ready';
        status('Your game is ready to play.');
        telemetry.track('launcher_ready', {source: 'local'});
        return true;
    } catch (error) {
        if (hasMarker || error.name !== 'NotFoundError') status('Stored disc unavailable. Re-import your original disc. ' + storageError(error), true);
        return false;
    }
}
async function importDisc(source, iso) {
    if (!engine || !storageRoot || importing || running) return;
    importing = true; cancelled = false;
    telemetry.track('disc_import_start', {source: iso ? 'iso' : 'folder'});
    $('ready').hidden = true; $('progress').hidden = false; $('cancel').hidden = false;
    $('progress').value = 0;
    for (const input of [$('iso'), $('folder')]) input.disabled = true;
    document.querySelectorAll('label.button').forEach(el => el.classList.add('disabled'));
    const directoryName = 'disc-' + crypto.randomUUID();
    let staging;
    try {
        // Preserve the previous complete disc until the new transaction commits.
        const estimate = await navigator.storage.estimate();
        if (estimate.quota && estimate.quota - estimate.usage < 510 * 1024 * 1024)
            throw new DOMException('Not enough room for disc import', 'QuotaExceededError');
        await navigator.storage.persist?.();
        staging = await storageRoot.getDirectoryHandle(directoryName, {create: true});
        const files = new Map();
        if (!iso) {
            for (const file of source) {
                const parts = file.webkitRelativePath.split('/');
                const index = parts.findIndex(p => p.toUpperCase() === 'ROADRASH');
                const relative = parts.slice(index >= 0 ? index + 1 : 1).join('/').toUpperCase();
                if (files.has(relative)) throw new Error('Duplicate disc file: ' + relative);
                files.set(relative, file);
            }
        }
        call('rr_import_begin', null, ['number'], [iso ? 1 : 0]);
        const imported = [];
        for (;;) {
            if (cancelled) throw new Error('Import cancelled. Your previous disc is preserved.');
            const request = call('rr_import_next', 'string');
            const need = JSON.parse(request);
            if (need.error) throw new Error(need.error);
            if (need.done) break;
            if (!iso && !files.has(need.file.toUpperCase())) {
                call('rr_import_missing', null); continue;
            }
            let bytes;
            if (iso) bytes = new Uint8Array(await source.slice(need.offset, need.offset + need.size).arrayBuffer());
            else bytes = new Uint8Array(await files.get(need.file.toUpperCase()).arrayBuffer());
            const pointer = engine._malloc(bytes.length || 1);
            if (!pointer) throw new Error('Not enough memory for disc import');
            try {
                engine.HEAPU8.set(bytes, pointer);
                call('rr_import_supply', null, ['number', 'number'], [pointer, bytes.length]);
            } finally { engine._free(pointer); }
            const output = call('rr_import_output', 'string');
            if (output) {
                const start = call('rr_import_data');
                const size = call('rr_import_size');
                await writeFile(staging, encodeURIComponent(output), engine.HEAPU8.slice(start, start + size));
                imported.push({path: output, size, hash: references.find(r => r.path === output).hash});
                call('rr_import_flushed', null);
            }
            const count = call('rr_import_count');
            $('progress').value = count;
            status(`Verifying disc · ${count} / ${references.length} files${output ? ' · ' + output : ''}`);
            await new Promise(resolve => setTimeout(resolve, 0));
        }
        if (imported.length !== references.length) throw new Error('Disc verification did not produce every required file');
        await writeFile(storageRoot, 'active.json', JSON.stringify({version: 1, directory: directoryName, files: imported}));
        // The marker is the commit point. Cleanup cannot invalidate this import.
        for await (const [name, handle] of storageRoot.entries()) {
            if (handle.kind === 'directory' && name.startsWith('disc-') && name !== directoryName)
                await storageRoot.removeEntry(name, {recursive: true}).catch(() => {});
        }
        await restoreDisc();
        telemetry.track('disc_import_complete');
    } catch (error) {
        if (cancelled) telemetry.track('disc_import_cancelled');
        else telemetry.error(error, 'disc_import');
        call('rr_import_cancel', null);
        if (staging) await storageRoot.removeEntry(directoryName, {recursive: true}).catch(() => {});
        await restoreDisc();
        status(storageError(error), true);
    } finally {
        importing = false; $('progress').hidden = true; $('cancel').hidden = true;
        for (const input of [$('iso'), $('folder')]) { input.disabled = false; input.value = ''; }
        document.querySelectorAll('label.button').forEach(el => el.classList.remove('disabled'));
    }
}
// room: a room code (optionally "@watch:" prefixed) or "@host"/"@rooms";
// the game opens its online screens with it (Frontend::net_join_invite).
async function startGame(quick, room) {
    if (!engine || !assetDirectory || running) return;
    running = true;
    stopRoomPolling();
    const started = performance.now();
    const mode = room ? onlineMode(room) : quick ? 'quick_race' : 'main_game';
    engine.assetError = ''; engine.gameError = ''; engine.gameException = undefined;
    telemetry.track('game_start', {mode, ...(quick ? {course: Number($('course').value), character: Number($('character').value)} : {})});
    telemetry.gameStarted(mode);
    document.body.classList.add('playing');
    loadingStage = 'Starting game…'; updateLoading();
    $('setup').hidden = true; $('canvas').hidden = false; $('loading').hidden = false;
    $('fullscreen').disabled = !fullscreenRequest(); $('reload').hidden = false;
    $('state').textContent = 'Playing';
    $('canvas').focus();
    const allocated = [];
    try {
        // Create/resume audio within the launch gesture, before asynchronous reads.
        const Audio = window.AudioContext || window.webkitAudioContext;
        if (Audio) {
            engine.SDL3 = engine.SDL3 || {};
            engine.SDL3.audioContext = engine.SDL3.audioContext || new Audio();
            engine.SDL3.audioContext.resume().catch(() => {});
        }
        const args = ['rr-game', '--assets', '/assets', '--windowed', '--window-scale', '1'];
        if (quick) args.push('--race', '--course', $('course').value, '--character', $('character').value);
        // Online play uses rr-netd on this page's origin, where /net/health lists rooms.
        if (room) args.push('--room', room);
        args.push('--net-server', (location.protocol === 'https:' ? 'wss://' : 'ws://') + location.host + '/net');
        const argv = engine._malloc((args.length + 1) * 4); allocated.push(argv);
        if (!argv) throw new Error('Not enough memory to start the game');
        args.forEach((arg, index) => {
            const ptr = engine._malloc(engine.lengthBytesUTF8(arg) + 1); allocated.push(ptr);
            if (!ptr) throw new Error('Not enough memory to start the game');
            engine.stringToUTF8(arg, ptr, engine.lengthBytesUTF8(arg) + 1);
            new DataView(engine.HEAPU8.buffer).setUint32(argv + index * 4, ptr, true);
        });
        new DataView(engine.HEAPU8.buffer).setUint32(argv + args.length * 4, 0, true);
        const code = await engine.ccall('main', 'number', ['number', 'number'], [args.length, argv], {async: true});
        if (code) throw engine.gameException || new Error(engine.assetError || engine.gameError || 'The game stopped with exit code ' + code + '.');
        if (persistentSaves) {
            try { await sync(false); }
            catch (error) { telemetry.error(error, 'save'); storageWarning = 'Your latest saves could not be stored. Allow browser storage before playing again.'; }
        }
        telemetry.track('game_exit', {mode, duration_ms: Math.round(performance.now() - started)});
        showGameMessage((storageWarning ? storageWarning + ' ' : '') + 'Use Exit to launcher to play again.', 'Game closed');
    } catch (error) {
        telemetry.error(error, 'game');
        showGameMessage(storageError(error) + ' Use Exit to launcher to try again.');
    } finally {
        running = false;
        $('fullscreen').disabled = true;
        telemetry.gameStopped(); allocated.forEach(ptr => { if (ptr) engine._free(ptr); });
    }
}
$('iso').addEventListener('change', e => e.target.files[0] && importDisc(e.target.files[0], true));
$('folder').addEventListener('change', e => e.target.files.length && importDisc(e.target.files, false));
$('cancel').onclick = () => { cancelled = true; };
$('play').onclick = () => startGame(false);
// Online (mano a mano). The launcher lists public rooms from rr-netd's
// /net/health (same origin as the hosted game); joining and hosting happen in
// the game's own online screens, opened through --room.
const codeAlphabet = '23456789ABCDEFGHJKMNPQRSTUVWXYZ';
function parseRoomCode(text) {
    let value = String(text || '').trim();
    const query = value.indexOf('room=');
    if (query >= 0) value = value.slice(query + 5).split(/[&#]/)[0];
    value = value.replace(/[\s\-/]/g, '').toUpperCase();
    return value.length === 6 && [...value].every(c => codeAlphabet.includes(c)) ? value : '';
}
const showCode = code => code.slice(0, 3) + '-' + code.slice(3);
const onlineMode = room => room === '@host' ? 'online_host' : room === '@rooms' ? 'online_browse'
    : room.startsWith('@watch:') ? 'online_watch' : 'online_join';
const joinRoom = (code, watch) => startGame(false, (watch ? '@watch:' : '') + code);
$('net-host').onclick = () => startGame(false, '@host');
$('net-browse').onclick = () => startGame(false, '@rooms');
$('net-code').addEventListener('input', () => {
    const value = $('net-code').value, code = parseRoomCode(value);
    // A pasted invite link collapses to its code.
    if (code && value.includes('room=')) $('net-code').value = showCode(code);
    $('net-join-go').disabled = !code;
    $('net-code').setAttribute('aria-invalid', String(Boolean(value.trim()) && !code));
});
$('net-code').addEventListener('change', () => {
    const code = parseRoomCode($('net-code').value);
    if (code) $('net-code').value = showCode(code);
});
$('net-join').addEventListener('submit', event => {
    event.preventDefault();
    const code = parseRoomCode($('net-code').value);
    if (code) joinRoom(code, $('net-watch').checked);
});
// An invite link (?room=CODE) is offered explicitly. It is removed from the
// address so Start Game keeps its normal meaning; the game also reads it.
const invited = parseRoomCode(new URLSearchParams(location.search).get('room'));
if (invited) {
    $('invite-code').textContent = showCode(invited);
    $('invite').hidden = false;
    $('net-code').value = showCode(invited);
    $('net-join-go').disabled = false;
    $('invite-join').onclick = () => joinRoom(invited, false);
    $('invite-watch').onclick = () => joinRoom(invited, true);
    const url = new URL(location.href);
    url.searchParams.delete('room');
    history.replaceState(null, '', url);
}
const emptyRooms = $('net-empty').textContent;
const roomStatus = {lobby: 'Open', full: 'Full', racing: 'Racing'};
function renderRooms(rooms) {
    const list = $('net-rooms');
    list.replaceChildren(...rooms.map(room => {
        const row = document.createElement('li');
        const info = document.createElement('div');
        const host = document.createElement('span');
        host.className = 'room-host';
        host.textContent = room.host || 'Rider';
        const meta = document.createElement('span');
        meta.className = 'room-meta';
        meta.textContent = `Level ${room.level + 1} · ${room.humans}/${room.seats} riders · ${showCode(room.code)}`;
        info.append(host, meta);
        const chip = document.createElement('span');
        chip.className = 'room-chip';
        chip.dataset.status = room.status;
        chip.textContent = roomStatus[room.status] || room.status;
        const watch = room.status !== 'lobby';
        const button = document.createElement('button');
        button.type = 'button';
        button.textContent = watch ? 'Watch' : 'Join';
        button.setAttribute('aria-label', `${button.textContent} ${host.textContent}'s room ${showCode(room.code)}`);
        button.onclick = () => joinRoom(room.code, watch);
        row.append(info, chip, button);
        return row;
    }));
    $('net-empty').hidden = rooms.length > 0;
}
function setNetStatus(state, text) {
    $('net-status').dataset.state = state;
    $('net-status').lastElementChild.textContent = text;
}
let roomTimer, roomRequest;
async function refreshRooms() {
    if (running || document.hidden) return;
    roomRequest?.abort();
    const request = roomRequest = new AbortController();
    try {
        const response = await fetch('/net/health', {cache: 'no-store', signal: request.signal});
        const health = response.ok ? await response.json() : null;
        if (!health?.ok) throw new Error('Server unavailable');
        const rooms = (Array.isArray(health.public_rooms) ? health.public_rooms : [])
            .filter(room => parseRoomCode(room.code) && Number.isInteger(room.level) && Number.isInteger(room.humans));
        const players = Number(health.players) || 0;
        setNetStatus('online', `${players} ${players === 1 ? 'rider' : 'riders'} online`);
        $('net-empty').textContent = emptyRooms;
        renderRooms(rooms);
    } catch (error) {
        if (request.signal.aborted) return;
        setNetStatus('offline', 'Server offline');
        renderRooms([]);
        $('net-empty').textContent = 'The multiplayer server is not reachable from this page. You can still try hosting or joining in game.';
    }
}
function startRoomPolling() {
    clearInterval(roomTimer);
    refreshRooms();
    roomTimer = setInterval(refreshRooms, 8000);
}
function stopRoomPolling() { clearInterval(roomTimer); roomRequest?.abort(); }
// Rider picker, modelled on the original character select (0x00421df0
// case 0xe). Portraits, stats, bios and the in-race sprite are read at run
// time from the player's verified disc files; nothing disc-derived ships.
const riderFiles = ['Axle', 'Bose', 'Cydney', 'Jon', 'Pearl', 'Rhonda', 'Jim', 'Mike'];
const riderRows = [8, 7, 2, 5, 4, 6, 3, 1];                          // 0x00415fc0 character -> rider row
const showroomBikes = [0, 1, 2, 4, 3, 10, 12, 14, 11, 13, 5, 7, 9, 6, 8]; // 0x0041ff00 name slot 0x80+i -> bike
const riderLean = [3, 6, 5, 6, 3, 8, 9, 8];                            // HIBOB riding frames: straight, right, left
const riderNames = [...document.querySelectorAll('#rider-roster [role=radio]')].map(button => button.textContent);
let roster, rosterLoad, riderSprites, riderSpriteLoad, riderPreview = 1, riderTimer;
const portraits = new Map();
async function discBytes(path) {
    const file = await (await assetDirectory.getFileHandle(encodeURIComponent(path))).getFile();
    return new Uint8Array(await file.arrayBuffer());
}
function peImage(bytes) {
    const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
    if (view.getUint16(0, true) !== 0x5a4d) throw new Error('Not a PE image');
    const pe = view.getUint32(60, true), table = pe + 24 + view.getUint16(pe + 20, true);
    const sections = Array.from({length: view.getUint16(pe + 6, true)}, (_, i) => ({
        va: view.getUint32(table + 40 * i + 12, true), size: view.getUint32(table + 40 * i + 16, true),
        raw: view.getUint32(table + 40 * i + 20, true)}));
    const offset = (rva, count) => {
        const s = sections.find(s => rva >= s.va && rva - s.va + count <= s.size);
        if (!s) throw new Error('PE address has no file data');
        return s.raw + rva - s.va;
    };
    const base = view.getUint32(pe + 52, true);
    return {bytes, view, offset, resources: view.getUint32(pe + 136, true),
        data: (address, count) => bytes.subarray(offset(address - base, count), offset(address - base, count) + count)};
}
// RT_STRING blocks hold 16 counted UTF-16 strings; block n starts at id (n-1)*16.
function peStrings(pe) {
    const {view} = pe, base = pe.offset(pe.resources, 16), strings = new Map(), utf16 = new TextDecoder('utf-16le');
    const entries = dir => Array.from({length: view.getUint16(base + dir + 12, true) + view.getUint16(base + dir + 14, true)},
        (_, i) => [view.getUint32(base + dir + 16 + 8 * i, true), view.getUint32(base + dir + 20 + 8 * i, true) & 0x7fffffff]);
    for (const [type, types] of entries(0)) {
        if (type !== 6) continue;
        for (const [block, languages] of entries(types))
            for (const [, data] of entries(languages)) {
                const size = view.getUint32(base + data + 4, true);
                let p = pe.offset(view.getUint32(base + data, true), size);
                for (let k = 0; k < 16; k++) {
                    const n = view.getUint16(p, true); p += 2;
                    if (n && !strings.has((block - 1) * 16 + k)) strings.set((block - 1) * 16 + k, utf16.decode(pe.bytes.subarray(p, p + 2 * n)));
                    p += 2 * n;
                }
            }
    }
    return strings;
}
const formatC = (format, args) => { let i = 0; return format.replace(/%([%sdc])/g, (_, c) => c === '%' ? '%' : String(args[i++])); };
// Frontend::prepare_characters for the quick race (level 1, imperial units).
async function loadRoster() {
    const [exe, text] = await Promise.all([discBytes('ROADRASH.EXE'), discBytes('TEXT/ENU.DLL')]);
    const pe = peImage(exe), strings = peStrings(peImage(text)), str = id => strings.get(id) || '';
    const cash = pe.data(0x00469000, 18), levelBikes = pe.data(0x004641c8, 16 * 5);
    const entries = riderNames.map((name, i) => {
        const row = riderRows[i], body = pe.data(0x00463c78 + row * 0x40 + 8, 8);
        const inches = body[0] | body[1] << 8, pounds = body[4] | body[5] << 8;
        const heightFormat = str(4000);
        const height = formatC(heightFormat.replace(/^[+-]/, ''), [Math.floor(inches / 12), inches % 12]);
        const bike = levelBikes[row * 5];
        const bikeName = str(0x80 + Math.max(0, showroomBikes.lastIndexOf(bike)));
        const lines = formatC(str(40 + i), [height, formatC(str(4001), [pounds]), bikeName, cash[2 * (i + 1)] | cash[2 * (i + 1) + 1] << 8]).split('\n');
        return {name, bike, bikeName, stats: lines.slice(0, 4), bio: lines.slice(4).join(' ')};
    });
    return {entries, remaps: pe.data(0x0046e158, 15 * 256)};
}
async function loadRiderSprites() {
    const [bob, palette] = await Promise.all([discBytes('DATA/BIKERS/HIBOB.DAT'), discBytes('DATA/PALETTE.RAW')]);
    const frames = [];
    for (let p = 0, i = 0; i <= Math.max(...riderLean); i++) {
        const width = bob[p], height = bob[p + 1];
        frames.push({width, height, pixels: bob.subarray(p + 2, p + 2 + width * height)});
        p += 2 + width * height;
    }
    const canvas = $('rider-sprite');
    canvas.width = Math.max(...riderLean.map(i => frames[i].width));
    canvas.height = Math.max(...riderLean.map(i => frames[i].height));
    return {frames, palette};
}
function portrait(c) {
    if (!portraits.has(c)) portraits.set(c, discBytes(`IMAGES/CHARS/${riderFiles[c - 1].toUpperCase()}-N.RRI`)
        // .RRI is a JPEG. A data: URL, because the deployed CSP (img-src 'self' data:) blocks blob:.
        .then(bytes => new Promise((resolve, reject) => {
            const reader = new FileReader();
            reader.onload = () => resolve(reader.result);
            reader.onerror = () => reject(reader.error);
            reader.readAsDataURL(new Blob([bytes], {type: 'image/jpeg'}));
        }))
        .catch(error => { portraits.delete(c); throw error; }));
    return portraits.get(c);
}
// HIBOB frame through the bike's colour remap (0x00443820, 0x00446190).
function drawRider(frameIndex) {
    const canvas = $('rider-sprite'), entry = roster?.entries[riderPreview - 1];
    if (!riderSprites || !entry) return;
    const frame = riderSprites.frames[frameIndex], {palette} = riderSprites;
    const context = canvas.getContext('2d'), image = context.createImageData(canvas.width, canvas.height);
    const left = (canvas.width - frame.width) >> 1, top = canvas.height - frame.height;
    for (let y = 0; y < frame.height; y++)
        for (let x = 0; x < frame.width; x++) {
            const v = frame.pixels[y * frame.width + x];
            if (!v) continue;
            const c = roster.remaps[entry.bike * 256 + v] * 4, o = ((top + y) * canvas.width + left + x) * 4;
            image.data.set([palette[c], palette[c + 1], palette[c + 2], 255], o);
        }
    context.putImageData(image, 0, 0);
}
function animateRider() {
    clearInterval(riderTimer);
    let step = 0;
    drawRider(riderLean[0]);
    if (matchMedia('(prefers-reduced-motion: reduce)').matches) return;
    riderTimer = setInterval(() => drawRider(riderLean[++step % riderLean.length]), 220);
}
function previewRider(c) {
    riderPreview = c;
    const entry = roster?.entries[c - 1];
    document.querySelectorAll('#rider-roster [role=radio]').forEach(button => {
        const on = Number(button.value) === c;
        button.setAttribute('aria-checked', String(on));
        button.tabIndex = on ? 0 : -1;
    });
    $('rider-title').textContent = $('rider-confirm-name').textContent = riderNames[c - 1];
    $('rider-stats').replaceChildren(...(entry ? entry.stats : []).map(line => Object.assign(document.createElement('span'), {textContent: line})));
    $('rider-bio').textContent = entry ? entry.bio : '';
    $('rider-bike-name').textContent = entry ? entry.bikeName : '';
    $('rider-portrait').classList.remove('shown');
    portrait(c).then(url => {
        if (riderPreview !== c) return;
        $('rider-portrait').src = url;
        $('rider-portrait').classList.add('shown');
    }).catch(error => console.warn('Rider portrait unavailable:', error));
    if (riderSprites && entry) animateRider();
}
function setRider(c) {
    $('character').value = String(c);
    $('rider-name').textContent = riderNames[c - 1];
    $('rider-bike').textContent = roster?.entries[c - 1].bikeName || 'Choose your rider';
    try { localStorage.setItem('rr-rider', String(c)); } catch {}
    portrait(c).then(url => { if ($('character').value === String(c)) $('rider-thumb').src = url; })
        .catch(error => console.warn('Rider portrait unavailable:', error));
}
function riderReady() {
    let saved = 1;
    try { saved = Number(localStorage.getItem('rr-rider')) || 1; } catch {}
    rosterLoad ||= loadRoster().then(result => { roster = result; }).catch(error => {
        rosterLoad = undefined;
        console.warn('Rider details unavailable:', error);
    });
    setRider(saved >= 1 && saved <= 8 ? saved : 1);
    rosterLoad.then(() => setRider(Number($('character').value)));
}
$('rider-button').onclick = async () => {
    previewRider(Number($('character').value));
    $('rider-dialog').showModal();
    $('rider-roster').querySelector('[aria-checked=true]').focus();
    await (rosterLoad ||= loadRoster().then(result => { roster = result; }).catch(error => { rosterLoad = undefined; console.warn(error); }));
    riderSpriteLoad ||= loadRiderSprites().then(result => { riderSprites = result; })
        .catch(error => { riderSpriteLoad = undefined; console.warn('Rider sprite unavailable:', error); });
    await riderSpriteLoad;
    $('rider-bike-view').hidden = !riderSprites;
    if ($('rider-dialog').open) previewRider(riderPreview);
};
$('rider-roster').addEventListener('click', event => {
    const button = event.target.closest('[role=radio]');
    if (button) previewRider(Number(button.value));
});
$('rider-roster').addEventListener('dblclick', event => {
    if (event.target.closest('[role=radio]')) $('rider-confirm').click();
});
$('rider-roster').addEventListener('keydown', event => {
    const step = {ArrowDown: 1, ArrowRight: 1, ArrowUp: -1, ArrowLeft: -1}[event.key];
    let next;
    if (step) next = (riderPreview - 1 + step + 8) % 8 + 1;
    else if (event.key === 'Home') next = 1;
    else if (event.key === 'End') next = 8;
    else if (event.key === 'Enter') { event.preventDefault(); $('rider-confirm').click(); return; }
    else return;
    event.preventDefault();
    previewRider(next);
    $('rider-roster').querySelector('[aria-checked=true]').focus();
});
$('rider-dialog').addEventListener('close', () => {
    clearInterval(riderTimer);
    if ($('rider-dialog').returnValue === 'ride') {
        setRider(riderPreview);
        telemetry.track('rider_select', {character: riderPreview});
    }
    $('rider-dialog').returnValue = '';
    $('rider-button').focus();
});
$('quick').onclick = () => startGame(true);
$('reload').onclick = () => { telemetry.track('launcher_exit'); location.reload(); };
function fullscreenRequest() {
    const viewport = document.querySelector('.viewport');
    return viewport.requestFullscreen || viewport.webkitRequestFullscreen;
}
$('fullscreen').onclick = async () => {
    const request = fullscreenRequest();
    if (!request) { $('fullscreen').disabled = true; return; }
    try { await request.call(document.querySelector('.viewport')); telemetry.track('fullscreen'); }
    // A denied fullscreen gesture must leave the running game visible.
    catch (error) { status('Fullscreen unavailable. You can keep playing in this window.'); }
};
// SDL receives these events too; prevent browser scrolling while the game owns focus.
document.addEventListener('visibilitychange', () => {
    if (running) call('rr_web_visibility', null, ['number'], [document.hidden ? 1 : 0]);
    else if (!document.hidden) refreshRooms();
});
window.addEventListener('keydown', event => {
    if (running && document.activeElement === $('canvas') && ['ArrowUp', 'ArrowDown', 'ArrowLeft', 'ArrowRight', ' ', 'Backspace'].includes(event.key)) event.preventDefault();
});
(async () => {
    try {
        if (!window.isSecureContext) throw new Error('Open this page over HTTPS to play.');
        references = await fetchData('references.json', response => {
            if (!response.ok) throw new Error('Game reference list unavailable. Reload to retry.');
            return response.json();
        });
        // SDL's Asyncify presentation and local file reads may yield more than
        // once between draws. Keep the last completed WebGL frame visible.
        const canvas = $('canvas');
        const getContext = canvas.getContext.bind(canvas);
        canvas.getContext = (kind, attributes) => getContext(kind,
            kind.startsWith('webgl') || kind === 'experimental-webgl'
                ? {...attributes, preserveDrawingBuffer: true} : attributes);
        for (let attempt = 0; ; attempt++) {
            try {
                engine = await createRoadRash({
                    canvas: $('canvas'), noInitialRun: true,
                    print: console.log,
                    printErr: message => { console.warn(message); telemetry.log(message); },
                    preRun: [module => { module.ENV.RR_PREF_OVERRIDE = '/persist'; }],
                    onAbort: reason => {
                        // Factory failures are retried below and reported only if all
                        // attempts fail. A live engine abort is immediately actionable.
                        if (!running) return;
                        running = false; telemetry.gameStopped();
                        telemetry.error(new Error(String(reason)), 'engine_abort');
                        showGameMessage('Engine error: ' + reason + '. Use Exit to launcher to restart.');
                    }
                });
                break;
            } catch (error) {
                if (attempt >= 2) throw error;
                await new Promise(resolve => setTimeout(resolve, 250 * (attempt + 1)));
            }
        }
        engine.webLoading = stage => { loadingStage = stage; updateLoading(); };
        await setupStorage();
        engine.localAssets = {
            names: new Set(),
            read: async path => {
                try { return await (await assetDirectory.getFileHandle(encodeURIComponent(path))).getFile(); }
                catch (error) { throw new Error('Stored game file missing: ' + path + '. Re-import your disc.'); }
            }
        };
        status('Connecting to game server…');
        if (!await restoreHostedAssets()) {
            if (!storageRoot) throw new Error('Local disc import needs browser storage. Allow site storage or use a regular browsing window, then retry.');
            document.querySelector('.import-actions').hidden = false;
            $('setup-description').textContent = 'Choose your original PC disc image (.iso) or the ROADRASH folder. Game files stay on this device.';
            status('Engine ready. Select your original PC disc. Allow about 500 MB of browser storage.');
            await restoreDisc();
        }
        for (const input of [$('iso'), $('folder')]) input.disabled = false;
        document.querySelectorAll('label.button').forEach(el => el.classList.remove('disabled'));
    } catch (error) { telemetry.error(error, 'initialization'); status(storageError(error), true); $('retry').hidden = false; }
})();
$('retry').onclick = () => { telemetry.track('connection_retry'); location.reload(); };
