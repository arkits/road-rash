# Browser hosting on agnee

Production: https://road-rash.archit.xyz — Dokku app `road-rash` on
`agnee` (5.161.142.20), with Cloudflare proxied DNS. The owner explicitly
requested hosting the original game files on this VPS on October 25, 2026;
that request supersedes the earlier local-only web deployment policy for
this deployment. Commercial files remain outside Git, CI artifacts and images.

Cloudflare configuration rule `Road Rash strict origin HTTPS` matches only
this hostname and uses strict certificate verification. Host Nginx terminates
TLS using a Let's Encrypt certificate, then proxies to Dokku's generated HTTP
virtual host on loopback. `deploy/road-rash-tls.conf` is installed at
`/etc/nginx/conf.d/road-rash-tls.conf`. It binds only the public IPv4 address
to preserve the existing Tailscale HTTPS listener. Other apps' proxy/SSL
settings and the server firewall remain unchanged.

Certbot's system timer renews the certificate using webroot
`/var/lib/dokku/data/road-rash-acme`; Dokku's app-specific
`nginx.conf.d/acme.conf` serves the challenge directory. The deploy hook
`/etc/letsencrypt/renewal-hooks/deploy/road-rash-nginx` validates/reloads
Nginx after renewal. `nginx.conf.d/https-redirect.conf` redirects Cloudflare
requests whose original scheme was HTTP; the local TLS proxy sets the
forwarded scheme to HTTPS, avoiding redirect loops.

## Deployment

`.github/workflows/web-deploy.yml` builds the shared C++ game with pinned
Emscripten 6.0.10 on pushes to main affecting game/build/web/deployment files,
or by manual workflow dispatch. It validates the seven-file code bundle,
packages an Nginx Dockerfile, config, app.json and distribution, and streams
the archive over SSH into `dokku git:from-archive road-rash --`.

The dedicated `DOKKU_ROAD_RASH_KEY` GitHub secret is a deploy-only key. Its
server authorized_keys entry forces this one Dokku command and restricts
forwarding/PTY access. It cannot open an interactive root shell. As with any
Docker deployment credential, protect it as a production deployment credential.
`DOKKU_ROAD_RASH_KNOWN_HOSTS` contains the host's Ed25519 public key, obtained
through the existing authenticated connection. The workflow checks this key;
it does not trust an unauthenticated scan on each run.

Dokku healthchecks require both the web endpoint and mounted asset manifest.
The workflow verifies HTTPS health and the exact Git revision, then launches
a fresh Chromium session and checks automatic assets, hash rejection, cache
repair, visible race rendering, keyboard input and nonzero audio. Evidence is
retained as a GitHub Actions artifact for seven days. Deployment jobs serialize.
Failed startup healthchecks preserve the previous container. Post-deployment
browser test failures fail the workflow; they do not automatically roll back.

## Persistent assets

The 341 reference paths total 489,500,591 logical bytes. Store them at
`/home/arkits/road-rash-assets`, mounted read-only into the app at
`/srv/game-assets`. Content-addressed filenames are SHA-256 hashes; the
manifest maps original paths to hashes and sizes. Identical content deduplicates.
All files were verified locally and again after transfer. Do not commit them.

To reprovision from the original disc:

```sh
python3 tools/export_web_assets.py ../cdrom/ROADRASH .tools/hosted-game-assets
rsync -az --exclude manifest.json .tools/hosted-game-assets/ arkits@agnee:/home/arkits/road-rash-assets/
scp .tools/hosted-game-assets/manifest.json arkits@agnee:/home/arkits/road-rash-assets/manifest.json.next
ssh arkits@agnee 'mv /home/arkits/road-rash-assets/manifest.json.next /home/arkits/road-rash-assets/manifest.json'
```

Transfer immutable objects first, then atomically publish the manifest. Keep
old hash files while older clients/deployments might still reference them.

The launcher validates the hosted manifest against compiled reference hashes.
Only fonts load before launch. Other files download on demand, verify SHA-256
and size, then cache in OPFS. Cached bytes are checked on first use each page
session; corrupt/missing data is fetched again. Cache write failures still allow
playing downloaded files. A local asset-free server returning manifest 404 keeps
the original disc import mode. HTTPS and OPFS remain browser requirements.

Releases do not rely on clearing users' caches. Browser engine/app/CSS/reference
files now have content hashes in their filenames and immutable cache headers;
the HTML entrypoint uses no-store. The engine loader embeds the exact hashed
Wasm filename before the loader itself is hashed. This prevents Cloudflare or
browser caches from mixing an older loader with a newly deployed Wasm import
table. CI tests an upgrade from a poisoned, four-hour cached legacy loader
before deployment. The hosted game-assets manifest still revalidates separately.
Servers support byte ranges.
The engine still loads requested media as whole files; this is on-demand file
loading, not progressive video streaming. Startup still needs network access
for the manifest; offline launch is not implemented.

## Operations

