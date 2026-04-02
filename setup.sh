#!/usr/bin/env bash
# setup.sh — Download X-Plane SDK headers, Dear ImGui, and nlohmann/json.
# Run once before first build.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

green() { printf '\033[32m%s\033[0m\n' "$*"; }
red()   { printf '\033[31m%s\033[0m\n' "$*"; }
bold()  { printf '\033[1m%s\033[0m\n'  "$*"; }

bold "=== xp_pilot setup ==="

# ── 1. X-Plane SDK headers ─────────────────────────────────────────────────────
# The SDK zip contains CHeaders/XPLM/*.h and CHeaders/Widgets/*.h
SDK_DIR="$SCRIPT_DIR/sdk"
if [ -f "$SDK_DIR/XPLM/XPLMPlugin.h" ]; then
    green "SDK headers already present – skipping download."
else
    bold "Downloading X-Plane SDK headers..."
    TMP=$(mktemp -d)
    # SDK 4.0 (X-Plane 12 compatible)
    curl -fsSL "https://developer.x-plane.com/wp-content/plugins/code-sample-generation/sdk_zip_files/XPSDK430.zip" \
         -o "$TMP/sdk.zip" \
      || { red "SDK download failed."; red "Download manually from https://developer.x-plane.com/sdk/plugin-sdk-downloads/"; red "and extract CHeaders/XPLM → sdk/XPLM and CHeaders/Widgets → sdk/XPWidgets"; exit 1; }
    unzip -q "$TMP/sdk.zip" -d "$TMP/sdk_extracted"
    mkdir -p "$SDK_DIR/XPLM" "$SDK_DIR/XPWidgets" "$SDK_DIR/Libraries/Win"
    find "$TMP/sdk_extracted" -path "*/CHeaders/XPLM/*.h" -exec cp {} "$SDK_DIR/XPLM/" \;
    find "$TMP/sdk_extracted" -path "*/CHeaders/Widgets/*.h" -exec cp {} "$SDK_DIR/XPWidgets/" \;
    find "$TMP/sdk_extracted" -path "*/Libraries/Win/*.lib" -exec cp {} "$SDK_DIR/Libraries/Win/" \;
    rm -rf "$TMP"
    green "SDK headers installed."
fi

# ── 2. Dear ImGui ──────────────────────────────────────────────────────────────
IMGUI_DIR="$SCRIPT_DIR/vendor/imgui"
if [ -f "$IMGUI_DIR/imgui.h" ]; then
    green "Dear ImGui already present – skipping download."
else
    bold "Downloading Dear ImGui v1.91.9..."
    mkdir -p "$IMGUI_DIR/backends"
    TMP=$(mktemp -d)
    curl -fsSL "https://github.com/ocornut/imgui/archive/refs/tags/v1.91.9.zip" \
         -o "$TMP/imgui.zip"
    unzip -q "$TMP/imgui.zip" -d "$TMP/"
    SRC="$TMP/imgui-1.91.9"
    cp "$SRC"/imgui.{h,cpp} "$IMGUI_DIR/"
    cp "$SRC"/imgui_{draw,tables,widgets,internal}.{h,cpp} "$IMGUI_DIR/" 2>/dev/null || true
    cp "$SRC"/imgui_internal.h "$IMGUI_DIR/" 2>/dev/null || true
    cp "$SRC"/imconfig.h "$IMGUI_DIR/"
    cp "$SRC"/imstb_textedit.h "$SRC"/imstb_rectpack.h "$SRC"/imstb_truetype.h "$IMGUI_DIR/" 2>/dev/null || true
    cp "$SRC"/backends/imgui_impl_opengl2.{h,cpp} "$IMGUI_DIR/backends/"
    rm -rf "$TMP"
    green "Dear ImGui installed."
fi

# ── 3. nlohmann/json ───────────────────────────────────────────────────────────
JSON_FILE="$SCRIPT_DIR/vendor/json.hpp"
if [ -f "$JSON_FILE" ]; then
    green "nlohmann/json already present – skipping download."
else
    bold "Downloading nlohmann/json..."
    curl -fsSL \
        "https://github.com/nlohmann/json/releases/download/v3.11.3/json.hpp" \
        -o "$JSON_FILE"
    green "nlohmann/json installed."
fi

green ""
green "Setup complete. Run ./build.sh to compile."
