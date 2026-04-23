### Flight Logger + Auto QNH plugin for X-Plane 12

macOS universal binary (arm64 + x86_64). Records flights, generates HTML logbook reports, rates landings, and keeps the altimeter in sync with actual QNH.


### What's New in v1.2.8

  - **New Settings tab in the Logbook window** — all feature toggles are now in one place. Open the Logbook (Plugins → xp_pilot → Open / Close Logbook) and switch to the **Settings** tab. Changes are saved immediately to `settings.json` and persist across sessions.
  - **Five independent toggles**, each usable on its own:
    - **Write flight logs to disk** (default: on) — when off, no JSON flight file and no HTML report are written. Flight tracking and all on-screen messages still work, they just don't produce artifacts.
    - **Auto QNH** (default: off) — silently syncs pilot and copilot altimeter to the actual sea-level pressure (skipped on standard 29.92).
    - **Show QNH warning messages** (default: on) — gates the on-screen *CHECK ALTIMETER* and *ALTIMETER DISAGREE* warnings. Independent of Auto QNH: warnings can be suppressed while auto-correction runs, or vice-versa.
    - **Show flight logger status messages** (default: on) — gates the on-screen overlays (*DEP cached*, *REC Flight recording started*, *Touch-and-Go*, *Flight saved*, etc.).
    - **Show landing rating popup** (default: on) — gates the post-touchdown popup with landing quality (BUTTER! / GREAT / ACCEPTABLE / HARD / WASTED) and metrics. **Independent of the log-writing toggle** — pilots who use Windows-only flight reporting tools but still want the landing rating can disable everything *except* this popup.
  - **Removed "Auto QNH" from the plugin menu** — Auto QNH is now toggled exclusively via the Settings tab. The plugin menu keeps only "Open / Close Logbook".
  - **New aircraft profile: Bombardier Challenger (CL60)** — mapped to the `vlj` landing-rating profile (same class as the Citation X).


### Features

  - Automatic flight tracking with state machine (Idle → Rolling → Airborne → Landed → Shutdown)
  - Flight data stored as JSON in `~/X-Plane 12/Output/FlightLogger/flights/`
  - HTML logbook reports with track map, landing details, wind, and block time
  - Landing quality rating with aircraft-profile-specific thresholds (ultra_light → heavy_jet), auto-selected by ICAO code
  - Touch-and-go support
  - Auto QNH: silent altimeter sync + optional on-screen warnings for mismatches and pilot/copilot disagreement
  - Manual QNH commands: `xp_pilot/qnh/set_qnh`, `xp_pilot/qnh/set_flightlevel`
  - In-sim ImGui Logbook window with flight list, detail view, report regeneration, and the new Settings tab


### Installation

  Download `xp_pilot.xpl`, place it into your X-Plane `Resources/plugins/xp_pilot/` directory. See the README for setup.


### Requirements

  - macOS 12.0+ (arm64 / x86_64 universal binary)
  - X-Plane 12


### Known Limitations

  - macOS only
  - No automated tests — the plugin must be validated in X-Plane 12
  - Flight data JSON format may change between minor versions; regenerate reports after upgrades if needed

  Full Changelog: https://github.com/thWelly/xp_pilot/compare/v1.2.7...v1.2.8
