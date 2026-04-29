### Flight Logger + Auto QNH plugin for X-Plane 12

Native plugin for **macOS (arm64 + x86_64 universal binary)**, **Linux (x86_64)** and **Windows**. Records flights, generates HTML logbook reports, rates landings, and keeps the altimeter in sync with actual QNH.


### What's New in v1.3.2

  - **External volume support on macOS** — fixes flight log and HTML report writing when X-Plane is installed on an external disk mounted under `/Volumes/`. Earlier versions converted the SDK's HFS path by hand and silently dropped the volume mount prefix, so all plugin file I/O ended up pointing at the read-only system root. xp_pilot now requests POSIX paths via `XPLM_USE_NATIVE_PATHS` and resolves the data directory consistently on every platform.


### What's New in v1.3.0

  - **Bounce detection** — the flight logger now distinguishes a bounced landing from a clean one. When the main gear touches down, lifts off, and touches again before the nose gear settles, each additional touchdown is counted as a *bounce*.
    - The landing rating reflects the **hardest** touchdown, not the cushioned final settle — a bounced landing is judged by its worst impact.
    - A short low-altitude rebound (AGL < 5 ft) counts as a bounce; a higher hop is ignored, and a real climb above ~50 ft AGL still triggers a separate Touch-and-Go entry as before.
    - The bounce count is shown in the post-touchdown popup, in the Logbook flight detail list, and in the HTML report's landing card. Flights without bounces look exactly as before.
    - Stored as `bounce_count` in the flight JSON.


### Features

  - Automatic flight tracking with state machine (Idle → Rolling → Airborne → Landed → Shutdown)
  - Flight data stored as JSON in `~/X-Plane 12/Output/FlightLogger/flights/`
  - HTML logbook reports with track map, landing details, wind, and block time
  - Landing quality rating with aircraft-profile-specific thresholds (ultra_light → heavy_jet), auto-selected by ICAO code
  - Bounce detection — bounced landings are counted and the rating reflects the hardest touchdown
  - Touch-and-go support
  - Auto QNH: silent altimeter sync + optional on-screen warnings for mismatches and pilot/copilot disagreement
  - Manual QNH commands: `xp_pilot/qnh/set_qnh`, `xp_pilot/qnh/set_flightlevel`
  - In-sim ImGui Logbook window with flight list, detail view, report regeneration, and the new Settings tab


### Installation

  Download the `xp_pilot.zip` from the release assets, unzip it, and copy the `xp_pilot` folder into your X-Plane `Resources/plugins/` directory. The ZIP contains all three platform binaries (`mac_x64/`, `lin_x64/`, `win_x64/`) — X-Plane 12 will load the right one automatically. See the README for setup.


### Requirements

  - macOS 12.0+ (arm64 / x86_64 universal binary), Linux (x86_64), or Windows
  - X-Plane 12


### Known Limitations

  - No automated tests — the plugin must be validated in X-Plane 12
  - Flight data JSON format may change between minor versions; regenerate reports after upgrades if needed

  Full Changelog: https://github.com/thWelly/xp_pilot/compare/v1.3.0...v1.3.2
