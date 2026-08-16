### Flight Logger + Auto QNH plugin for X-Plane 12

Native plugin for **macOS (arm64 + x86_64 universal binary)**, **Linux (x86_64)** and **Windows**. Records flights, generates HTML logbook reports, rates landings, and keeps the altimeter in sync with actual QNH.


### What's New in v1.6.0

  - **Touchdown speed** — every landing now records the airspeed at the moment the gear touches: indicated airspeed and ground speed. Shown in the landing popup, the Logbook detail view and the HTML report. Stored as `ias_kts` and `ground_speed_kts` in the flight JSON.
  - **Runway analysis** — xp_pilot now knows *where* on the runway you touched down, not just how hard.
    - **Runway identifier**, **touchdown point** (distance past the threshold in metres and feet, plus the percentage of runway used), **runway remaining**, and **centerline deviation** (how far left or right of the centerline).
    - The runway is picked from the airport layout by touchdown position and true heading, so parallel runways are told apart correctly — verified against EDDF and KJFK. Displaced thresholds are honoured, so the distance is measured from the actual landing threshold rather than the start of the pavement.
    - The layout is read from X-Plane's global airport database. The lookup runs on a background thread during the approach, so there is no frame hitch in the touchdown frame.
    - Landings that cannot be matched — grass strips, water landings, helicopter set-downs, or airports missing from the database — simply omit the runway rows. Airports supplied by Custom Scenery add-ons fall back to the global layout, which may differ slightly for heavily modified airfields.
    - Can be switched off with the *Analyze touchdown point* toggle in the Settings tab.
    - Stored as `runway_ident`, `runway_distance_m`, `runway_offset_m` and `runway_length_m` in the flight JSON, alongside the touchdown position (`lat`, `lon`, `heading_true`) and the airport of that landing (`airport_icao`).
  - **Redesigned landing popup** — a colour-coded rating banner, metrics laid out in labelled columns, and a plan view of the runway showing exactly where you touched down relative to threshold and centerline. The same runway diagram appears in the HTML flight report's landing card.
  - **Replay the landing popup** — the popup can now be summoned at any time via the new command `xp_pilot/logbook/show_last_landing`, which can be bound to a key or joystick button in X-Plane's control settings. Also available from **Plugins → xp_pilot → Show Last Landing Rating** and from a button in the Settings tab. If the current session has no landing yet, the most recent landing from the logbook is shown — useful for reviewing or capturing a landing after restarting the sim. It works even when the automatic post-touchdown popup is switched off.
  - **Configurable popup position** — the landing popup can be placed in any of seven screen positions (the four corners, top or bottom centre, or dead centre) via a new dropdown in the Settings tab. The default placement is unchanged. Stored as `popup_position` in `settings.json`.
  - Flight logs written by this release use `version: 3`. Existing flight logs stay readable and their reports render exactly as before — the new fields simply stay hidden for landings recorded with earlier versions. No migration and no manual steps are required.


### What's New in v1.5.5

  - **Sim pauses no longer count as flight time** ([#3](https://github.com/rwellinger/xp_pilot/issues/3)) — pausing a flight and picking it up later used to produce impossible block times, because the block time was a plain wall-clock difference between takeoff roll and shutdown. The flight logger now accumulates only unpaused simulation time (`sim/time/paused`), so the block time reflects what was actually flown.
    - Flights that were paused show the full calculation in the HTML report and in the Logbook detail view: **Total** (gross), **Paused** and **Block Time** (net), down to the second so the three values visibly add up. Flights without a pause look exactly as before.
    - The block time starts with the takeoff roll, not with engine start. A pause *before* that — waiting on the runway — lies outside the block time and is therefore not counted; there is nothing to subtract it from.
    - **Pauses are marked on the route** — a yellow dot on the HTML report's map (hover for the duration) and on the track view in the logbook window, with a legend below the map. Flights recorded before this release have their pauses reconstructed from the gaps between track points, so older reports gain the markers when regenerated.
    - The pause total is stored as `paused_sec` in the flight JSON, alongside the exact active time in `block_time_sec` and a `pauses` array holding each pause with timestamp, duration and position; the file is now written as `version: 2`. Existing flight logs stay readable and unchanged — they simply report no pause, and their block time keeps its minute resolution.
    - Every pause is noted in X-Plane's `Log.txt` (`Sim resumed after N s`), so an unexpected block time can be traced without picking the flight JSON apart.
    - Track sampling pauses along with the sim, so the report's altitude and speed charts keep a true 10-second time axis instead of stretching across the pause.


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
  - Flight data stored as JSON in `<X-Plane>/Output/x_pilot_reports/flights/`
  - HTML logbook reports with track map, landing details, wind, and block time
  - Landing quality rating with aircraft-profile-specific thresholds (ultra_light → heavy_jet), auto-selected by ICAO code
  - Touchdown speed (IAS and ground speed) and runway placement — runway identifier, distance past the threshold, runway remaining, centerline deviation
  - Bounce detection — bounced landings are counted and the rating reflects the hardest touchdown
  - Touch-and-go support
  - Auto QNH: silent altimeter sync + optional on-screen warnings for mismatches and pilot/copilot disagreement
  - Manual QNH commands: `xp_pilot/qnh/set_qnh`, `xp_pilot/qnh/set_flightlevel`
  - Bindable logbook commands: `xp_pilot/logbook/toggle`, `xp_pilot/logbook/show_last_landing`
  - In-sim ImGui Logbook window with flight list, detail view, report regeneration, and the Settings tab


### Installation

  Download the `xp_pilot.zip` from the release assets, unzip it, and copy the `xp_pilot` folder into your X-Plane `Resources/plugins/` directory. The ZIP contains all three platform binaries (`mac_x64/`, `lin_x64/`, `win_x64/`) — X-Plane 12 will load the right one automatically. See the [README](README.md) for setup.


### Requirements

  - macOS 12.0+ (arm64 / x86_64 universal binary), Linux (x86_64), or Windows
  - X-Plane 12


### Known Limitations

  - The plugin must be validated in X-Plane 12; unit tests cover logic and parsing only
  - Flight data JSON format may change between minor versions; regenerate reports after upgrades if needed


### Release process

**Versioning:** Dev builds (`make build`) embed `SNAPSHOT` as the version string. Only release builds show the real version number from `VERSION.txt`.

1. Ensure all changes are committed and pushed to `main`
2. Run the release command:
   ```bash
   make release VERSION=1.3.0
   ```
   This will:
   - Write the version to `VERSION.txt`
   - Create a commit (`release 1.3.0`)
   - Create an annotated git tag (`v1.3.0`)
   - Push the tag to origin
3. On GitHub, [create a release](../../releases/new) from the pushed tag
4. The CI pipeline detects the `release` event and builds all three platforms with the real version number
5. The resulting `xp_pilot.zip` (containing macOS, Linux, and Windows binaries) is automatically attached to the GitHub release

**Local release build** (e.g. for testing before release):

```bash
make release-build
```
