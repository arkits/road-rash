#!/bin/sh
# Plan 33a: packages for the Linux native gate on Debian trixie (GCC 14,
# Clang 19, and the dev packages the pinned SDL needs for its X11, Wayland,
# PipeWire, PulseAudio, ALSA, udev and D-Bus backends). Used by
# linux/gate.Dockerfile and .github/workflows/linux.yml.
set -eu
apt-get update -qq
DEBIAN_FRONTEND=noninteractive apt-get install -y -qq --no-install-recommends \
    g++-14 gcc-14 clang-19 lld-19 llvm-19 libclang-rt-19-dev cmake ninja-build make git python3 ca-certificates \
    file binutils libssl-dev pkg-config \
    libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxss-dev libxtst-dev \
    libxfixes-dev libxkbcommon-dev libwayland-dev wayland-protocols libdecor-0-dev \
    libegl-dev libgl-dev libgles-dev libdrm-dev libgbm-dev \
    libpulse-dev libpipewire-0.3-dev libasound2-dev libudev-dev libdbus-1-dev libibus-1.0-dev
# The presets name gcc/g++ and clang/clang++.
ln -sf /usr/bin/clang-19 /usr/local/bin/clang
ln -sf /usr/bin/clang++-19 /usr/local/bin/clang++
ln -sf /usr/bin/gcc-14 /usr/local/bin/gcc
ln -sf /usr/bin/g++-14 /usr/local/bin/g++
ln -sf /usr/bin/llvm-symbolizer-19 /usr/local/bin/llvm-symbolizer
rm -rf /var/lib/apt/lists/*
