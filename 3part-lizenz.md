# Third-Party Licenses — xp_pilot

**As of:** 2026-08-18 · **Version:** 1.6.3 · **Project license:** GPL-3.0-or-later, Copyright (C) 2026 thWelly

This document lists every third-party component xp_pilot uses at build time or at runtime,
together with its license and any obligations that come with it.

---

## 1. Libraries compiled into the plugin

| Component | Version | License | Obtained via | Commercial use |
|---|---|---|---|---|
| [Dear ImGui](https://github.com/ocornut/imgui) | 1.92.8 | MIT | `make setup` → `vendor/imgui/` | Yes |
| [nlohmann/json](https://github.com/nlohmann/json) | 3.12.0 | MIT | `make setup` → `vendor/json.hpp` | Yes |
| [X-Plane SDK (XPLM/XPWidgets)](https://developer.x-plane.com/sdk/) | XPSDK430 | Laminar Research SDK License (permissive, explicitly allows commercial plugins) | `make setup` → `sdk/` | Yes |
| OpenGL (system framework) | — | Platform API, no bundled implementation | macOS `-framework OpenGL`, Linux/Windows `OpenGL::GL` | Yes |

**Obligations:** MIT and the SDK license require the copyright and license text to be passed
on. A file such as this one, or a `licenses/` directory in the release package, satisfies that.
None of these licenses is copyleft, so they impose no disclosure requirement on our own source.

## 2. Test-only dependencies (not part of the shipped `.xpl`)

| Component | Version | License | Obtained via | Commercial use |
|---|---|---|---|---|
| [Catch2](https://github.com/catchorg/Catch2) (amalgamated) | 3.15.1 | BSL-1.0 (Boost Software License) | `make setup` → `vendor/catch2/` | Yes |

BSL-1.0 does not even require a license notice for binary-only distribution. Since Catch2 only
ends up in `xp_pilot_tests`, it is irrelevant for any release package.

## 3. Loaded at runtime by the HTML report (CDN, `src/html_report.cpp`)

| Component | Version | License | Delivery | Commercial use |
|---|---|---|---|---|
| [Leaflet](https://leafletjs.com/) | 1.9.4 | BSD-2-Clause | `unpkg.com` CDN | Yes |
| [Chart.js](https://www.chartjs.org/) | 4.4.0 | MIT | `cdn.jsdelivr.net` CDN | Yes |

Both are referenced, not redistributed — their license notices travel inside the CDN-served
files themselves. If they are ever bundled locally, their license texts must ship with the
package.

Leaflet will be replaced by **[MapLibre GL JS](https://github.com/maplibre/maplibre-gl-js)
(BSD-3-Clause)** as part of the map provider migration described in section 5. It is likewise
permissive and likewise CDN-referenced, so the obligations do not change.

## 4. Data services (not libraries, but subject to terms of use)

| Service | Role | Terms |
|---|---|---|
| CARTO basemap tiles (`basemaps.cartocdn.com`) | Map tiles in the flight report, `src/html_report.cpp`, `map_and_charts_script()` | Per the [CARTO documentation](https://docs.carto.com/faqs/carto-basemaps), the basemaps are available **exclusively under an Enterprise license**; free access is limited to non-profit grantees. The report does carry the required attribution (`© OpenStreetMap contributors © CARTO`), but attribution alone does not grant access. |
| OpenStreetMap (data behind the tiles) | Map data | ODbL, attribution present — unproblematic. |
| SkyVector (deep links) | Links to charts | Links only, no data ingestion — unproblematic. |

> **Action required.** xp_pilot is neither a CARTO Enterprise customer nor a grantee, so the
> current tile usage is not properly covered. Section 5 describes the replacement.

Everything else in this document is unencumbered; CARTO is the only item needing action.

---

## 5. Map provider migration

The flight report currently pulls its tiles from CARTO (`src/html_report.cpp`,
`map_and_charts_script()`). Per section 4 that usage is not covered, so the provider is being
replaced.

### 5.1 Provider comparison

| Provider | Role | License / cost | API key | Commercial use |
|---|---|---|---|---|
| CARTO (current) | Basemap | Enterprise license or non-profit grant | no | **No** |
| [OpenFreeMap](https://openfreemap.org/) | Basemap | Project under MIT; public instance is free, **no usage limits**, no registration, attribution only | **no** | **Yes** |
| [MapTiler](https://www.maptiler.com/cloud/pricing/) | Basemap | Free: 5,000 map sessions/month, **non-commercial only**. Flex: 30 USD/month, 25,000 sessions, commercial use permitted | yes | Yes (Flex and up) |
| [openAIP](https://www.openaip.net/) | Aviation **overlay** | Data under CC BY-NC 4.0 | yes | **No** without written permission |
| [OSM standard tiles](https://operations.osmfoundation.org/policies/tiles/) | Basemap | Tile Usage Policy forbids heavy use; access may be blocked without notice | no | Unsuitable at our user count |

### 5.2 Three findings that drive the design

**openAIP is not a CARTO replacement.** openAIP provides an overlay of airspaces and airfields
that sits *on top of* a basemap. A basemap is still required either way — the two providers are
complementary, not alternatives.

**No API key can be protected inside the report.** Reports are local `file://` HTML documents on
the user's machine. MapTiler's key restrictions work via HTTP origin or referer, and `file://`
has no origin, so an embedded key would be unprotected and trivially extractable from any
report. A centrally embedded key would also have a volume problem: 25,000 sessions on the Flex
plan amount to roughly 12 report views per user per month across the current user base, after
which overage billing starts. Hence the hybrid model in 5.3 — which also matches the
local-first principle: no server, no accounts, no per-user running costs.

**Switching providers forces a library switch.** OpenFreeMap serves vector tiles, and Leaflet
1.9.4 only handles raster. Choosing a key-free default therefore requires moving to
**MapLibre GL JS (BSD-3-Clause)**. That migration, not the URL swap, is the actual work.
MapTiler serves both formats and works under MapLibre as well.

### 5.3 Target architecture

- **Rendering:** MapLibre GL JS instead of Leaflet.
- **Default basemap:** OpenFreeMap — no key, no limits, commercial use permitted. Works for
  every user immediately, with no registration.
- **Optional:** a "MapTiler API key" field in the settings. Users who supply their own key get
  MapTiler's map styles; the key stays in the local `settings.json`.
- **Aviation overlay:** openAIP as an optional additional layer, also using the user's own key.

**Note on openAIP licensing:** openAIP data is CC BY-NC 4.0. Since tiles are fetched at runtime
by the user with the user's own API key, we neither redistribute nor bundle the data, which
keeps the NonCommercial clause out of our distribution. Should the data ever be bundled or the
overlay be offered through a key we supply, that assessment has to be revisited.

### 5.4 Open questions to resolve before implementation

- **openAIP tile format, URL scheme, key handling and rate limits are unverified.** The Swagger
  documentation renders via JavaScript and `openaip.net` returns 403 to automated requests —
  verify manually before implementing.
- The exact wording of the openAIP terms of use regarding redistribution.
- Whether MapTiler's "map session" accounting behaves sensibly for `file://` pages.

### 5.5 Effort estimate

| Step | Affected | Effort |
|---|---|---|
| Leaflet → MapLibre GL JS (polyline, markers, tooltips → GeoJSON sources/layers and popups, `fitBounds` → `LngLatBounds`) | `src/html_report.cpp` (`REPORT_CDN_HEAD`, `map_and_charts_script`) | 0.5–1 day |
| Extend settings with `maptiler_api_key`, `openaip_api_key` and a layer toggle | `src/main.cpp` (`load_settings` / `Settings::save`) | 1–2 h |
| Settings UI: text fields for the keys (`ImGui::InputText` — the screen currently uses only checkbox, combo, slider and InputInt) | `src/logbook_ui.cpp` (`draw_settings_screen`) | 1–2 h |
| Make key changes take effect | reuse the existing "regenerate all reports" function | 0.5 h |
| Set attribution according to the active layer | `src/html_report.cpp` | 1 h |

**Backwards compatibility:** `settings.json` is read throughout via `j.value(key, default)`, so
new fields are harmless for existing users. Reports generated earlier keep their CARTO URL until
the user regenerates them — worth mentioning in the release notes.

---

## 6. Maintenance

Keep this document in sync whenever a dependency is added, removed or upgraded — in particular
when `make setup` pins a new version.

> Note: this is a technical inventory, not legal advice.
