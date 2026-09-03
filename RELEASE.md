### Flight Logger + Auto QNH plugin for X-Plane 12

Native plugin for **macOS (arm64 + x86_64 universal binary)**, **Linux (x86_64)** and **Windows**. Records flights, generates HTML logbook reports, rates landings, and keeps the altimeter in sync with actual QNH.

### What's New in v1.8.0

  - **An aircraft the profile list does not name is now rated by its airframe** — anything missing from `flight_logger_profiles.json` was rated against `medium_ga`, whatever it was. That calls a normal −400 fpm airliner touchdown a **HARD LANDING!**, while an ultralight gets away with far more than it should. Helicopters already had a safety net, because X-Plane reports the airframe category itself; fixed-wing aircraft had none.
    - Maximum takeoff mass, engine count and engine type now place an unlisted type in one of the existing profiles: a jet up to 20 t on `vlj` and anything heavier on `heavy_jet`, a turboprop on `turboprop`, and a piston single on `ultra_light`, `light_ga` or `medium_ga` at 600 kg and 1500 kg. A piston twin never lands in the two light profiles, whatever it weighs.
    - The explicit ICAO list keeps priority throughout — the classification only replaces the fallback. Where the sim reports no usable mass, `medium_ga` applies exactly as before.
    - **Ratings change for aircraft that were not on the list.** Most become more appropriate for what is being flown; some become stricter, because a lax profile was being applied where a tight one belongs. Anything you disagree with can now be set by hand — see the next entry.
  - **You can set the landing profile yourself, from the Settings screen** — the aircraft-to-profile mapping lived only in `flight_logger_profiles.json`, which sits inside the plugin folder and is replaced on every update, so correcting a type by editing it never survived an upgrade. A **Landing profiles** section now sits on the Settings screen and covers the aircraft loaded in the sim.
    - It shows what that aircraft is rated against, the four thresholds behind it, and **where they came from** — your own assignment, the bundled aircraft list, or the airframe classification. That last line is what explains an unexpected rating, and it was previously visible only in `Log.txt`.
    - A dropdown assigns one of the bundled profiles, or hands the aircraft back to the automatic choice. Your assignment takes precedence over both the bundled list and the airframe classification.
    - **Custom thresholds** — the rating no longer has to be one of the eight bundled profiles. A pilot who finds `light_ga` slightly too strict for their type can enter the four descent rates themselves, for that type alone. Values must be negative and get harsher step by step, between −20 and −3000 fpm; anything else is refused rather than stored.
    - Every assignment you have made is listed underneath and can be removed individually. Changes take effect immediately, without restarting the sim.
    - For a helicopter, custom thresholds grade the descent rate only — drift, bank, yaw rate and G-force keep their fixed limits.
    - Assignments are stored under `aircraft_profiles` in the settings file, which can still be edited by hand with the sim closed. A string names a bundled profile, an object carries your own thresholds:

    ```json
    "aircraft_profiles": {
      "B77W": "heavy_jet",
      "C208": { "thresholds": [-150, -275, -400, -650] }
    }
    ```

    - The key is the ICAO type code exactly as the log line below prints it, matched exactly rather than as a substring — a short entry such as `B77` would otherwise capture every variant of a type at once.
    - An entry naming a profile that does not exist, or carrying thresholds that do not validate, is ignored and the normal lookup takes over, so a typo cannot leave a landing without thresholds.
    - The settings file lives outside the folder the updater owns, so an assignment survives plugin updates.
  - **The settings screen is laid out in two columns** — it only ever grew downwards while the window had width to spare, and the landing profiles section would have made that worse. The profile controls now take a column of their own and the four short sections share the other. A narrow window, or a high UI scale, keeps the previous stacked layout.
  - **The settings file moved to X-Plane's preferences folder** — it was written to `Output/x_pilot_reports/settings.json`, a folder named after the reports it sat next to, and nobody looks for configuration there. It now lives at `Output/preferences/xp_pilot.prf`, alongside the preference files of every other plugin.
    - **Your settings are moved for you on the first start** with this version, with every setting preserved. Nothing needs to be copied by hand.
    - If the old file cannot be read, it is left exactly where it is and the reason is written to `Log.txt`, so it can still be recovered by hand; the plugin starts from its defaults in that case rather than discarding anything.
    - The reasoning that put the file outside the plugin folder is unchanged — the SkunkCrafts updater owns that tree and would overwrite the file on every update. `Output/preferences/` satisfies that just as well and is the conventional place.
    - Flights, reports and the flight index stay in `Output/x_pilot_reports/`.
  - **Every aircraft load is reported in X-Plane's `Log.txt`** — the profile was previously visible only as one line in the HTML report, which left a surprising rating with no way back to the data behind it. The airframe figures and the resulting thresholds are now written whenever an aircraft is loaded:

    ```
    [xp_pilot] Aircraft 'EVOT': m_max=1950 engines=1 turbine -> profile turboprop [-150/-275/-400/-650]
    ```

  - **Fixed: two entries in the profile list never matched their aircraft** — both were written from the model name rather than the code the airframe actually sends. The Schleicher ASK 21 reports `AS21`, which `ASK2` does not contain, and the Lancair Evolution reports `EVOT`, not `EVOL`. Both aircraft had silently fallen through to `medium_ga` since their entries were added — the glider judged against light-aircraft thresholds, the turboprop against piston ones. The correct codes are added alongside the originals rather than replacing them, since add-ons reporting the longer codes still need them.
  - **Fixed: switching aircraft mid-session froze the sim for several seconds** — loading the first aircraft after startup was fine, but every change after that stopped the frames for 6 to 15 seconds, and it got worse with each further change in the same session. It happened only with xp_pilot loaded, and it happened with every one of the plugin's features switched off — which is what made it hard to find, because no plugin code was running at the time.
    - The cause was the drawing callback. X-Plane skips its entire OpenGL bookkeeping for a drawing phase that no plugin has asked for; the moment one has, every frame pays for saving and restoring GL state and, on the Metal backend, for synchronising textures across X-Plane's OpenGL bridge — whether the callback draws anything or not. xp_pilot registered its callback when the plugin loaded and held it for the whole session, while the three functions behind it returned immediately unless the logbook window, the landing popup or a QNH warning was actually up. An aircraft change is exactly when X-Plane throws away and reloads textures, so that is where the bill arrived.
    - The callback is now registered only while something is genuinely on screen and released again as soon as it is not. Nothing about the plugin's behaviour changes; it simply stops asking X-Plane for a rendering path it was not using.
    - **This also removes a per-frame cost that was there the whole time**, not only during aircraft changes — Laminar put the OpenGL bridge at up to 10 ms per frame. Anyone flying with the logbook closed was paying it for nothing.
    - Alongside it, closing the logbook no longer left a pending flight-list refresh to run a directory scan from inside the drawing callback; the next time the window opens it reloads anyway.
  - Flight logs written by this release use `version: 8`. `landing_profile` records which profile a flight was rated against and `landing_thresholds` the values behind it, so a regenerated report reproduces the rating a landing was given even after the assignment changed — and so custom thresholds, which have no profile name to look up, survive at all. Both fields are purely additive; older flights resolve through the ICAO lookup exactly as they did before.

