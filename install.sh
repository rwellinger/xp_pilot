#!/usr/bin/env bash
# install.sh — Install xp_pilot into X-Plane available plugins.
# XLauncher can then symlink it to active plugins.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
XPLANE_ROOT="/Users/robertw/X-Plane 12"
DEST="$XPLANE_ROOT/Resources/available plugins/xp_pilot"
BIN_DEST="$DEST/mac_x64"
DATA_DEST="$DEST/data"

green() { printf '\033[32m%s\033[0m\n' "$*"; }
red()   { printf '\033[31m%s\033[0m\n' "$*"; }
bold()  { printf '\033[1m%s\033[0m\n'  "$*"; }

if [ ! -f "$SCRIPT_DIR/build/xp_pilot.xpl" ]; then
    red "Plugin not built yet. Run ./build.sh first."
    exit 1
fi

bold "=== Installing xp_pilot ==="
mkdir -p "$BIN_DEST"
cp "$SCRIPT_DIR/build/xp_pilot.xpl" "$BIN_DEST/"

# Remove quarantine attribute (set when files come from internet — sometimes
# also set by Finder on local copies; harmless to run even if not set)
xattr -dr com.apple.quarantine "$BIN_DEST/xp_pilot.xpl" 2>/dev/null || true

# Ad-hoc code sign (no certificate needed — required on macOS 12+ for dylibs
# that aren't Apple-notarized; the "-" means ad-hoc, valid for own machine only)
codesign --force --deep --sign - "$BIN_DEST/xp_pilot.xpl"
green "Signed:    $BIN_DEST/xp_pilot.xpl"

green "Installed: $BIN_DEST/xp_pilot.xpl"

# Install data files (profiles JSON) – only if not already present at destination
# (preserve user edits if they already exist)
mkdir -p "$DATA_DEST"
if [ -f "$SCRIPT_DIR/data/flight_logger_profiles.json" ]; then
    if [ ! -f "$DATA_DEST/flight_logger_profiles.json" ]; then
        cp "$SCRIPT_DIR/data/flight_logger_profiles.json" "$DATA_DEST/"
        green "Installed: $DATA_DEST/flight_logger_profiles.json"
    else
        green "Profiles JSON already exists at destination – not overwritten."
    fi
fi

# ── Migrate old FlyWithLua flight logs (one-time, non-destructive) ────────────
OLD_DATA="$XPLANE_ROOT/Resources/available plugins/FlyWithLua/Scripts/flight_logger_data"
NEW_DATA="$DEST/data"
MIGRATE_MARKER="$NEW_DATA/.migrated_from_flywothlua"

if [ -d "$OLD_DATA/flights" ] && [ ! -f "$MIGRATE_MARKER" ]; then
    bold "Migrating FlyWithLua flight logs..."
    mkdir -p "$NEW_DATA/flights" "$NEW_DATA/reports"
    count=0
    for f in "$OLD_DATA/flights/"*.json; do
        [ -f "$f" ] || continue
        fname="$(basename "$f")"
        if [ ! -f "$NEW_DATA/flights/$fname" ]; then
            cp "$f" "$NEW_DATA/flights/"
            count=$((count + 1))
        fi
    done
    touch "$MIGRATE_MARKER"
    green "Migrated $count flight JSON files to $NEW_DATA/flights/"
    green "Note: HTML reports will be regenerated on first X-Plane start"
    green "(use 'Rebuild All Reports' in the Logbook window)"
else
    if [ -f "$MIGRATE_MARKER" ]; then
        green "FlyWithLua logs already migrated – skipping."
    fi
fi

green ""
green "Plugin installed. Activate via XLauncher, then restart X-Plane."
