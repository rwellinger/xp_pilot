#!/usr/bin/env bash
# build.sh — Compile xp_pilot plugin (Universal Binary: arm64 + x86_64).
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

green() { printf '\033[32m%s\033[0m\n' "$*"; }
red()   { printf '\033[31m%s\033[0m\n' "$*"; }
bold()  { printf '\033[1m%s\033[0m\n'  "$*"; }

# Sanity checks
if [ ! -f "sdk/XPLM/XPLMPlugin.h" ]; then
    red "SDK headers missing. Run ./setup.sh first."
    exit 1
fi
if [ ! -f "vendor/imgui/imgui.h" ]; then
    red "Dear ImGui missing. Run ./setup.sh first."
    exit 1
fi

bold "=== Building xp_pilot ==="
cmake -B build -DCMAKE_BUILD_TYPE=Release -Wno-dev
cmake --build build --parallel
echo ""
bold "Build output:"
file build/xp_pilot.xpl
green "Done. Run ./install.sh to deploy."
