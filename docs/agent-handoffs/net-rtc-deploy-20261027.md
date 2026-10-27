# net-rtc-deploy-20261027

Task: Task9 scoped rolling UDP port allocator and deploy rollback tests

Baseline: 2cb308190125d0ef050f982d0ff8ab91802106a0

Worktree: `/Users/archit/Dev/rr/.plan34-impl-20261026`
Build directory: `/Users/archit/Dev/rr/.plan34-impl-20261026/build-agent-net-rtc-deploy-20261027`
Owner PID: 86791
Resource token: `ed4c7436-95e1-4e0d-a2f0-6a565e79356a`

Owned files:

- `/Users/archit/Dev/rr/.plan34-impl-20261026/deploy/road-rash-net/deploy.py`
- `/Users/archit/Dev/rr/.plan34-impl-20261026/tests/net_deploy_test.py`

## Progress / evidence

Implemented Task 9 native/browser peers and negotiated connection wrapper.
Signals go over WS after Welcome, queued callbacks never touch simulation.
Input/Ack/Frames switch to unordered zero-retransmit DataChannel; all control,
catch-up and results keep WS. Explicit additive type29 RtcClose carries
permanent fallback over WS; IDs1–28 unchanged. Timeout5s; actual failed/closed
peer never closes WS. Same wrapper survives lobby/race/rematch handovers.
Server UDP/publicIPv4 config validated; all peers multiplex on one UDP port.
RTC remains opt-in (--rtc, browser ?rtc=1); force --no-udp / udp=off.

Gates: Debug119/119 (435.44s), Release119/119 (39.68s); ASan/UBSan focused4/4;
Python deploy tests7/7; browser game/fixtures compiled; real Chromium browser-
browser and browser-native tests passed via tests/net_rtc_browser.cjs. Native
2/8client required-RTC and blocked/forced-close agreed480-tick results passed;
strict per-client counters prove all8 use one UDP port. Current Release final
blocked/closed cases pass0 live stalls, matched hashes, close fallback count2.
Native game/bots rebuilt after final source fingerprint change. No physics or
original-oracle fidelity claim added. Actionlint and clang-format22 pass.

Sanitizer caveat: pinned libjuice picohash's C callback function casts trigger
function-type UBSan. Only juice-static compiles -fno-sanitize=function; ASan and
other UBSan, including first-party code, stay enabled. Browser formatter split
strict JS equality in EM_ASM; fixed using format-protected EM_JS, rebuilt and
retested. Codec unknown-type test updated for new29; only appended golden1d.

Actual Linux netem server-egress20ms/5%loss, two fixed-seed clients,1800ticks:
WS reps p50/p95/p99=0/0/6, stalls46/47, late9/15; RTC=0/0/7 and0/0/6,
stalls1/0, late2/2; bytes329740/329162 vs922150/916015. Initial measurement
includes2–4 sealed ticks beyond cutoff in denominator; final metric caps at
cutoff. All runs agreed within-group; not cross-run goldens. Tradeoff does not
justify RTC default; WS remains default. Full table in VERIFICATION.md.

Deployment helper deploy.py fixed to only road-rash-net assigns distinct
one-to-one UDP port per generation in40000–40099, updates future container
options/env, retains ordinary startup checks and old live mapping, restores
old config/options on failure. Identical archive skip forces ps:rebuild.
Archive compressed/expanded bounded, exact3 code files+directory, no links,
ELF64x86-64 and startup health required. Root-private flock serializes it.
Seven tests cover rollback/first-failure/replay/changed-generation/archive.
Installed /usr/local/sbin/road-rash-net-deploy and changed only dedicated key's
forced command (backup authorized_keys.pre-rtc-20261027-86791). Old source2cb
service stays WS-only until CI; new scoped IPv4 UFW UDP rule to5.161.142.20
40000:40099 added after verifying whole range unused. No other app/global
proxy/firewall rule changed; TCP/TLS routes intact. No private key regenerated.

## Remaining / live acceptance

Commit/push code triggers net workflow: required RTC, no UDP, then forced
close tick390. Verify public source revision, per-client RTC counters and zero
desyncs, run native outside-network check, verify same-source archive replay
and next generation port/health, record receipt; finish Task8Step3. No live
UDP acceptance claimed before that receipt. Browser workflow now builds/runs
RTC fixtures. Device/full interactive cross-play remains Task10.

Evidence /tmp/rr-rtc-* and outside-Git .plan34-logs/rtc-20261027; no commercial
assets in images or Git. Local Docker benchmarks/servers all cleaned up.
Current build dirs build-p34, build-p34-rel (now transportON), build-p34-fuzz
(transportON, Clang22), build-p34-web. Libdatachannel read-only shared source
from build-p34/_deps. Compiler/tests used; no live C++ LSP exposed.

## Live processes/resources

Owner86791; own main/wire/deploy/browser claims held until completion.
Production service intentionally running. Root SSH usable after owner unlock.
No local server/container left. Main checkout frontend claim untouched; retain
pre-existing analysis symlinks. Live CI receipt appended below when verified.
