SHELL := /bin/bash

XPLANE_ROOT := /Users/robertw/X-Plane 12
PLUGIN_DIR  := $(XPLANE_ROOT)/Resources/available plugins/xp_pilot

SDK_SENTINEL    := sdk/XPLM/XPLMPlugin.h
IMGUI_SENTINEL  := vendor/imgui/imgui.h
JSON_SENTINEL   := vendor/json.hpp
CATCH2_SENTINEL := vendor/catch2/catch_amalgamated.hpp

CATCH2_VERSION := 3.7.1

.PHONY: help all setup build test install format lint build-windows release release-build cleanup-tags cleanup-runs clean distclean

.DEFAULT_GOAL := help

# ── Help ──────────────────────────────────────────────────────────────────────
help:
	@echo "xp_pilot — X-Plane 12 plugin (macOS)"
	@echo ""
	@echo "Usage: make <target>"
	@echo ""
	@echo "Common:"
	@echo "  help            Show this message (default)"
	@echo "  setup           Download SDK, Dear ImGui, nlohmann/json, Catch2 into sdk/ + vendor/"
	@echo "  build           Configure + compile → build/xp_pilot.xpl"
	@echo "  test            Build and run the Catch2 unit tests"
	@echo "  install         Code-sign and copy the plugin into X-Plane (mac_x64)"
	@echo "  clean           Remove build/ and build-lint/"
	@echo "  distclean       clean + remove sdk/ and vendor/ (everything 'make setup' installed)"
	@echo ""
	@echo "Code quality:"
	@echo "  format          Run clang-format on src/*.{cpp,hpp}"
	@echo "  lint            Run clang-tidy (uses build-lint/ for compile_commands.json)"
	@echo ""
	@echo "CI / cross-platform:"
	@echo "  build-windows   Windows x64 build (used by CI)"
	@echo ""
	@echo "Release:"
	@echo "  release VERSION=x.y.z   Tag + push release (commits VERSION.txt)"
	@echo "  release-build           Local release build (-DRELEASE=ON)"
	@echo "  cleanup-tags            Prune local tags removed on origin"
	@echo "  cleanup-runs            Delete all GitHub Actions runs except the newest per workflow"

all: format build lint test

# ── Setup ─────────────────────────────────────────────────────────────────────
setup: $(SDK_SENTINEL) $(IMGUI_SENTINEL) $(JSON_SENTINEL) $(CATCH2_SENTINEL)
	@echo "Setup complete. Run 'make build' to compile."

