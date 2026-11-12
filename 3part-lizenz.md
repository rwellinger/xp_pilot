# Third-Party Licenses — xp_pilot

**As of:** 2026-09-27 · **Version:** 1.8.2 · **Project license:** GPL-3.0-or-later, Copyright (C) 2026 thWelly

Every third-party component xp_pilot uses at build time or at runtime, with its license and
the obligations that come with it.

---

## 1. Libraries compiled into the plugin

| Component | Version | License | Obtained via |
|---|---|---|---|
| [Dear ImGui](https://github.com/ocornut/imgui) | 1.92.8 | MIT | `make setup` → `vendor/imgui/` |
| [nlohmann/json](https://github.com/nlohmann/json) | 3.12.0 | MIT | `make setup` → `vendor/json.hpp` |
| [X-Plane SDK (XPLM/XPWidgets)](https://developer.x-plane.com/sdk/) | XPSDK430 | Laminar Research SDK License | `make setup` → `sdk/` |
| OpenGL (system framework) | — | Platform API, no bundled implementation | macOS `-framework OpenGL`, Linux/Windows `OpenGL::GL` |
| [Roboto](https://github.com/googlefonts/roboto) Medium, Latin-1 subset | from Dear ImGui 1.92.8 | Apache-2.0 | `tools/generate_fonts.sh` → `src/fonts/roboto_medium.hpp` (committed) |
| [Font Awesome Free](https://fontawesome.com) solid, icon subset | 6.7.2 | SIL OFL 1.1 (font), CC BY 4.0 (icons) | `tools/generate_fonts.sh` → `src/fonts/fa_solid.hpp` (committed) |

**Obligations:** MIT, Apache-2.0, OFL 1.1 and the SDK license require the copyright and
license text to be passed on with the binaries. The release ZIP satisfies that with
`THIRD_PARTY_LICENSES.md` (source: `packaging/THIRD_PARTY_LICENSES.md`, copied by the release
job in `.github/workflows/build.yml`), next to the GPL `LICENSE`. None of them is copyleft
towards xp_pilot's own code; OFL only requires the fonts themselves to stay under OFL.
Update that file whenever a component in this table is added or upgraded.

## 2. Test-only dependencies (not part of the shipped `.xpl`)

| Component | Version | License | Obtained via |
|---|---|---|---|
| [Catch2](https://github.com/catchorg/Catch2) (amalgamated) | 3.15.1 | BSL-1.0 (Boost Software License) | `make setup` → `vendor/catch2/` |

BSL-1.0 does not require a license notice for binary-only distribution, and Catch2 only ends up
in `xp_pilot_tests`, so it is irrelevant for any release package.

## 3. Loaded at runtime by the HTML report (CDN, `src/html_report.cpp`)

| Component | Version | License | Delivery |
|---|---|---|---|
| [MapLibre GL JS](https://github.com/maplibre/maplibre-gl-js) | 5 | BSD-3-Clause | `unpkg.com` CDN |
| [Chart.js](https://www.chartjs.org/) | 4.4.0 | MIT | `cdn.jsdelivr.net` CDN |

Both are referenced, not redistributed — their license notices travel inside the CDN-served
files. If they are ever bundled locally, their license texts must ship with the package.

## 4. Data services (not libraries, but subject to terms of use)

| Service | Role | Terms |
|---|---|---|
| [OpenFreeMap](https://openfreemap.org/) | Basemap for the flight report | Public instance is free for any use, no registration, no API key, no usage limits. Requires the attribution the report renders: "OpenFreeMap © OpenMapTiles Data from OpenStreetMap". |
| OpenStreetMap | Map data behind the tiles | ODbL, attribution rendered in the report. |
| [Natural Earth](https://www.naturalearthdata.com/) | Coastlines, lakes, borders and place names drawn on the in-plugin track map, shipped as `data/coastlines.dat` and `data/cities.dat` | Public domain — "no permission needed" — so redistribution inside the plugin is unrestricted. Regenerate with `tools/build_map_data.py`. |
| X-Plane airspace database | Airspace outlines on the in-plugin track map | Read from the user's own X-Plane installation (`Resources/default data/airspaces/`). Nothing is copied or redistributed; the plugin only reads what the simulator already installed. |
| SkyVector | Deep links to charts | Links only, no data ingestion. |

---

Keep this document in sync whenever a dependency is added, removed or upgraded — in particular
when `make setup` pins a new version.

> Note: this is a technical inventory, not legal advice.
