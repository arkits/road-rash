#!/usr/bin/env python3
"""Plan 34 Task 3 Step 5 / plan 33 Task A9: cross-platform determinism matrix.

Runs rr-determinism (a fixed 8-human input log; see tools/determinism.cpp) on
every platform available here and compares the per-60-tick state hashes with
the committed synthetic reference tests/data/determinism_synthetic.txt (and,
with the disc, with this host's disc run). Exit 1 on any difference.

  tools/determinism_matrix.py [target ...]     (default: every target)

Targets: macos, linux (x86-64, Docker), linux-arm64 (Docker), wasm (emcc +
Node), android (adb device + build-android-det/), windows and windows-arm64
(native on a Windows host). Unavailable targets are skipped with the reason.
"""
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
OUT = Path(os.environ.get("RR_DET_OUT", ROOT / "analysis" / "determinism"))
REF = ROOT / "tests" / "data" / "determinism_synthetic.txt"
DISC = Path(os.environ.get("RR_DISC", ROOT.parent / "cdrom" / "ROADRASH"))
TARGETS = ["macos", "linux", "linux-arm64", "wasm", "android", "windows", "windows-arm64"]
failures = 0


def run(cmd, log, **kw):
    with open(log, "a", encoding="utf-8") as f:
        return subprocess.run(cmd, stdout=f, stderr=subprocess.STDOUT, check=True, **kw)


def capture(cmd, out_file):
    with open(out_file, "w", encoding="utf-8") as f:
        subprocess.run(cmd, stdout=f, check=True)


def check(name, got, reference):
    global failures
    a = Path(got).read_text().splitlines()
    b = Path(reference).read_text().splitlines()
    if a == b:
        print(f"determinism: {name} matches {Path(reference).name}")
        return
    first = next((i for i, (x, y) in enumerate(zip(a, b)) if x != y), min(len(a), len(b)))
    print(f"determinism: {name} DIFFERS from {Path(reference).name} at line {first + 1}", file=sys.stderr)
    failures += 1


def host_native(name, build_dir, extra=()):
    """Configure, build and run rr-determinism with the host toolchain."""
    log = OUT / f"{name}-build.log"
    log.write_text("")
    run(["cmake", "-S", str(ROOT), "-B", str(build_dir), "-DCMAKE_BUILD_TYPE=Release", "-DRR_NET_TRANSPORT=OFF",
         *extra], log)
    run(["cmake", "--build", str(build_dir), "--target", "rr-determinism", "--config", "Release", "--parallel"], log)
    exe = next((p for p in [build_dir / "rr-determinism", build_dir / "Release" / "rr-determinism.exe",
                            build_dir / "rr-determinism.exe"] if p.exists()), None)
    if exe is None:
        raise RuntimeError(f"rr-determinism not found under {build_dir}")
    capture([str(exe), "--synthetic"], OUT / f"{name}-synthetic.txt")
    check(f"{name}-synthetic", OUT / f"{name}-synthetic.txt", REF)
    if (DISC / "ROADRASH.EXE").is_file():
        capture([str(exe), "--disc", str(DISC)], OUT / f"{name}-disc.txt")
        host_disc = OUT / "macos-disc.txt"
        if name != "macos" and host_disc.is_file():
            check(f"{name}-disc", OUT / f"{name}-disc.txt", host_disc)


def docker_linux(name, docker_platform):
    if not shutil.which("docker") or subprocess.run(["docker", "info"], capture_output=True).returncode:
        print(f"determinism: {name} skipped (no Docker daemon)")
        return
    mounts = ["-v", f"{ROOT}:/src:ro", "-v", f"rr-det-{name}-build:/build", "-v", f"{OUT}:/out"]
    disc = os.environ.get("RR_DISC_MOUNT") and DISC.is_dir()
    if disc:
        mounts += ["-v", f"{DISC}:/disc:ro"]
    script = (
        "apt-get update -qq >/dev/null && apt-get install -y -qq g++ cmake make python3 ca-certificates >/dev/null\n"
        f"cmake -S /src -B /build -DCMAKE_BUILD_TYPE=Release -DRR_NET_TRANSPORT=OFF -DRR_SDL_HEADLESS=ON >/out/{name}-build.log 2>&1\n"
        f"cmake --build /build --target rr-determinism --parallel >>/out/{name}-build.log 2>&1\n"
        f"/build/rr-determinism --synthetic >/out/{name}-synthetic.txt\n"
        f"if [ -f /disc/ROADRASH.EXE ]; then /build/rr-determinism --disc /disc >/out/{name}-disc.txt; fi")
    subprocess.run(["docker", "run", "--rm", "--platform", docker_platform, *mounts, "debian:trixie", "sh", "-ec",
                    script], check=True)
    check(f"{name}-synthetic", OUT / f"{name}-synthetic.txt", REF)
    if (OUT / f"{name}-disc.txt").is_file() and (OUT / "macos-disc.txt").is_file():
        check(f"{name}-disc", OUT / f"{name}-disc.txt", OUT / "macos-disc.txt")


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    wanted = sys.argv[1:] or TARGETS
    system, machine = platform.system(), platform.machine().lower()
    for target in wanted:
        if target not in TARGETS:
            print(f"determinism: unknown platform {target}", file=sys.stderr)
            return 1
        if target == "macos":
            if system != "Darwin":
                print("determinism: macos skipped (not on macOS)")
                continue
            host_native("macos", ROOT / "build-det-macos")
        elif target == "linux":
            docker_linux("linux", "linux/amd64")
        elif target == "linux-arm64":
            docker_linux("linux-arm64", "linux/arm64")
        elif target == "wasm":
            if not shutil.which("emcc"):
                print("determinism: wasm skipped (no emcc; source emsdk_env.sh)")
                continue
            build = ROOT / "build-det-wasm"
            log = OUT / "wasm-build.log"
            log.write_text("")
            run(["emcmake", "cmake", "-S", str(ROOT), "-B", str(build), "-DCMAKE_BUILD_TYPE=Release"], log)
            run(["cmake", "--build", str(build), "--target", "rr-determinism", "--parallel"], log)
            capture(["node", str(build / "rr-determinism.js"), "--synthetic"], OUT / "wasm-synthetic.txt")
            check("wasm32-synthetic", OUT / "wasm-synthetic.txt", REF)
        elif target == "android":
            adb = Path(os.environ.get("ADB", ROOT / ".tools/android-sdk/platform-tools/adb"))
            if not adb.exists() or subprocess.run([str(adb), "get-state"], capture_output=True).returncode:
                print("determinism: android skipped (no device)")
                continue
            binary = ROOT / "build-android-det" / "rr-determinism"
            if not binary.exists():
                print(f"determinism: android skipped (build {binary} for arm64-v8a first)")
                continue
            subprocess.run([str(adb), "push", str(binary), "/data/local/tmp/rr-determinism"], check=True,
                           capture_output=True)
            capture([str(adb), "shell", "/data/local/tmp/rr-determinism", "--synthetic"],
                    OUT / "android-synthetic.txt")
            check("android-arm64-synthetic", OUT / "android-synthetic.txt", REF)
        else:  # windows, windows-arm64
            want_arm = target == "windows-arm64"
            on_arm = machine in ("arm64", "aarch64")
            if system != "Windows" or want_arm != on_arm:
                print(f"determinism: {target} skipped (needs a {'ARM64' if want_arm else 'x64'} Windows host)")
                continue
            host_native(target, ROOT / f"build-det-{target}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
