# Multiplayer server deployment

Status (October 27, 2026): `road-rash-net` is provisioned on agnee and serves
`wss://road-rash.archit.xyz/net` plus HTTPS `/net/health`. Native production
bots passed a 420-tick agreed cutoff. The dedicated restricted deploy key and
`RR_NET_DEPLOY_ENABLED=true` are installed; the enabled CI production run is
recorded in the live handoff. WebRTC is implemented as an opt-in transport (`--rtc`, browser `?rtc=1`).
The Task 9 live UDP acceptance receipt is recorded in its handoff.

## Package and workflow

`server/Dockerfile` builds a stripped Linux amd64 Release daemon with its exact
Git revision. The workflow copies that binary into an archive containing only
`rr-netd`, `Dockerfile` and `app.json`. The runtime uses Debian bookworm's SSL
libraries, runs as uid/gid 999, and has no disc files. Startup health checks use
container port 8081 and `/net/health`; the public check also requires the exact
Git revision. The WebSocket listener is container port 8080.

The deploy credential is a separate `DOKKU_ROAD_RASH_NET_KEY`. Its root
`authorized_keys` entry must force only the archive command, matching the
existing web deployment's restrictions:

```
restrict,command="/usr/local/sbin/road-rash-net-deploy" ssh-ed25519 PUBLIC_KEY road-rash-net-ci
```

Install the root-owned `deploy.py` as `/usr/local/sbin/road-rash-net-deploy`
(mode 755) before installing this entry. The helper targets only this app,
validates a bounded code archive and retains Dokku startup checks.
Keep the private key out of logs/Git; feed its file to `gh secret set
DOKKU_ROAD_RASH_NET_KEY --repo arkits/road-rash < KEY_FILE`. The workflow
uses the already authenticated `DOKKU_ROAD_RASH_KNOWN_HOSTS` variable. No host-key
scan or unchecked host-key acceptance is used.

## Provisioning reference (already installed)

Use the authorized operator identity (`root@agnee` for host changes).
Inspect `dokku version`, apps, port listeners, the existing TLS file, firewall
and renewal hook first. Record backups of files before changing them. Preserve
other applications and any unrelated configuration.

For the new app only (first confirm ports 18080/18081 are free):

```sh
dokku apps:create road-rash-net
dokku proxy:set road-rash-net nginx
dokku domains:set road-rash-net road-rash-net.internal
dokku nginx:set road-rash-net bind-address-ipv4 127.0.0.1
dokku nginx:set road-rash-net bind-address-ipv6 ::1
dokku ports:set road-rash-net http:18080:8080 http:18081:8081
sudo install -d -m 750 -o 999 -g 999 /var/lib/dokku/data/storage/road-rash-net
dokku storage:mount road-rash-net /var/lib/dokku/data/storage/road-rash-net:/data
```

Verified on Dokku 0.38.25. Its global default is Caddy, so the app-specific
`proxy:set ... nginx` is required; the global proxy setting is unchanged. Dokku
owns the loopback HTTP listeners and updates their container upstreams on each
deploy; fixed Docker host-port bindings would prevent overlapping deployment
health checks. Bind both listeners to loopback, and confirm that with `ss`.
Install the dedicated forced-command public key and deploy the code archive.
Check internal health and revision before exposing the route.

Install `tls-locations.conf` as `/etc/nginx/snippets/road-rash-net.conf` and add
its include inside the existing `road-rash.archit.xyz` TLS server block, as
shown in `../road-rash-tls.conf`. Merge with the current host file; do not blindly
replace it. `nginx -t` must pass before reload. Recheck web `/healthz`, existing
revision/assets, `/net/health`, WebSocket upgrade and the Certbot renewal hook.
Do not change certificate/challenge handling or other host virtual servers.

When the app, routes and key have passed those checks:

```sh
gh variable set RR_NET_DEPLOY_ENABLED --repo arkits/road-rash --body true
gh workflow run net-deploy.yml --repo arkits/road-rash --ref main
```

The enabled workflow fetches 341 hash-verified files into `$RUNNER_TEMP`, mounts
them read-only into a separate Linux bot container and runs two real rollback
clients against `wss://road-rash.archit.xyz/net`. The scripted 420-tick cutoff
must produce matching hashes and agreed official results. This is a short
transport/determinism check, not a completed lap. Game data enters neither the
image nor uploaded artifact and is removed in the always-run cleanup step.

## UDP and Task 9

Only IPv4 UDP 40000–40099 to 5.161.142.20 is allowed for this app. The deploy
helper chooses one free port in that range per generation and publishes it
one-to-one using `--publish=5.161.142.20:PORT:PORT/udp`. It sets
`RR_NETD_RTC=1`, `RR_NETD_PUBLIC_IP=5.161.142.20` and both `RR_NETD_UDP_BEGIN`
and `RR_NETD_UDP_END` to that port; `RR_NETD_UDP_PUBLISHED` records the managed
mapping. libjuice UDP mux lets every client peer share that socket. Bare-metal
server deployments can configure a wider local UDP range using the same env.

Old and new generations use different ports, so ordinary rolling startup
health checks remain enabled. No fixed-range bind collision, skipped checks,
STUN mapping service or change to other apps is needed. On a failed deploy the
helper restores the previous future-container options/config; the old healthy
container keeps its original live mapping. Replaying an identical archive
forces `ps:rebuild` if `git:from-archive` otherwise skips it, ensuring the new
mapping is actually applied. Seven isolated tests cover failure restoration,
first deploy, archive rejection and replay/changed-generation behavior.

Race Input/Ack/Frames use unordered, zero-retransmit DataChannel when open.
Controls, logs and results retain WebSocket. After 5 seconds without opening,
or on channel close/error, a reliable type-29 `RtcClose` message makes both
ends permanently resume WebSocket on that connection. Lobby/race/rematch all
keep the same wrapper/peer. Existing wire IDs are unchanged; old WS-only peers
can ignore this additive transport control.

Native clients opt in with `--rtc`; bots `--require-rtc` additionally require
positive send/receive counters for every client. `--no-udp` forces WebSocket;
`--rtc-close-tick N` exercises mid-race failure. Browser `?rtc=1` opts in,
`?rtc=1&udp=off` disables it. Native and browser fixtures test real peers plus
cross-engine traffic. The default remains WebSocket: at real 5% netem loss,
DataChannel cut late inputs/stalls but did not improve p99 rollback and used
about 2.8x outgoing bytes. Full measurements are in VERIFICATION.md.

## Rollback

Disable `RR_NET_DEPLOY_ENABLED` to stop further deploys. Redeploy the retained
code-only archive for the previous known-good revision through the same
restricted key, and require that revision in health. Failed startup checks
retain Dokku's previous container; a failed post-deploy smoke needs an explicit
rollback. Keep persistent race logs (the daemon prunes after seven days).
For a route rollback, restore only the saved TLS/include change, run `nginx -t`
and reload. Do not remove or reconfigure the existing web app.

Configuration references: [Dokku port management](https://dokku.com/docs/networking/port-management/),
[Nginx proxy properties](https://dokku.com/docs/networking/proxies/nginx/),
[startup health checks](https://dokku.com/docs/deployment/zero-downtime-deploys/).
