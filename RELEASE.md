### Flight Logger + Auto QNH plugin for X-Plane 12

Native plugin for **macOS (arm64 + x86_64 universal binary)**, **Linux (x86_64)** and **Windows**. Records flights, generates HTML logbook reports, rates landings, and keeps the altimeter in sync with actual QNH.


### Unreleased

  - **Open a position on SkyVector** — coordinates are now one click away from an aeronautical chart instead of something to copy by hand.
    - The **Live** tab has a **SkyVector** button next to the position line; it opens the current aircraft position on [skyvector.com](https://skyvector.com/) in the default browser.
    - Every landing card in the HTML report gained a **Touchdown position** row showing the touchdown coordinates as a link to the same chart. Landings recorded before touchdown coordinates existed simply omit the row.
  - No file format change; flight logs stay at `version: 4`.


### What's New in v1.6.3

  - **Live tab — watch the flight while it is still running** — the Logbook window has a new first tab showing the flight in progress instead of making you wait for the report. It shows the departure airport, aircraft and off-blocks time, the running block time (pause-aware, exactly as the finished report counts it), the current position, altitude and AGL, indicated airspeed, vertical speed and true heading, the maxima reached so far, and the route flown from takeoff up to this moment on the same track map the logbook detail view uses. Landings already made during the flight — a touch-and-go en route — appear with their full rating.
    - The position line updates every frame; the track map follows the 10-second sampling grid, so the first line segment appears after about 20 seconds of flight.
    - Nothing is written to disk and no HTML report is generated — this is a read-only view of what the recorder already holds in memory.
    - When *Write flight logs to disk* is switched off the recorder keeps no track, so the tab says so instead of showing an empty map. All other live values still work.
    - Between flights the tab reads *No flight in progress*.
  - **Replay no longer pollutes the flight statistics** — entering X-Plane's replay mid-flight and leaving it again used to feed the replayed frames into the track and into the **Max Speed** and **Max Altitude** tiles. Replay time is now excluded from the active flight time, exactly like a sim pause, which also stops the track sampler for its duration. Flights ending in replay were already discarded and still are.
  - **Implausible speed samples are discarded** — a repositioning, an aircraft reload or a single bad frame could hand the logger one garbage airspeed reading, and the running maximum kept it for the rest of the flight. Readings outside 0–1000 kts, or jumping more than 150 kts from the previous 10-second sample, are now dropped instead of recorded.
  - No file format change; flight logs stay at `version: 4`.


### What's New in v1.6.2

  - **Fixed: the landing card's "AGL at 50ft gate" row never showed a 50 ft gate** — it always read 1–2 ft, and rightly so: the value was sampled in the touchdown frame, so it showed the height of the aircraft datum above ground at the moment the gear touched, not the height on approach. The gate itself was never recorded — only the label claimed it. The row has been present since the first release.
  - **The 50 ft gate is now real** — the flight logger detects the crossing of 50 ft AGL on descent and records the approach state there, interpolated between the two frames that straddle the gate so the reading does not depend on the frame rate. The landing card shows it as **Speed at 50ft gate** with indicated airspeed and descent rate — the numbers a landing is actually flown against, and a useful comparison with the **Touchdown Speed** row just above it: crossing the gate fast is what turns into float.
  - **Float time now starts at the same gate** — its timer used to trip at 15.0 m (49.2 ft); it now uses the exact 50 ft (15.24 m), so float time and gate reading describe the same segment of the approach.
  - Stored as `gate_ias_kts` and `gate_fpm` in the flight JSON. The file format stays at `version: 4` — the fields are purely additive. Existing flight logs stay readable and simply omit the gate row when their reports are regenerated; the old `agl_ft` field is still written and parsed, it is no longer displayed.


### What's New in v1.6.1

  - **Fixed: indicated airspeed was recorded about 1.94× too high** — the flight logger treated X-Plane's `indicated_airspeed` dataref as metres per second and converted it to knots, but the dataref already reports knots. Every IAS-derived value was therefore inflated: the **Max Speed** tile, the **IAS** chart in the HTML report, the **Touchdown Speed** row, and the landing popup's **TOUCHDOWN IAS** cell — which is why indicated airspeed read far above ground speed. Ground speed and all wind figures were never affected.
  - **Existing flights are corrected on read** — flight logs written by earlier versions keep their stored values, but their inflated speeds are scaled back when the file is parsed. Regenerating an older report from the Logbook window makes it show the correct speeds. No migration and no manual steps are required.
  - Flight logs written by this release use `version: 4`, marking the corrected airspeed scale.


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
  - 50 ft gate — indicated airspeed and descent rate recorded as the approach crosses 50 ft AGL
  - Bounce detection — bounced landings are counted and the rating reflects the hardest touchdown
  - Touch-and-go support
  - Auto QNH: silent altimeter sync + optional on-screen warnings for mismatches and pilot/copilot disagreement
  - Manual QNH commands: `xp_pilot/qnh/set_qnh`, `xp_pilot/qnh/set_flightlevel`
  - Bindable logbook commands: `xp_pilot/logbook/toggle`, `xp_pilot/logbook/show_last_landing`
  - In-sim ImGui Logbook window with flight list, detail view, report regeneration, and the Settings tab
  - Live tab — position, altitude, speed, running block time and the route flown so far, while the flight is still in progress
  - SkyVector links — open the live position or a recorded touchdown point on an aeronautical chart in the browser


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
