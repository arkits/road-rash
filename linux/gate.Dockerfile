# Plan 33a Task 1: Linux native gate image (Debian trixie; packages in
# linux/install-gate-deps.sh, shared with .github/workflows/linux.yml).
# Build for either architecture:
#   docker build --platform linux/arm64 -t rr-linux-gate:arm64 -f linux/gate.Dockerfile linux
#   docker build --platform linux/amd64 -t rr-linux-gate:amd64 -f linux/gate.Dockerfile linux
# Then mount the checkout and the disc (../cdrom beside it) and run
# tools/linux_gate.sh (see docs/TOOLING.md, "Linux gate in Docker").
FROM debian:trixie
COPY install-gate-deps.sh /tmp/install-gate-deps.sh
RUN sh /tmp/install-gate-deps.sh && rm /tmp/install-gate-deps.sh
