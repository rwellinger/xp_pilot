# xp_pilot

An X-Plane 12 plugin for macOS that combines three quality-of-life features for general aviation and jet simulation:

- **Flight Logger** — records every flight and generates an HTML logbook with route maps and landing analysis
- **Auto QNH** — automatically keeps the altimeter in sync with actual sea-level pressure
- **Star Wars Mode** — suppresses X-Plane's 3D rain particle effect at high speed (>120 kts)

---

## Features

### Flight Logger

Records a complete flight from engine start to shutdown and saves it as JSON plus an HTML report.

- Detects takeoff, airborne phase, landing, and shutdown automatically via a state machine
- Samples track points every 10 seconds (lat/lon, altitude, speed, vertical speed)
- Captures landing data at touchdown: descent rate (fpm), G-force, pitch, float time, flare quality, wind (headwind/crosswind)
- Rates each landing: **BUTTER!** / **GREAT LANDING!** / **ACCEPTABLE** / **HARD LANDING!** / **WASTED!**
- Thresholds are profile-based per aircraft category (see [Aircraft Profiles](#aircraft-profiles))
- HTML reports include a mini route map and charts; an `index.html` lists all flights
- Reports are stored next to the plugin: `data/flights/` and `data/reports/`

### Auto QNH

- Monitors the difference between actual QNH and the pilot's altimeter setting
- Shows an on-screen warning when the drift exceeds ~1.7 hPa
- Optional auto mode (toggle via menu) silently syncs both pilot and copilot baro to actual QNH
- Shows a second warning if pilot and copilot altimeters disagree by more than 0.01 inHg

| Command | Description |
|---|---|
| `xp_pilot/qnh/set_qnh` | One-shot: set both baros to current QNH |
| `xp_pilot/qnh/set_flightlevel` | One-shot: set both baros to 29.92 inHg |

### Star Wars Mode (Rain Blocker)

Suppresses X-Plane's 3D rain particles above 120 kts groundspeed. Hysteresis: rain returns below 80 kts. Toggle via the plugin menu or the command `xp_pilot/rain_blocker/toggle`.

---

## Installation

**Prerequisites:** Xcode Command Line Tools, CMake 3.21+, X-Plane 12

```bash
# 1. Download dependencies (X-Plane SDK, Dear ImGui, nlohmann/json)
./setup.sh

# 2. Build
./build.sh

# 3. Install to X-Plane (ad-hoc code-signed for macOS 12+)
./install.sh
```

The plugin is installed to:
```
~/X-Plane 12/Resources/available plugins/xp_pilot/
├── mac_x64/xp_pilot.xpl
└── data/
    ├── flight_logger_profiles.json
    ├── flights/        ← JSON flight records
    └── reports/        ← HTML reports + index.html
```

Activate it via [XLauncher](https://forums.x-plane.org/index.php?/files/file/82454-xlauncher/) or copy/symlink into `Resources/plugins/`.

---

## Plugin Menu

Under **Plugins → xp_pilot**:

| Item | Description |
|---|---|
| Auto QNH | Toggle automatic QNH sync (checkbox) |
| Open / Close Logbook | Open the in-sim flight logbook window |
| Star Wars Mode | Toggle rain suppression at high speed (checkbox) |

---

## Aircraft Profiles

Landing quality thresholds are configured per aircraft category in `data/flight_logger_profiles.json`. The plugin matches the aircraft's ICAO type code against the `match` strings in order.

| Profile | Butter | Great | Acceptable | Hard |
|---|---|---|---|---|
| `ultra_light` | < 75 fpm | < 150 fpm | < 250 fpm | < 400 fpm |
| `light_ga` | < 100 fpm | < 200 fpm | < 300 fpm | < 500 fpm |
| `medium_ga` | < 125 fpm | < 250 fpm | < 350 fpm | < 600 fpm |
| `turboprop` | < 150 fpm | < 275 fpm | < 400 fpm | < 650 fpm |
| `vlj` | < 200 fpm | < 350 fpm | < 500 fpm | < 750 fpm |
| `heavy_jet` | < 250 fpm | < 400 fpm | < 600 fpm | < 850 fpm |

The `shutdown_trigger` setting controls when a flight is finalised: `engine` (all engines off), `beacon` (beacon light off), or `nav_light` (nav lights off). Default is `engine`; can be overridden per aircraft entry.

---

## Development

```
src/
├── main.cpp            Plugin entry points, draw callback, menu
├── flight_logger.*     State machine, data acquisition, JSON save
├── html_report.*       HTML/index generation, JSON parsing
├── logbook_ui.*        Dear ImGui logbook window
├── auto_qnh.*          Altimeter monitoring and auto-sync
└── rain_blocker.*      Rain suppression at high speed
```

`sdk/` and `vendor/` are populated by `./setup.sh` and are not committed to the repository.

---

## Platform

macOS 12+ · Universal binary (arm64 + x86_64) · X-Plane 12