```sh
ssh arkits@agnee 'dokku ps:report road-rash'
ssh arkits@agnee 'dokku logs road-rash --num 100'
ssh arkits@agnee 'dokku storage:report road-rash'
curl --fail https://road-rash.archit.xyz/healthz
curl --fail https://road-rash.archit.xyz/revision.txt
```

Native gameplay fidelity gaps remain tracked in CURRENT_STATUS.md; deployment
checks do not establish original-game parity or mobile/Safari acceptance.


## Multiplayer service (October 27, 2026)

Production WebSocket: `wss://road-rash.archit.xyz/net`; health:
`https://road-rash.archit.xyz/net/health`. Dokku app `road-rash-net` runs the
Linux amd64 code-only image with persistent `/data` mounted from
`/var/lib/dokku/data/storage/road-rash-net` (uid/gid 999). The daemon keeps race
logs for seven days. No game files are installed in this app/image.

The app explicitly selects Nginx (Dokku 0.38.25's global default remains Caddy).
Its private listeners are `127.0.0.1` / `::1` ports 18080 (container WS 8080) and
18081 (container health 8081). The existing TLS server includes
`/etc/nginx/snippets/road-rash-net.conf` for `/net` and `/net/health` only.
Other app/proxy settings and firewall rules are unchanged. The TLS file backup
is `/etc/nginx/conf.d/road-rash-tls.conf.pre-net-20261027-80613`.

`DOKKU_ROAD_RASH_NET_KEY` is restricted to
`/usr/bin/dokku git:from-archive road-rash-net --`; the local temporary private
key was deleted after installation/verification. `RR_NET_DEPLOY_ENABLED=true`
enables deployment and a two-client Linux CI production smoke after exact
revision health checks. Code archives are retained for seven days. The script
fetches verified data into ephemeral CI storage and never uploads/includes it
in an artifact/image. See [runbook](../deploy/road-rash-net/README.md) for
provisioning, artifact rollback and the local verification evidence.

Native bots passed matching hashes/agreed official results at a scripted
420-tick cutoff over public Cloudflare HTTPS/WebSocket. This is transport and
determinism evidence, not full-course or interactive cross-play acceptance.
Existing web health/revision/341-file manifest were checked, and the renewal
deploy hook successfully validated/reloaded Nginx (no certificate renewal was
attempted). UDP publishing/firewall/candidate/DataChannel acceptance stays open
until Task 9 implements that transport; no unused UDP range was opened.


## WebRTC generation ports (plan 34 Task 9)

The dedicated key now forces `/usr/local/sbin/road-rash-net-deploy`, installed
from `deploy/road-rash-net/deploy.py`. It still targets only `road-rash-net`.
The helper chooses one free UDP port per generation in 40000–40099, configures
the server's matching local bounds/public IPv4 and publishes that port one-to-one.
Old/new generations coexist on different ports while ordinary startup health
checks stay enabled. Failure restores the previous options/config; identical
archive replay forces a rebuild if necessary. Existing TCP loopback/TLS routes
are unchanged. UFW adds an IPv4 UDP input rule to 5.161.142.20:40000–40099. The existing
Docker forwarding policy also needs a `DOCKER-USER` exception on eth0 matching
UDP original destination 5.161.142.20:40000–40099 before its public-interface
drop. It is persisted in `/etc/ufw/after.rules`, syntax-checked with
`iptables-restore --test`, and inserted into the live chain without a global
reload. Other published ports keep their existing policy. See deploy README
for the exact rule/removal; backup `after.rules.pre-rr-net-20261027-86791`.
The authorized_keys backup before wrapper selection is
`/root/.ssh/authorized_keys.pre-rtc-20261027-86791`. No private key was recreated.

Clients remain WS by default after mixed loss measurements; use native `--rtc`,
browser `?rtc=1`, or bot `--require-rtc` to opt in. `--no-udp` (browser
`?rtc=1&udp=off`) forces TCP. See VERIFICATION.md for loss metrics and the RTC
handoff for live acceptance.


Public RTC/fallback acceptance passed in production CI 40575802077: every
client exchanges DataChannel traffic, UDP-disabled clients agree over WS,
and mid-race close resumes WS with agreed results. Identical archive replay
rotated UDP 40001->40000 through startup checks; 62 public health samples had
zero failures. The helper handles Dokku's explicit exit 1 no-change outcome,
then rebuilds; other failures restore prior config. See VERIFICATION.md and
the RTC handoff for receipts, source revisions and the final deployment.


Final source `3eb22dcaeb18c9c44e4a4dbb41b066283cfc2ff8` also passed
[CI 40576936619](https://github.com/arkits/road-rash/actions/runs/40576936619)
with the Welcome/choice ordering fixes: all three strict cases agreed on
results, with positive traffic on every required-RTC peer and zero live late
inputs/stalls. Health reports three finished races, zero desyncs, malformed
messages or rate limits; UDP 40001; startup checks neither disabled nor skipped.
