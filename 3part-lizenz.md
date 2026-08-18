# Third-Party Licenses — xp_pilot

**Stand:** 2026-08-18 · **Version:** 1.6.3 · **Projektlizenz:** GPL-3.0-or-later, Copyright (C) 2026 thWelly

Dieses Dokument listet alle Fremdkomponenten auf, die xp_pilot zur Build-Zeit oder zur Laufzeit
verwendet, inklusive ihrer Lizenz und der Frage, ob sie einem kommerziellen Closed-Source- bzw.
Paid-Upgrade-Modell ("xp_pilot Pro") entgegenstehen.

---

## 1. In das Plugin kompilierte Bibliotheken

| Komponente | Version | Lizenz | Bezug | Kommerziell nutzbar |
|---|---|---|---|---|
| [Dear ImGui](https://github.com/ocornut/imgui) | 1.92.8 | MIT | `make setup` → `vendor/imgui/` | Ja |
| [nlohmann/json](https://github.com/nlohmann/json) | 3.12.0 | MIT | `make setup` → `vendor/json.hpp` | Ja |
| [X-Plane SDK (XPLM/XPWidgets)](https://developer.x-plane.com/sdk/) | XPSDK430 | Laminar Research SDK License (permissiv, ausdrücklich für kommerzielle Plugins) | `make setup` → `sdk/` | Ja |
| OpenGL (System-Framework) | — | Plattform-API, keine mitgelieferte Implementierung | macOS `-framework OpenGL`, Linux/Windows `OpenGL::GL` | Ja |

**Geplant:** Mit dem Kartenumbau aus Abschnitt 6 kommt **MapLibre GL JS (BSD-3-Clause)**
anstelle von Leaflet hinzu — ebenfalls permissiv, ebenfalls per CDN referenziert.

**Pflichten:** MIT und die SDK-Lizenz verlangen die Weitergabe des Copyright- und Lizenztextes.
Das genügt eine Datei wie diese bzw. ein `licenses/`-Verzeichnis im Release-Paket.
Keine Copyleft-Wirkung — der eigene Quellcode muss deswegen **nicht** offengelegt werden.

## 2. Nur für Tests (nicht Teil des ausgelieferten `.xpl`)

| Komponente | Version | Lizenz | Bezug | Kommerziell nutzbar |
|---|---|---|---|---|
| [Catch2](https://github.com/catchorg/Catch2) (amalgamated) | 3.15.1 | BSL-1.0 (Boost Software License) | `make setup` → `vendor/catch2/` | Ja |

BSL-1.0 verlangt bei reiner Binärauslieferung nicht einmal einen Lizenzhinweis; da Catch2 nur in
`xp_pilot_tests` landet, ist es für ein Auslieferungspaket ohnehin irrelevant.

## 3. Zur Laufzeit im HTML-Report geladen (CDN, `src/html_report.cpp`)

| Komponente | Version | Lizenz | Auslieferung | Kommerziell nutzbar |
|---|---|---|---|---|
| [Leaflet](https://leafletjs.com/) | 1.9.4 | BSD-2-Clause | `unpkg.com` CDN | Ja |
| [Chart.js](https://www.chartjs.org/) | 4.4.0 | MIT | `cdn.jsdelivr.net` CDN | Ja |

Beide werden nur referenziert, nicht mitgeliefert — die Lizenzhinweise stecken in den
ausgelieferten CDN-Dateien selbst. Würden sie künftig lokal gebündelt, müssten die Lizenztexte
mit ins Paket.

## 4. Datendienste (keine Bibliotheken, aber Nutzungsbedingungen)

| Dienst | Rolle | Bedingungen |
|---|---|---|
| CARTO Basemap Tiles (`basemaps.cartocdn.com`) | Kartenkacheln im Flugreport, `src/html_report.cpp`, `map_and_charts_script()` | Laut [CARTO-Dokumentation](https://docs.carto.com/faqs/carto-basemaps) sind die Basemaps **ausschließlich mit einer Enterprise-Lizenz** verfügbar; kostenlos nur für Non-Profit-Grantees. Attribution ist im Report vorhanden (`© OpenStreetMap contributors © CARTO`), reicht aber nicht aus. |
| OpenStreetMap (Datenquelle hinter den Tiles) | Kartendaten | ODbL, Attribution vorhanden — unkritisch. |
| SkyVector (Deep-Links) | Verlinkung auf Charts | Nur Links, keine Datenübernahme — unkritisch. |

> **Handlungspunkt — und zwar jetzt, nicht erst für Pro.** Die CARTO-Nutzung ist auch im
> heutigen Open-Source-Betrieb nicht sauber abgedeckt, weil xp_pilot weder Enterprise-Kunde
> noch Grantee ist. Ein Providerwechsel ist damit unabhängig von der Pro-Frage fällig —
> siehe Abschnitt 6.

Alles andere in diesem Dokument ist frei; CARTO ist die einzige Position mit echtem
Handlungsbedarf.

---

## 5. Bewertung für ein "xp_pilot Pro"-Modell

### 5.1 Fremdlizenzen sind kein Hindernis
Sämtliche Abhängigkeiten sind permissiv (MIT / BSD-2 / BSL-1.0 / Laminar SDK). Keine davon
erzwingt Offenlegung. Ein Closed-Source-Pro-Build ist aus Sicht der Third-Party-Lizenzen
uneingeschränkt möglich — sofern die Lizenztexte aus Abschnitt 1 dem Paket beiliegen.

### 5.2 Das Hindernis ist die eigene Projektlizenz (GPL-3.0)
xp_pilot steht selbst unter GPL-3.0. Das betrifft nicht die Fremdlibs, sondern den eigenen Code:

- **Als Copyright-Inhaber bist du an die GPL nicht gebunden.** Du darfst deinen eigenen Code
  jederzeit zusätzlich unter einer proprietären Lizenz herausgeben (Dual Licensing). Die GPL
  bindet nur die Empfänger, nicht den Urheber.
- **Bereits veröffentlichte Versionen bleiben GPL — unwiderruflich.** Version 1.6.3 und alles
  davor kann von jedem geforkt und weiterentwickelt werden. Das gewünschte "Einfrieren des
  Stands" funktioniert also genau so: alter Stand bleibt offen, neue Arbeit erscheint unter
  neuer Lizenz. Rückwirkend zurückziehen geht nicht.
- **Kritisch: fremde Beiträge.** Sobald Code von Dritten (Pull Requests, Patches) im Repo liegt,
  gehört dir das Copyright daran nicht — du kannst diese Teile nicht relizenzieren, ohne die
  Zustimmung der Beitragenden einzuholen oder den Code neu zu schreiben.
  → **Stand 2026-08-18 unkritisch:** `git log --format='%an <%ae>' | sort -u` liefert
  ausschliesslich Identitäten des Projektinhabers (`Rob Wellinger`, `rwellinger`, `thWelly`) —
  keine Fremdbeiträge, das Copyright liegt vollständig bei dir. Um das so zu halten: vor der
  Annahme externer Pull Requests ein CLA / DCO mit Relizenzierungsklausel einführen.
- **Praktische Folge des Freeze-Modells:** Der eingefrorene GPL-Stand ist eine dauerhafte
  Fork-Basis. Der Mehrwert von Pro muss also in den *neuen* Features liegen, nicht im Zugang zum
  Bestehenden.

### 5.3 Checkliste vor einem Pro-Launch
1. Copyright-Historie erneut auf Fremdbeiträge prüfen (aktuell sauber, siehe 5.2).
2. Tile-Provider umstellen — CARTO raus, Zielarchitektur nach Abschnitt 6 (bereits vor Pro fällig).
3. Lizenztexte von ImGui, nlohmann/json und dem X-Plane SDK dem Release-Paket beilegen.
4. Ab dem Umstellungszeitpunkt neue Commits unter die neue Lizenz stellen; den GPL-Stand als
   Tag/Branch klar markieren.
5. Dieses Dokument bei jedem Dependency-Update mitpflegen.

---

## 6. Kartenanbieter — Migrationsoptionen

Der Flugreport lädt seine Kacheln heute von CARTO (`src/html_report.cpp`, `map_and_charts_script()`). Das ist die
einzige Abhängigkeit, die einem Bezahlprodukt im Weg steht — und laut Abschnitt 4 auch schon
heute nicht sauber gedeckt.

### 6.1 Anbietervergleich

| Anbieter | Rolle | Lizenz / Kosten | API-Key | Kommerziell |
|---|---|---|---|---|
| CARTO (heute) | Basemap | Enterprise-Lizenz oder Non-Profit-Grant | nein | **Nein** |
| [OpenFreeMap](https://openfreemap.org/) | Basemap | Projekt unter MIT; öffentliche Instanz gratis, **keine Limits**, keine Registrierung, nur Attribution | **nein** | **Ja** |
| [MapTiler](https://www.maptiler.com/cloud/pricing/) | Basemap | Free: 5.000 Map-Sessions/Monat, **ausdrücklich nicht kommerziell**. Flex: 30 USD/Monat, 25.000 Sessions, kommerziell erlaubt | ja | Ja (ab Flex) |
| [openAIP](https://www.openaip.net/) | Aviation-**Overlay** | Daten unter CC BY-NC 4.0 | ja | **Nein** ohne schriftliche Freigabe |
| [OSM Standard Tiles](https://operations.osmfoundation.org/policies/tiles/) | Basemap | Tile Usage Policy: „heavy use“ untersagt, Sperrung ohne Vorwarnung möglich | nein | Bei ~2000 Usern ungeeignet |

### 6.2 Drei Befunde, die die Architektur bestimmen

**openAIP ersetzt CARTO nicht.** openAIP liefert ein Overlay mit Lufträumen und Flugplätzen,
das über einer Basemap liegt. Eine Basemap wird also in jedem Fall zusätzlich gebraucht — die
beiden Anbieter sind komplementär, keine Alternativen.

**Kein API-Key ist im Report absicherbar.** Reports sind lokale `file://`-HTML-Dateien beim
User. MapTilers Key-Restriction arbeitet über HTTP-Origin bzw. Referer — bei `file://` gibt es
keinen Origin, der Key wäre also ungeschützt und im Klartext aus jedem Report extrahierbar.
Ein zentral eingebetteter Key hätte zudem ein Mengenproblem: 25.000 Sessions im Flex-Tarif
entsprechen bei ~2000 Usern rund 12 Report-Öffnungen pro User und Monat, danach läuft die
Overage-Abrechnung. Das ist der Grund für das Hybrid-Modell in 6.3 — und es passt zum
local-first-Prinzip: kein Server, keine Accounts, keine laufenden Kosten pro User.

**Der Providerwechsel erzwingt einen Bibliothekswechsel.** OpenFreeMap liefert Vector-Tiles;
Leaflet 1.9.4 kann nur Raster. Der keyfreie Default bedingt damit den Umstieg auf
**MapLibre GL JS (BSD-3-Clause)**. Das ist der eigentliche Aufwand — nicht der URL-Tausch.
MapTiler liefert beide Formate und läuft unter MapLibre ebenfalls.

### 6.3 Zielarchitektur

- **Rendering:** MapLibre GL JS statt Leaflet.
- **Default-Basemap:** OpenFreeMap — ohne Key, ohne Limit, kommerziell erlaubt. Funktioniert
  für jeden User sofort, ohne Registrierung.
- **Optional:** Feld „MapTiler API key“ in den Settings. Wer einen eigenen Key hinterlegt,
  bekommt MapTilers Kartenstile; der Key bleibt in der lokalen `settings.json` und kostet dich
  nichts.
- **Aviation-Overlay:** openAIP als zusätzlich aktivierbarer Layer, ebenfalls mit
  User-eigenem Key.

### 6.4 Abgrenzung zwischen Open Source und Pro

openAIP steht unter CC BY-NC 4.0. Das Overlay bleibt deshalb der freien Version vorbehalten;
eine kostenpflichtige Pro-Version nutzt OpenFreeMap oder den MapTiler-Key des Users.

Diese Aufteilung ist lizenzrechtlich sauber, aber **kommunikativ heikel**: die kostenlose
Version hätte beim Aviation-Layer mehr zu bieten als die bezahlte. Zwei Auswege, die vor einem
Pro-Launch zu bewerten sind:

1. Bei openAIP eine schriftliche Freigabe für die kommerzielle Nutzung anfragen. Deren FAQ
   deutet an, dass das Mitliefern der Daten in bezahlten Anwendungen geduldet wird, solange die
   Daten nicht als solche verkauft werden — **das ist ohne schriftliche Bestätigung keine
   tragfähige Grundlage.**
2. Das Overlay in Pro durch eine kommerziell lizenzierte Aviation-Datenquelle ersetzen.

### 6.5 Offene Prüfpunkte vor der Umsetzung

- **openAIP-Tiles-Format, URL-Schema, Key-Übergabe und Rate-Limits sind nicht verifiziert.**
  Die Swagger-Doku rendert per JavaScript und `openaip.net` liefert 403 an automatisierte
  Abrufe — vor der Implementierung manuell prüfen.
- Exakter Wortlaut der openAIP-Nutzungsbedingungen zur kommerziellen Weitergabe.
- Ob MapTiler „Map Sessions“ bei `file://`-Seiten überhaupt sinnvoll zählt.

### 6.6 Aufwandsschätzung

| Schritt | Betroffen | Aufwand |
|---|---|---|
| Leaflet → MapLibre GL JS (Polyline, Marker, Tooltips → GeoJSON-Sources/Layers und Popups, `fitBounds` → `LngLatBounds`) | `src/html_report.cpp` (`REPORT_CDN_HEAD`, `map_and_charts_script`) | 0,5–1 Tag |
| Settings um `maptiler_api_key`, `openaip_api_key` und Layer-Toggle erweitern | `src/main.cpp` (`load_settings` / `Settings::save`) | 1–2 h |
| Settings-UI: Textfelder für die Keys (`ImGui::InputText` — bisher gibt es nur Checkbox, Combo, Slider und InputInt) | `src/logbook_ui.cpp` (`draw_settings_screen`) | 1–2 h |
| Key-Änderung wirksam machen | vorhandene „Regenerate all reports“-Funktion wiederverwenden | 0,5 h |
| Attribution abhängig vom aktiven Layer setzen | `src/html_report.cpp` | 1 h |

**Backwards-Kompatibilität:** `settings.json` wird durchgängig über `j.value(key, default)`
gelesen — neue Felder sind für Bestandsuser unkritisch. Bereits erzeugte Reports behalten ihre
CARTO-URL, bis der User sie neu generiert; ein Hinweis im Release-Text ist sinnvoll.

---

> Hinweis: Diese Zusammenstellung ist eine technische Bestandsaufnahme, keine Rechtsberatung.
> Vor einem Lizenzwechsel mit kommerzieller Verwertung ist eine anwaltliche Prüfung sinnvoll.
