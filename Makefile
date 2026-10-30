# Convenience wrapper around the CMake / Gradle / script workflows in README.md.
# Run `make help` for targets. Override variables on the command line, e.g.
#   make run COURSE=2 BIKE=5      make build BUILD_TYPE=Release

BUILD_DIR  ?= build
BUILD_TYPE ?= Debug
ASSETS     ?= ../cdrom/ROADRASH
COURSE     ?=
BIKE       ?=
JOBS       ?= $(shell nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)
# Homebrew JDK when present; other hosts keep their own JAVA_HOME (plan 33 A9).
JAVA_HOME  ?= $(wildcard /opt/homebrew/opt/openjdk@21/libexec/openjdk.jdk/Contents/Home)

.PHONY: help all configure build run run-first viewer video test test-fast test-sim corpus smoke \
        import app apk apk-release decompile inventory ci clean distclean \
        doctor gate-native gate-sanitizers verify-sim gate-android-debug \
        gate-android-release gate-package agent-status analysis-index agent-sessions lsp-check agent-tools-test \
        format format-check lint hooks

help:
	@echo "Targets:"
	@echo "  build        configure and build everything ($(BUILD_TYPE)) into $(BUILD_DIR)/"
	@echo "  run          build and launch rr-game (optional COURSE=0-4 BIKE=0-14; ASSETS=$(ASSETS))"
	@echo "  run-first    launch rr-game without --assets (first-run import flow)"
	@echo "  viewer       build and launch the asset research viewer"
	@echo "  video        play a cutscene: make video VIDEO=VIDEO/INTRO.AVI"
	@echo "  test         run ctest"
	@echo "  test-fast    ctest without slow tests, parallel"
	@echo "  test-sim     ctest simulation tests, parallel"
	@echo "  smoke        viewer smoke check and corpus check"
	@echo "  corpus       python3 tools/check_corpus.py"
	@echo "  import       import an ISO: make import ISO=path/to.iso DEST=path/ROADRASH"
	@echo "  app          standalone macOS app bundle (tools/build_macos_app.sh)"
	@echo "  apk          Android debug APK"
	@echo "  apk-release  Android release APK (debug signing)"
	@echo "  inventory    regenerate analysis/inventory.json"
	@echo "  decompile    run Ghidra headless export (overwrites analysis import)"
	@echo "  ci           tools/ci.sh local verification"
	@echo "  clean        remove $(BUILD_DIR)/"
	@echo "  distclean    also remove build-release/ and android build output"
	@echo "  doctor       read-only capability, instruction and resource report"
	@echo "  gate-native|gate-sanitizers|verify-sim  logged checks (override BUILD_DIR)"
	@echo "  gate-android-debug|gate-android-release|gate-package  logged artifact checks"
	@echo "  agent-status regenerate docs/CURRENT_STATUS.md from its evidence manifest"
	@echo "  analysis-index build the address/evidence lookup cache"
	@echo "  agent-sessions inventory retained local histories into analysis/agent-sessions.json"
	@echo "  lsp-check    read-only compile database freshness check"
	@echo "  agent-tools-test synthetic tooling regressions (no native build)"
	@echo "  format       clang-format 22 (FILES='src/foo.cpp src/foo.hpp'; default all src/tests/fuzz/oracle C/C++)"
	@echo "  format-check read-only formatting gate (same FILES selection)"
	@echo "  hooks        install the clang-format pre-commit hook into .git/hooks"
	@echo "  lint         clang-tidy 22 (FILES='src/foo.cpp'; configured BUILD_DIR required)"

all: build

format:
	python3 tools/cpp_quality.py format $(FILES)

format-check:
	python3 tools/cpp_quality.py format-check $(FILES)

lint:
	python3 tools/cpp_quality.py lint --build-dir "$(BUILD_DIR)" $(FILES)

hooks:
	sh tools/git-hooks/install.sh

configure:
	cmake -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=$(BUILD_TYPE)

build: configure
	cmake --build $(BUILD_DIR) --parallel $(JOBS)

run: build
	$(BUILD_DIR)/rr-game --assets $(ASSETS) $(if $(COURSE),--course $(COURSE)) $(if $(BIKE),--bike $(BIKE))

run-first: build
	$(BUILD_DIR)/rr-game

viewer: build
	$(BUILD_DIR)/rr-viewer --assets $(ASSETS)

video: build
	$(BUILD_DIR)/rr-game --assets $(ASSETS) --video $(or $(VIDEO),VIDEO/INTRO.AVI)

test: build
	ctest --test-dir $(BUILD_DIR) --output-on-failure

test-fast: build
	ctest --test-dir $(BUILD_DIR) -LE slow -j $(JOBS) --output-on-failure
test-sim: build
	ctest --test-dir $(BUILD_DIR) -L sim -j $(JOBS) --output-on-failure

smoke: build corpus
	$(BUILD_DIR)/rr-viewer --assets $(ASSETS) --smoke

corpus:
	python3 tools/check_corpus.py $(ASSETS)

import: build
	@test -n "$(ISO)" -a -n "$(DEST)" || { echo "usage: make import ISO=<iso> DEST=<dir>"; exit 1; }
	$(BUILD_DIR)/rr-import "$(ISO)" "$(DEST)"

app:
	tools/build_macos_app.sh

apk:
	JAVA_HOME=$(JAVA_HOME) android/gradlew -p android assembleDebug

apk-release:
	JAVA_HOME=$(JAVA_HOME) android/gradlew -p android assembleRelease -PreleaseDebugSigning=true

inventory:
	python3 tools/inventory.py $(ASSETS) --out analysis/inventory.json

decompile:
	tools/decompile.sh

ci:
	tools/ci.sh

doctor:
	python3 tools/agent_doctor.py

gate-native:
	python3 tools/agent_gate.py native --build-dir "$(BUILD_DIR)" --jobs "$(JOBS)"

gate-sanitizers:
	python3 tools/agent_gate.py sanitizers --build-dir "$(BUILD_DIR)-sanitizers" --jobs "$(JOBS)"

verify-sim:
	python3 tools/agent_gate.py oracle --build-dir "$(BUILD_DIR)" --jobs "$(JOBS)"

gate-android-debug:
	python3 tools/agent_gate.py android-debug

gate-android-release:
	python3 tools/agent_gate.py android-release

gate-package:
	python3 tools/agent_gate.py package --build-dir "$(BUILD_DIR)-package"

agent-status:
	python3 tools/agent_status.py

analysis-index:
	python3 tools/analysis_index.py

agent-sessions:
	python3 tools/agent_sessions.py --out analysis/agent-sessions.json

lsp-check:
	python3 tools/agent_lsp.py

agent-tools-test:
	python3 tests/agent_tooling_test.py
	python3 tests/agent_sessions_test.py

clean:
	rm -rf $(BUILD_DIR)

distclean: clean
	rm -rf build-release
	-JAVA_HOME=$(JAVA_HOME) android/gradlew -p android clean
