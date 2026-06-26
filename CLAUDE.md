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
- **`flight_logger`** — Core data acquisition. Runs every frame sampling aircraft state via X-Plane datarefs. Saves flight data to `<X-Plane>/Output/x_pilot_reports/flights/` as JSON (resolved via `XPLMGetSystemPath`). Handles aircraft profiles for landing quality thresholds.
- **`auto_qnh`** — Monitors altimeter settings, syncs to actual QNH, issues warnings for mismatches, and registers X-Plane commands.
- **`logbook_ui`** — Dear ImGui window displaying flight history from JSON files, with delete/view/regenerate actions.
- **`html_report`** — Parses flight JSON and generates HTML reports in `<X-Plane>/Output/x_pilot_reports/reports/`.

Each module uses a C++ namespace with `init()` and `stop()` lifecycle functions.

**Data locations.** User data (flights, reports, `index.html`, `settings.json`) lives under `<X-Plane>/Output/x_pilot_reports/` so it survives plugin updates. The only bundled, read-only config is `flight_logger_profiles.json`, which stays in the plugin's own `data/` directory. On first start, `flight_logger::init()` migrates any user data left in the old in-plugin `data/` location to Output once (guarded by a `.migrated` marker).

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
- `make sanitize` builds + runs the Catch2 tests under ASan + UBSan in `build-sanitize/`. The `.xpl` plugin is intentionally not instrumented — for in-sim memory analysis use Instruments.app against the X-Plane process.

## Code Quality

All implementation in this repo must follow clean-code best practices. This applies to every change, now and in the future:

- **Single responsibility**: each function does one thing; each module owns one concern.
- **Meaningful names**: variables, functions and types read as plain English — no abbreviations, no cryptic suffixes.
- **Small functions, shallow nesting**: prefer early returns and helpers over deeply nested conditionals.
- **DRY**: extract shared logic rather than copying it; but don't abstract speculatively.
- **Encapsulation**: keep statics/internals private to their translation unit; expose only what the header promises.
- **Separation of concerns**: UI code never touches file I/O directly; data modules never draw.
- **Minimal comments**: let the code explain itself. Add a comment only when the *why* is non-obvious (invariant, workaround, surprising constraint). Don't comment what the code already says.
- **No speculative generality**: don't build abstractions for hypothetical future needs — match the existing codebase style.
- **Boundaries only for validation**: trust internal code; validate at the edges (user input, external APIs, file parsing).
