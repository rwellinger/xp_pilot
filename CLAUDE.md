# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

**xp_pilot** is a C++17 X-Plane 12 flight simulator plugin for macOS. It provides two features: Flight Logger (records flight data and generates HTML logbook reports) and Auto QNH (automatic barometric altimeter management).

## Commands

```bash
# One-time setup: downloads X-Plane SDK headers, Dear ImGui v1.91.9, nlohmann/json v3.11.3
./setup.sh

# Build the plugin (CMake Release build → build/xp_pilot.xpl)
./build.sh

# Install to X-Plane's plugins directory with code signing
./install.sh
```

There are no automated tests — the plugin must be tested by loading it in X-Plane 12.

## Architecture

Modules coordinate through X-Plane's XPLM API:

- **`main.cpp`** — Plugin entry points (`XPluginStart`, `XPluginStop`, `XPluginEnable`, `XPluginDisable`). Registers the draw callback that drives the UI.
- **`flight_logger`** — Core data acquisition. Runs every frame sampling aircraft state via X-Plane datarefs. Saves flight data to `~/X-Plane 12/Output/FlightLogger/flights/` as JSON. Handles aircraft profiles for landing quality thresholds.
- **`auto_qnh`** — Monitors altimeter settings, syncs to actual QNH, issues warnings for mismatches, and registers X-Plane commands.
- **`logbook_ui`** — Dear ImGui window displaying flight history from JSON files, with delete/view/regenerate actions.
- **`html_report`** — Parses flight JSON and generates HTML reports in `~/X-Plane 12/Output/FlightLogger/reports/`.

Each module uses a C++ namespace with `init()` and `stop()` lifecycle functions.

## Key Data Structures

Defined in `html_report.hpp`:
- `TrackPoint` — timestamped lat/lon/altitude/speed/vertical speed sample
- `LandingData` — descent rate, G-force, pitch, wind, quality rating
- `FlightData` — complete flight record (aircraft, airports, block time, track points, landings)

Aircraft landing quality thresholds are configured in `data/flight_logger_profiles.json` by category (ultra_light through heavy_jet), with ICAO-code-based automatic profile selection.

## Build Details

- **CMake 3.21+**, C++17, macOS 12.0+ universal binary (arm64 + x86_64 via Rosetta)
- Output is `build/xp_pilot.xpl` (X-Plane plugin binary format)
- Vendor dependencies in `vendor/` (imgui/, json.hpp) and SDK headers in `sdk/` (XPLM/, XPWidgets/) — both populated by `setup.sh`, not committed to the repo
- Compiler flags: `-Wall -Wextra -fvisibility=hidden`, OpenGL deprecation warnings suppressed
