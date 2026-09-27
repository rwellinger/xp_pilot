# xp_pilot

**Flight Logger + Auto QNH plugin for X-Plane 12** — native on macOS (Apple Silicon + Intel), Linux and Windows.

Product website, downloads and documentation:
**https://thwelly.ch/xplane-plugins/xp-pilot/**

Source code and issue tracker: https://github.com/rwellinger/xp_pilot

## What it does

- **Flight Logger** — records every flight automatically, from engine start to shutdown, and turns it into an HTML logbook with route map, altitude and speed charts, and a detailed analysis of every landing: descent rate, G-force, touchdown point on the runway, centerline deviation, aircraft configuration and weather. Each landing is rated against a profile that matches the aircraft.
- **Auto QNH** — keeps the altimeter in sync with the actual sea-level pressure, and warns you when the setting is wrong or pilot and copilot disagree.
- **In-sim logbook window** — live view of the flight in progress, flight history, archive and all settings. Open it via **Plugins → xp_pilot → Open / Close Logbook**.

No FlyWithLua required. No account, no subscription.

## Installation

1. Unzip `xp_pilot.zip`.
2. Copy the whole `xp_pilot` folder into your X-Plane plugins directory:

```
X-Plane 12/Resources/plugins/xp_pilot/
├── mac_x64/xp_pilot.xpl   ← macOS (ARM + Intel universal binary)
├── lin_x64/xp_pilot.xpl   ← Linux (x86_64)
├── win_x64/xp_pilot.xpl   ← Windows
└── data/                  ← bundled, read-only data
```

3. Start X-Plane. It loads the right binary for your platform automatically.

To update, replace the folder with the one from a newer release. Your flights and settings are not stored in it and stay where they are. The plugin also supports the SkunkCrafts Updater.

**Requirements:** X-Plane 12 · macOS 12.0+ (arm64 / x86_64), Linux (x86_64) or Windows

## Where your data lives

Everything the plugin writes lives outside the plugin folder, so it survives updates:

```
<X-Plane>/Output/x_pilot_reports/   ← flight records (JSON), HTML reports, index.html
<X-Plane>/Output/preferences/xp_pilot.prf   ← settings
```

## Privacy

- **Your data stays on your machine.** Flights, landings and reports are plain JSON and HTML files in your X-Plane folder.
- **No account, no login, no telemetry.** The plugin sends nothing anywhere.
- **Flying works fully offline.** The in-sim track map draws only from local data: airspaces from X-Plane's own database, coastlines and place names bundled with the plugin.
- **The only network traffic** happens outside the sim, on your action: an HTML report loads map tiles from [OpenFreeMap](https://openfreemap.org/) and chart libraries from public CDNs when you open it in a browser, and a SkyVector link opens skyvector.com. No API key or personal data is embedded in a report, so reports are safe to share.

## License

xp_pilot is free software, licensed under the **GNU General Public License v3.0 or later** — see `LICENSE`.
Copyright (C) 2026 thWelly.

It includes third-party components under their own licenses (Dear ImGui, nlohmann/json, Roboto, Font Awesome, X-Plane SDK, Natural Earth data) — see `THIRD_PARTY_LICENSES.md`.

## What's new

See `RELEASE.md` for the changes in each version.