$(SDK_SENTINEL):
	@echo "Downloading X-Plane SDK..."
	@set -euo pipefail; \
	TMP=$$(mktemp -d); \
	trap "rm -rf $$TMP" EXIT; \
	curl -fsSL "https://developer.x-plane.com/wp-content/plugins/code-sample-generation/sdk_zip_files/XPSDK430.zip" \
	     -o "$$TMP/sdk.zip"; \
	unzip -q "$$TMP/sdk.zip" -d "$$TMP/sdk_extracted"; \
	mkdir -p sdk/XPLM sdk/XPWidgets sdk/Libraries/Win sdk/Libraries/Mac; \
	find "$$TMP/sdk_extracted" -path "*/CHeaders/XPLM/*.h"   -exec cp {} sdk/XPLM/ \;; \
	find "$$TMP/sdk_extracted" -path "*/CHeaders/Widgets/*.h" -exec cp {} sdk/XPWidgets/ \;; \
	find "$$TMP/sdk_extracted" -path "*/Libraries/Win/*.lib"  -exec cp {} sdk/Libraries/Win/ \;; \
	cp -R "$$TMP/sdk_extracted"/*/Libraries/Mac/*.framework sdk/Libraries/Mac/ 2>/dev/null || \
	find "$$TMP/sdk_extracted" -name "*.framework" -exec cp -R {} sdk/Libraries/Mac/ \;
	@echo "SDK headers installed."

$(IMGUI_SENTINEL):
	@echo "Downloading Dear ImGui v1.91.9..."
	@set -euo pipefail; \
	TMP=$$(mktemp -d); \
	trap "rm -rf $$TMP" EXIT; \
	mkdir -p vendor/imgui/backends; \
	curl -fsSL "https://github.com/ocornut/imgui/archive/refs/tags/v1.91.9.zip" -o "$$TMP/imgui.zip"; \
	unzip -q "$$TMP/imgui.zip" -d "$$TMP/"; \
	SRC="$$TMP/imgui-1.91.9"; \
	cp "$$SRC"/imgui.{h,cpp} vendor/imgui/; \
	cp "$$SRC"/imgui_{draw,tables,widgets}.cpp vendor/imgui/; \
	cp "$$SRC"/imgui_internal.h "$$SRC"/imconfig.h vendor/imgui/; \
	cp "$$SRC"/imstb_textedit.h "$$SRC"/imstb_rectpack.h "$$SRC"/imstb_truetype.h vendor/imgui/ 2>/dev/null || true; \
	cp "$$SRC"/backends/imgui_impl_opengl2.{h,cpp} vendor/imgui/backends/
	@echo "Dear ImGui installed."

$(JSON_SENTINEL):
	@echo "Downloading nlohmann/json v3.11.3..."
	@mkdir -p vendor
	@curl -fsSL "https://github.com/nlohmann/json/releases/download/v3.11.3/json.hpp" \
	     -o vendor/json.hpp
	@echo "nlohmann/json installed."

$(CATCH2_SENTINEL):
	@echo "Downloading Catch2 v$(CATCH2_VERSION) (amalgamated)..."
	@set -euo pipefail; \
	TMP=$$(mktemp -d); \
	trap "rm -rf $$TMP" EXIT; \
	mkdir -p vendor/catch2; \
	curl -fsSL "https://github.com/catchorg/Catch2/archive/refs/tags/v$(CATCH2_VERSION).tar.gz" \
	     -o "$$TMP/catch2.tar.gz"; \
	tar -xzf "$$TMP/catch2.tar.gz" -C "$$TMP/"; \
	cp "$$TMP/Catch2-$(CATCH2_VERSION)/extras/catch_amalgamated.hpp" vendor/catch2/; \
	cp "$$TMP/Catch2-$(CATCH2_VERSION)/extras/catch_amalgamated.cpp" vendor/catch2/
	@echo "Catch2 installed."

# ── Build ─────────────────────────────────────────────────────────────────────
build: $(SDK_SENTINEL) $(IMGUI_SENTINEL) $(JSON_SENTINEL) $(CATCH2_SENTINEL)
	@echo "=== Building xp_pilot ==="
	cmake -B build -DCMAKE_BUILD_TYPE=Release -Wno-dev
	cmake --build build --parallel
	@echo ""
	@file build/xp_pilot.xpl
	@echo "Done. Run 'make install' to deploy."

# ── Test ──────────────────────────────────────────────────────────────────────
test: build
	@echo "=== Running xp_pilot tests ==="
	@./build/xp_pilot_tests

# ── Install ───────────────────────────────────────────────────────────────────
install:
	@if [ ! -f "build/xp_pilot.xpl" ]; then \
	    echo "Plugin not built yet. Run 'make build' first."; exit 1; \
	fi
	@echo "=== Installing xp_pilot ==="
	@mkdir -p "$(PLUGIN_DIR)/mac_x64"
	@cp build/xp_pilot.xpl "$(PLUGIN_DIR)/mac_x64/"
	@xattr -dr com.apple.quarantine "$(PLUGIN_DIR)/mac_x64/xp_pilot.xpl" 2>/dev/null || true
	@codesign --force --deep --sign - "$(PLUGIN_DIR)/mac_x64/xp_pilot.xpl"
	@echo "Signed:    $(PLUGIN_DIR)/mac_x64/xp_pilot.xpl"
	@echo "Installed: $(PLUGIN_DIR)/mac_x64/xp_pilot.xpl"
	@if [ ! -f "$(PLUGIN_DIR)/data/flight_logger_profiles.json" ]; then \
	    mkdir -p "$(PLUGIN_DIR)/data"; \
	    cp data/flight_logger_profiles.json "$(PLUGIN_DIR)/data/"; \
	    echo "Installed: $(PLUGIN_DIR)/data/flight_logger_profiles.json"; \
	else \
	    echo "Profiles JSON already exists – not overwritten."; \
	fi
	@set -euo pipefail; \
	OLD_DATA="$(XPLANE_ROOT)/Resources/available plugins/FlyWithLua/Scripts/flight_logger_data"; \
	MARKER="$(PLUGIN_DIR)/data/.migrated_from_flywothlua"; \
	if [ -d "$$OLD_DATA/flights" ] && [ ! -f "$$MARKER" ]; then \
	    echo "Migrating FlyWithLua flight logs..."; \
	    mkdir -p "$(PLUGIN_DIR)/data/flights" "$(PLUGIN_DIR)/data/reports"; \
	    count=0; \
	    for f in "$$OLD_DATA/flights/"*.json; do \
	        [ -f "$$f" ] || continue; \
	        fname=$$(basename "$$f"); \
	        if [ ! -f "$(PLUGIN_DIR)/data/flights/$$fname" ]; then \
	            cp "$$f" "$(PLUGIN_DIR)/data/flights/"; \
	            count=$$((count + 1)); \
	        fi; \
	    done; \
	    touch "$$MARKER"; \
	    echo "Migrated $$count flight JSON files."; \
	fi
	@echo ""
	@echo "Plugin installed. Activate via XLauncher, then restart X-Plane."

# ── Lint ──────────────────────────────────────────────────────────────────────
format:
	@command -v clang-format >/dev/null 2>&1 || { \
	    echo "clang-format not found. Install with: brew install llvm"; \
	    echo "Then add to PATH: export PATH=\"\$$(brew --prefix llvm)/bin:\$$PATH\""; \
	    exit 1; }
	clang-format -i src/*.cpp src/*.hpp

lint: $(SDK_SENTINEL) $(IMGUI_SENTINEL) $(JSON_SENTINEL) $(CATCH2_SENTINEL)
	@command -v clang-tidy >/dev/null 2>&1 || { \
	    echo "clang-tidy not found. Install with: brew install llvm"; \
	    echo "Then add to PATH: export PATH=\"\$$(brew --prefix llvm)/bin:\$$PATH\""; \
	    exit 1; }
	cmake -B build-lint -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DCMAKE_OSX_ARCHITECTURES=arm64 -Wno-dev
	clang-tidy -p build-lint --extra-arg="-isysroot" --extra-arg="$(shell xcrun --show-sdk-path)" src/*.cpp

# ── Build (Windows CI) ────────────────────────────────────────────────────────
build-windows: $(SDK_SENTINEL) $(IMGUI_SENTINEL) $(JSON_SENTINEL) $(CATCH2_SENTINEL)
	cmake -B build -A x64
	cmake --build build --config Release

# ── Release ───────────────────────────────────────────────────────────────────
release:
	@if [ -z "$(VERSION)" ]; then \
	    echo "Usage: make release VERSION=1.2.1"; exit 1; \
	fi
	@if ! git diff --quiet || ! git diff --cached --quiet; then \
	    echo "Uncommitted changes present. Commit or stash first."; exit 1; \
	fi
	@if [ -n "$$(git ls-files --others --exclude-standard)" ]; then \
	    echo "Untracked files present. Commit or clean up first."; exit 1; \
	fi
	@echo "$(VERSION)" > VERSION.txt
	@git add VERSION.txt
	@git commit -m "release $(VERSION)"
	@git push origin main
	@git tag -a "v$(VERSION)" -m "Release $(VERSION)"
	@git push origin "v$(VERSION)"
	@echo "Released v$(VERSION) and pushed tag to origin."

release-build: $(SDK_SENTINEL) $(IMGUI_SENTINEL) $(JSON_SENTINEL) $(CATCH2_SENTINEL)
	@echo "=== Building xp_pilot (release) ==="
	cmake -B build -DCMAKE_BUILD_TYPE=Release -DRELEASE=ON -Wno-dev
	cmake --build build --parallel
	@echo ""
	@file build/xp_pilot.xpl
	@echo "Done. Release build with version from VERSION.txt."

# ── Cleanup Tags ──────────────────────────────────────────────────────────────
cleanup-tags:
	git fetch --prune --prune-tags origin
	@echo "Local tags synced with remote."

# ── Cleanup GitHub Actions runs ───────────────────────────────────────────────
cleanup-runs:
	@command -v gh >/dev/null 2>&1 || { \
	    echo "gh not found. Install with: brew install gh"; exit 1; }
	@echo "Deleting GitHub Actions runs (keeping newest per workflow)..."
	@for wf in $$(gh workflow list --json id -q '.[].id'); do \
	    gh run list --workflow=$$wf --limit 1000 --json databaseId -q '.[1:] | .[].databaseId' \
	        | xargs -I {} gh run delete {}; \
	done
	@echo "Cleanup complete."

# ── Clean ─────────────────────────────────────────────────────────────────────
clean:
	rm -rf build build-lint

# ── Distclean ─────────────────────────────────────────────────────────────────
# Remove everything 'make setup' downloaded so a full re-bootstrap is forced.
distclean: clean
	rm -rf sdk/ vendor/
	@echo "Removed sdk/ and vendor/. Run 'make setup' to re-download dependencies."