### What's New in v1.7.2

  - **Every landing now records how the aircraft was configured** — the metrics said how hard you arrived, never how you had set the aircraft up to arrive. The landing card gained four rows, all sampled in the touchdown frame.
    - **Gear** — down and locked, or how far it actually got. The reading takes the least-extended gear leg the airframe actually has, so a partial extension shows up rather than being averaged away. Fixed-gear aircraft are reported as *Fixed* instead of being judged.
    - **Flaps** — the flap system's actual deployment. The flap handle is additionally sampled at the 50 ft gate, so a selection made in the flare is flagged as *changed below 50 ft*.
    - **Speedbrake** — armed, deployed, or retracted.
    - Gear, flaps and speedbrake are fixed-wing rows; a helicopter set-down omits them.
  - **Autoland is now visible as such** — the autopilot master state is recorded at touchdown and the landing card says **Hand-flown** or **Autopilot — autoland**. A **BUTTER!** rating means rather less when the autopilot flew the landing, and until now there was no way to tell the two apart in the logbook. Flight-director-only counts as hand-flown.
  - **The weather at touchdown is recorded** — reported visibility, the lowest broken or overcast ceiling above the airport, outside air temperature, and whether it was raining, summarised as **VMC** or **IMC** (below 5 km visibility or a ceiling under 1500 ft AGL). A −450 fpm arrival at 800 m visibility is a different piece of flying from the same figure in CAVOK, and the report now shows which one it was.
    - The ceiling is referenced to the elevation of the airport that was landed at, not to the ground under the aircraft, so it reads as a ceiling rather than as a height above terrain. Scattered and few layers are not a ceiling and are ignored.
  - **G-force is now a rating criterion for fixed-wing landings** — the rating followed the descent rate alone, which an aircraft dropped flat onto the runway can pass comfortably while recording a hefty vertical acceleration. Descent rate and G are now graded side by side and the **worse of the two decides**, exactly as helicopter landings have been judged since v1.7.1. The G steps are 1.4 / 1.7 / 2.1 / 2.6 for BUTTER / GREAT / ACCEPTABLE / HARD, anchored on the flight-data-monitoring convention of 2.1 G for a hard landing and 2.6 G for a severe one.
    - Descent-rate thresholds, the per-aircraft profiles and the crosswind allowance are unchanged. A landing that was rated on its descent rate before is rated the same now unless its G reading was worse than its rate.
    - Existing flight logs keep the rating they were stored with; regenerating an old report does not re-rate the landing.
  - **The landing popup** colours the **G-FORCE** cell by the grade it earns on its own, like the other metrics, and adds a context line below the flare verdict: conditions, flap setting, a gear warning if applicable, and *AUTOLAND* where it applies.
  - Flight logs written by this release use `version: 6`. The new fields are purely additive — older logs stay readable and simply omit the configuration and weather rows when their reports are regenerated, rather than showing zeros.
  - The credit for the idea belongs to [StableApproach](https://github.com/Clamb94/StableApproach), whose published configuration files made clear which of these parameters are worth recording.

### What's New in v1.7.1

  - **General improvements** of performance and stability
    - **Fixed: Helicopter support** useful parameters in landing rate for helicopters
    - **Fixed: Position Save** Window position and size is saved now after close

### What's New in v1.7.0

  - **The track map is now a real map** — the logbook's route view used to be a bare line on a flat background. It now draws the flight over geographic and aeronautical context, entirely from local data: no network connection, no API key, and it works with the sim offline.
    - **Airspaces** are read from X-Plane's own database (`Resources/default data/airspaces/`). Violet outlines mark controlled airspace, red marks restricted, prohibited and danger areas.
    - **Water and borders** come from a Natural Earth extract bundled with the plugin (`data/coastlines.dat`): lakes as filled shapes, coastlines and country borders as lines.
    - **Place names** (`data/cities.dat`) label the route largest-first. A label is skipped when it would collide with one already drawn, so names stay legible where cities cluster — a Pacific crossing no longer stacks ten Californian suburbs in one corner. Departure and arrival labels are reserved first and always win.
    - **What appears depends on the distance flown.** Airspaces are drawn only on local flights; beyond roughly 330 km a leg crosses several hundred of them, almost all irrelevant at cruise level, so the map switches to geography — which answers "where did I fly" far better at that scale.
    - **The track ramps from orange to near-white with altitude**, so a climb reads at a glance, and a scale bar plus ICAO labels for departure and arrival were added.
    - Loading runs on a background thread, so a long route never stalls the window.
  - **Fixed: the track map squashed north-south flights** — positions were stretched linearly from latitude and longitude into a fixed-aspect box, so a leg flown north distorted more the further it was from the equator. The map now uses Web Mercator with the aspect ratio preserved, and pads the shorter axis instead of deforming the route.
  - **Fixed: airspaces surrounding the route were missing entirely** — an airspace was kept only when one of its outline *points* fell inside the visible area. A control zone enclosing the airport you departed from has all its corners outside a short flight's bounds, so it was discarded: EDNY showed no airspaces at all, while nearby LSZG happened to sit close enough to an airspace edge to show some. The view centre is now also tested against each outline.
  - **Fixed: flights crossing the date line rendered as a world map** — longitude wraps at ±180°, so a Pacific crossing steps from 179.99 to -179.99 and the bounding box read that as "from one edge of the planet to the other". Auckland to Los Angeles drew the entire globe with the track jumping between the two sides. Longitudes are now expressed continuously across the date line.
  - **Fixed: long flights could corrupt the map** — Dear ImGui indexes vertices with 16 bits, and a long-haul route with all its coastlines exceeded that limit, which silently turned the map into noise. Points that would land on a pixel already covered are now skipped, and the overlay works to a fixed vertex budget. An eight-hour flight samples about 2,900 track points, of which some 650 are actually distinguishable at map resolution.
  - **Report maps moved from CARTO to OpenFreeMap** — CARTO's basemaps are licensed for enterprise customers and non-profit grantees, which xp_pilot is neither, so the previous usage was never properly covered. Reports now use [OpenFreeMap](https://openfreemap.org/), which is free for any use, needs no registration and no API key. Because the switch requires vector tiles, the report's map library changed from Leaflet to MapLibre GL JS. Existing reports keep their old map until you press **Rebuild All Reports** in the logbook.
    - No API key is involved anywhere, which also means a report you share with someone can never carry a credential of yours.
  - **Fixed: the logbook window could be scaled out of reach** — at 200% UI scale the window asked for 2120x1440 pixels regardless of your display. On any smaller screen its title bar and resize grip ended up off-screen, leaving no way to move or shrink it — and no way back to the scale setting that caused it. The window is now capped at 95% of the screen in both directions, and a large scale simply shows less at once.
    - **Reset from outside the window** — **Plugins → xp_pilot → Reset UI Scale & Window Size** restores 100% and recentres, deliberately placed in the plugin menu so it stays reachable no matter what the window is doing. Also available as the bindable command `xp_pilot/ui/reset_layout`, and as a **Reset** button next to the setting itself.
  - **Redesigned logbook window** — the window no longer opens on a row of tabs. It opens on a home screen of four large icon tiles (Live, Logbook, Archive, Settings), with a live status bar pinned above them that stays visible on every screen.
    - **Status bar** — aircraft type and tail number, departure airport and current position with true heading, and the live figures as labelled cells: altitude, indicated airspeed, vertical speed and running block time. A red **REC** indicator marks a flight being recorded; between flights it reads **IDLE** and the figures show placeholders, so the bar never changes height.
    - **Home screen** — each tile carries an icon and a live subtitle: whether a flight is recording, how many flights are in the logbook, how many are archived, and whether Auto QNH is on.
    - **Navigation** — every screen has a **‹ Home** button. `Esc` steps back to the home screen and only closes the window from there.
    - **Typography and icons** — the window now uses Roboto instead of Dear ImGui's built-in bitmap font, with Font Awesome icons on tiles, buttons and section headings. Both fonts are subset and compiled into the plugin (21 KB total), so nothing is loaded from disk or the network.
    - **UI scale** — a new setting scales fonts and spacing from 80% to 200% for high-DPI displays and TV-distance setups, in 5% steps via **−** / **+** buttons. It was a slider at first, but the scale applies live: dragging moved the slider out from under the cursor, which made overshooting to the maximum almost unavoidable. Stored as `ui_scale` in `settings.json`.
    - **Flight lists are real tables** — the logbook and archive lists use aligned columns with alternating row backgrounds instead of space-padded text, which no longer depends on a monospaced font.
    - Every action from the previous window is unchanged and in the same place: refresh, rebuild all reports, select all / clear, batch archive, batch delete, single archive / delete, open report, and the SkyVector link.
  - **Fixed: a phantom pause of one second** — flights that were never paused reported a one-second pause. The pause total was the difference between a whole-second wall clock and a continuously accumulated active time; because the two terms had different resolutions, the difference oscillated between 0 and 1 every second. In the live view this made the time row and the track map legend appear and disappear in a steady rhythm, and on a finished flight it wrote `paused_sec: 1` into the flight JSON while the `pauses` array stayed empty — so the HTML report showed a Total / Paused / Block Time breakdown for a flight that was never paused. Both counters are now fed from the same per-frame delta, so an unpaused flight reports exactly zero. Present since v1.5.5.
    - The time row now always shows all three figures; an unpaused flight simply shows a dash for the pause, so the layout no longer shifts when a flight crosses into a pause.
    - Existing flight logs keep their stored `paused_sec`. A spurious one-second pause in an older file disappears from the report when it is regenerated only if the flight is re-flown — the stored value itself is not rewritten.
  - **Open a position on SkyVector** — coordinates are now one click away from an aeronautical chart instead of something to copy by hand.
    - The **Live** screen has a **SkyVector** button; it opens the current aircraft position on [skyvector.com](https://skyvector.com/) in the default browser.
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
  - Flight data stored as JSON in `<X-Plane>/Output/x_pilot_reports/flights/`, settings in `<X-Plane>/Output/preferences/xp_pilot.prf`
  - HTML logbook reports with track map, landing details, wind, and block time
  - Landing quality rating from descent rate and vertical acceleration together, with aircraft-profile-specific fpm thresholds (ultra_light → heavy_jet), auto-selected by ICAO code — or from mass, engine count and engine type when the code is not listed, and assignable per aircraft — by profile or as custom thresholds — from the Settings screen
  - Touchdown speed (IAS and ground speed) and runway placement — runway identifier, distance past the threshold, runway remaining, centerline deviation
  - 50 ft gate — indicated airspeed and descent rate recorded as the approach crosses 50 ft AGL
  - Configuration at touchdown — gear, flaps (with late-change detection against the 50 ft gate), speedbrake, and whether the autopilot flew the landing
  - Conditions at touchdown — visibility, ceiling, temperature and precipitation, classified as VMC or IMC
  - Bounce detection — bounced landings are counted and the rating reflects the hardest touchdown
  - Touch-and-go support
  - Auto QNH: silent altimeter sync + optional on-screen warnings for mismatches and pilot/copilot disagreement
  - Manual QNH commands: `xp_pilot/qnh/set_qnh`, `xp_pilot/qnh/set_flightlevel`
  - Bindable logbook commands: `xp_pilot/logbook/toggle`, `xp_pilot/logbook/show_last_landing`
  - In-sim ImGui logbook window — tablet-style home screen with a live status bar, plus Live, Logbook, Archive and Settings screens
  - Live screen — position, altitude, speed, running block time and the route flown so far, while the flight is still in progress
  - Track map with airspaces, water, borders and place names — drawn entirely from local data, no network and no API key
  - Adjustable UI scale (80%–200%) for high-DPI displays, with a reset reachable from the plugin menu
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
