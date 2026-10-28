#!/usr/bin/env node
// Local test server for the browser bundle with online play. Serves a
// tools/build_web.sh dist, optional hosted game assets, and forwards /net
// (WebSocket) and /net/health to a local rr-netd, mirroring the production
// Nginx routes (deploy/road-rash-net/tls-locations.conf). Loopback only.
//
//   node tools/web_dev_server.mjs DIST [--assets DIR] [--port 8765]
//        [--netd-ws 18080] [--netd-http 18081]
import http from 'node:http';
import net from 'node:net';
import fs from 'node:fs';
import path from 'node:path';

const args = process.argv.slice(2);
const option = (name, fallback) => {
    const i = args.indexOf(name);
    return i >= 0 && i + 1 < args.length ? args[i + 1] : fallback;
};
const dist = path.resolve(args[0] || '');
const assets = option('--assets') && path.resolve(option('--assets'));
const port = Number(option('--port', 8765));
const wsPort = Number(option('--netd-ws', 18080));
const httpPort = Number(option('--netd-http', 18081));
if (!args[0] || !fs.existsSync(path.join(dist, 'index.html'))) {
    console.error('usage: web_dev_server.mjs DIST [--assets DIR] [--port N] [--netd-ws N] [--netd-http N]');
    process.exit(2);
}
const types = {'.html': 'text/html', '.js': 'text/javascript', '.css': 'text/css', '.json': 'application/json',
    '.wasm': 'application/wasm', '.png': 'image/png', '.txt': 'text/plain'};
const inside = (root, file) => file === root || file.startsWith(root + path.sep);

const server = http.createServer((req, res) => {
    const url = new URL(req.url, 'http://localhost');
    if (url.pathname === '/net/health') {
        const upstream = http.get({host: '127.0.0.1', port: httpPort, path: '/net/health', timeout: 5000}, r => {
            res.writeHead(r.statusCode, {'content-type': 'application/json', 'cache-control': 'no-store'});
            r.pipe(res);
        });
        upstream.on('error', () => { res.writeHead(502); res.end('{"ok":false}'); });
        return;
    }
    let root = dist, rel = decodeURIComponent(url.pathname);
    if (rel.startsWith('/game-assets/')) {
        if (!assets) { res.writeHead(404); res.end(); return; }
        root = assets; rel = rel.slice('/game-assets'.length);
    }
    const file = path.resolve(root, '.' + (rel === '/' ? '/index.html' : rel));
    if (!inside(root, file) || !fs.existsSync(file) || !fs.statSync(file).isFile()) {
        res.writeHead(404); res.end(); return;
    }
    res.writeHead(200, {'content-type': types[path.extname(file)] || 'application/octet-stream',
        'cache-control': 'no-cache'});
    fs.createReadStream(file).pipe(res);
});
// WebSocket upgrade: replay the request to rr-netd and splice the sockets.
server.on('upgrade', (req, socket, head) => {
    if (new URL(req.url, 'http://localhost').pathname !== '/net') { socket.destroy(); return; }
    const upstream = net.connect(wsPort, '127.0.0.1', () => {
        upstream.write(`${req.method} ${req.url} HTTP/1.1\r\n` +
            Object.entries(req.headers).map(([k, v]) => `${k}: ${v}`).join('\r\n') + '\r\n\r\n');
        if (head.length) upstream.write(head);
        socket.pipe(upstream).pipe(socket);
    });
    const close = () => { socket.destroy(); upstream.destroy(); };
    upstream.on('error', close);
    socket.on('error', close);
});
server.listen(port, '127.0.0.1', () =>
    console.log(`http://localhost:${port}/ -> ${dist}; /net -> 127.0.0.1:${wsPort}, /net/health -> :${httpPort}`));
